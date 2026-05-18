/**
 * @file SignalPreprocessing.cpp
 * @brief 信号预处理管道模块，整合IIR带通/陷波滤波、主动噪声消除（ANC）、自适应降噪、小波去噪、瞬态噪声抑制、运动伪迹削减和实时增益控制，支持导入离线批处理和实时流式多通道处理。
 */

#include "SignalPreprocessing.h"

#include "AdaptiveDownsampling.h"
#include "DataManager.h"
#include "DaqDeviceManager.h"
#include "TransientNoiseSuppressor.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <functional>
#include <limits>

namespace {
constexpr double kLowCutoffHz = 20.0;
constexpr double kHighCutoffHz = 2000.0;
constexpr double kPowerlineFundamentalHz = 50.0;
constexpr double kPowerlineSecondHarmonicHz = 100.0;
constexpr double kPowerlineThirdHarmonicHz = 150.0;
constexpr double kPowerlineFundamentalQFactor = 25.0;
constexpr double kPowerlineSecondHarmonicQFactor = 50.0;
constexpr double kPowerlineThirdHarmonicQFactor = 75.0;
constexpr double kMinimumCutoffGapHz = 1.0;
constexpr double kMinimumRealtimeGain = 0.5;
constexpr double kMaximumRealtimeGain = 5.0;
constexpr double kRealtimeGainEpsilon = 0.0001;
constexpr int kRealtimeChannelCount = 7;
constexpr int kRealtimeReferenceNoiseChannel = 7;
constexpr int kAncDebugLogEveryFrames = 10;
constexpr double kMinimumRmsForDb = 1e-12;
constexpr size_t kPreFilterSectionCount = 5;
constexpr int kNativeBandpassOrder = 4;
constexpr int kMinimumFirBandpassOrder = 128;
constexpr int kMaximumFirBandpassOrder = 8192;
constexpr double kFirBandpassImpulseSeconds = 0.12;
constexpr double kFirBandpassTailEnergyRatio = 1e-5;
constexpr int kMinimumScientificFilterOrder = 1;
constexpr int kMaximumScientificFilterOrder = 12;
constexpr double kMinimumScientificFilterFrequencyHz = 20.0;
constexpr double kPreviewMinimumScientificFilterFrequencyHz = 0.0;
constexpr double kMaximumScientificFilterFrequencyHz = 5000.0;
constexpr double kMinimumScientificFilterGapHz = 1.0;
constexpr double kMinimumScientificFilterBandwidthHz = 1.0;
constexpr double kMinimumScientificFilterRippleDb = 0.01;
constexpr double kMinimumScientificFilterAttenuationDb = 1.0;
constexpr int kScientificFilterResponsePointCount = 512;
constexpr int kPowerlineNotchCount = 3;
constexpr double kPi = 3.14159265358979323846;
constexpr double kAdaptiveNotchWindowSeconds = 1.0;
constexpr double kAdaptiveNotchFrameSeconds = 0.1;
constexpr double kAdaptiveNotchSearchHalfWidthHz = 1.0;
constexpr double kAdaptiveNotchSearchStepHz = 0.1;
constexpr double kAdaptiveNotchGuardRatio = 1.9952623149688795; // 6 dB amplitude ratio.
constexpr double kAdaptiveNotchTrackingAlpha = 0.2;
constexpr double kAdaptiveNotchReturnAlpha = 0.05;
constexpr double kAdaptiveNotchMaxStepHz = 0.2;
constexpr int kAdaptiveNotchReturnAfterMissedFrames = 10;
constexpr double kMinimumAdaptiveNotchMagnitude = 1e-12;
constexpr int kAdaptiveNotchMaxDetectionSampleRate = 12000;
constexpr std::array<double, kPowerlineNotchCount> kPowerlineFrequenciesHz{
    kPowerlineFundamentalHz,
    kPowerlineSecondHarmonicHz,
    kPowerlineThirdHarmonicHz
};
constexpr std::array<double, kPowerlineNotchCount> kPowerlineQFactors{
    kPowerlineFundamentalQFactor,
    kPowerlineSecondHarmonicQFactor,
    kPowerlineThirdHarmonicQFactor
};
constexpr std::array<double, 4> kAdaptiveNotchGuardOffsetsHz{ -3.0, -2.0, 2.0, 3.0 };

struct PreFilterDesign
{
    std::array<kfr::biquad_section<double>, kPreFilterSectionCount> sections{};
    size_t sectionCount = 0;

    bool hasSections() const
    {
        return sectionCount > 0;
    }

    kfr::iir_params<double, 5> params() const
    {
        return kfr::iir_params<double, 5>{ sections.data(), sectionCount };
    }
};

struct FirBandpassDesign
{
    kfr::univector<double> taps;

    bool hasTaps() const
    {
        return !taps.empty();
    }

    int order() const
    {
        return hasTaps() ? static_cast<int>(taps.size()) - 1 : 0;
    }
};

struct ScientificFilterDesign
{
    kfr::iir_params<double> params;
    QString errorMessage;

    bool hasSections() const
    {
        return params.size() > 0 && errorMessage.isEmpty();
    }
};

int resolveSampleRate(int samplingRate)
{
    if (samplingRate > 0) {
        return samplingRate;
    }

    qWarning() << "SignalPreprocessing: invalid sample rate" << samplingRate
               << ", fallback to" << DataManager::DEFAULT_SAMPLE_RATE;
    return DataManager::DEFAULT_SAMPLE_RATE;
}

bool isFrequencyWithinNyquist(double frequencyHz, int samplingRate)
{
    return static_cast<double>(samplingRate) > 2.0 * frequencyHz;
}

int normalizeNotchFrequencyMode(int mode)
{
    return mode == NotchFrequencyAdaptive
        ? NotchFrequencyAdaptive
        : NotchFrequencyFixed;
}

int normalizeWebRtcNoiseSuppressionLevel(int level)
{
    return std::clamp(
        level,
        static_cast<int>(AdaptiveNoiseReduction::NoiseSuppressionLow),
        static_cast<int>(AdaptiveNoiseReduction::NoiseSuppressionVeryHigh));
}

AdaptiveNoiseReduction::Parameters normalizeAdaptiveNoiseReductionParameters(
    AdaptiveNoiseReduction::Parameters parameters)
{
    parameters.noiseSuppressionLevel =
        normalizeWebRtcNoiseSuppressionLevel(parameters.noiseSuppressionLevel);
    return parameters;
}

bool adaptiveNoiseReductionParametersEqual(
    const AdaptiveNoiseReduction::Parameters& left,
    const AdaptiveNoiseReduction::Parameters& right)
{
    return left.noiseSuppressionLevel == right.noiseSuppressionLevel &&
        left.highPassFilterEnabled == right.highPassFilterEnabled &&
        left.automaticGainControlEnabled == right.automaticGainControlEnabled &&
        left.transientSuppressionEnabled == right.transientSuppressionEnabled;
}

double finiteOrDefault(double value, double defaultValue)
{
    return std::isfinite(value) ? value : defaultValue;
}

int normalizeScientificFilterPrototype(int prototype)
{
    return std::clamp(
        prototype,
        static_cast<int>(ScientificFilterBessel),
        static_cast<int>(ScientificFilterElliptic));
}

int normalizeScientificFilterType(int filterType)
{
    return std::clamp(
        filterType,
        static_cast<int>(ScientificFilterLowPass),
        static_cast<int>(ScientificFilterBandStop));
}

int normalizeScientificFilterOrder(int order)
{
    return std::clamp(
        order,
        kMinimumScientificFilterOrder,
        kMaximumScientificFilterOrder);
}

double normalizeScientificFilterFrequency(double frequencyHz, double defaultFrequencyHz)
{
    return std::clamp(
        finiteOrDefault(frequencyHz, defaultFrequencyHz),
        kMinimumScientificFilterFrequencyHz,
        kMaximumScientificFilterFrequencyHz);
}

double normalizeScientificFilterPositiveValue(double value, double defaultValue, double minimumValue)
{
    return std::max(minimumValue, finiteOrDefault(value, defaultValue));
}

ScientificFilterConfig normalizeScientificFilterConfig(ScientificFilterConfig config)
{
    ScientificFilterConfig defaults;
    config.prototype = normalizeScientificFilterPrototype(config.prototype);
    config.filterType = normalizeScientificFilterType(config.filterType);
    config.order = normalizeScientificFilterOrder(config.order);
    config.cutoffFrequencyHz =
        normalizeScientificFilterFrequency(config.cutoffFrequencyHz, defaults.cutoffFrequencyHz);
    config.lowCutoffFrequencyHz =
        normalizeScientificFilterFrequency(config.lowCutoffFrequencyHz, defaults.lowCutoffFrequencyHz);
    config.highCutoffFrequencyHz =
        normalizeScientificFilterFrequency(config.highCutoffFrequencyHz, defaults.highCutoffFrequencyHz);
    config.transitionBandwidthHz =
        normalizeScientificFilterPositiveValue(
            config.transitionBandwidthHz,
            defaults.transitionBandwidthHz,
            kMinimumScientificFilterBandwidthHz);
    config.stopbandAttenuationDb =
        normalizeScientificFilterPositiveValue(
            config.stopbandAttenuationDb,
            defaults.stopbandAttenuationDb,
            kMinimumScientificFilterAttenuationDb);
    config.passbandRippleDb =
        normalizeScientificFilterPositiveValue(
            config.passbandRippleDb,
            defaults.passbandRippleDb,
            kMinimumScientificFilterRippleDb);
    return config;
}

bool nearlyEqual(double left, double right)
{
    return std::abs(left - right) <= 0.0001;
}

bool scientificFilterConfigsEqual(
    const ScientificFilterConfig& left,
    const ScientificFilterConfig& right)
{
    return left.enabled == right.enabled &&
        left.prototype == right.prototype &&
        left.filterType == right.filterType &&
        left.order == right.order &&
        nearlyEqual(left.cutoffFrequencyHz, right.cutoffFrequencyHz) &&
        nearlyEqual(left.lowCutoffFrequencyHz, right.lowCutoffFrequencyHz) &&
        nearlyEqual(left.highCutoffFrequencyHz, right.highCutoffFrequencyHz) &&
        nearlyEqual(left.transitionBandwidthHz, right.transitionBandwidthHz) &&
        nearlyEqual(left.stopbandAttenuationDb, right.stopbandAttenuationDb) &&
        nearlyEqual(left.passbandRippleDb, right.passbandRippleDb);
}

ScientificFilterConfig scientificFilterConfigFromVariantMap(const QVariantMap& map)
{
    ScientificFilterConfig config;
    config.enabled = map.value(QStringLiteral("enabled"), config.enabled).toBool();
    config.prototype = map.value(QStringLiteral("prototype"), config.prototype).toInt();
    config.filterType = map.value(QStringLiteral("filterType"), config.filterType).toInt();
    config.order = map.value(QStringLiteral("order"), config.order).toInt();
    config.cutoffFrequencyHz =
        map.value(QStringLiteral("cutoffFrequencyHz"), config.cutoffFrequencyHz).toDouble();
    config.lowCutoffFrequencyHz =
        map.value(QStringLiteral("lowCutoffFrequencyHz"), config.lowCutoffFrequencyHz).toDouble();
    config.highCutoffFrequencyHz =
        map.value(QStringLiteral("highCutoffFrequencyHz"), config.highCutoffFrequencyHz).toDouble();
    config.transitionBandwidthHz =
        map.value(QStringLiteral("transitionBandwidthHz"), config.transitionBandwidthHz).toDouble();
    config.stopbandAttenuationDb =
        map.value(QStringLiteral("stopbandAttenuationDb"), config.stopbandAttenuationDb).toDouble();
    config.passbandRippleDb =
        map.value(QStringLiteral("passbandRippleDb"), config.passbandRippleDb).toDouble();
    return normalizeScientificFilterConfig(config);
}

double scientificFilterMaximumFrequencyHz(int sampleRate)
{
    const double nyquistHz = static_cast<double>(resolveSampleRate(sampleRate)) / 2.0;
    return std::min(kMaximumScientificFilterFrequencyHz, nyquistHz - kMinimumScientificFilterGapHz);
}

QString scientificFilterPrototypeLabel(int prototype)
{
    switch (normalizeScientificFilterPrototype(prototype)) {
    case ScientificFilterBessel:
        return QStringLiteral("贝塞尔");
    case ScientificFilterButterworth:
        return QStringLiteral("巴特沃斯");
    case ScientificFilterChebyshevI:
        return QStringLiteral("切比雪夫 I 型");
    case ScientificFilterElliptic:
        return QStringLiteral("椭圆");
    default:
        return QStringLiteral("未知");
    }
}

QString scientificFilterTypeLabel(int filterType)
{
    switch (normalizeScientificFilterType(filterType)) {
    case ScientificFilterLowPass:
        return QStringLiteral("低通");
    case ScientificFilterHighPass:
        return QStringLiteral("高通");
    case ScientificFilterBandPass:
        return QStringLiteral("带通");
    case ScientificFilterBandStop:
        return QStringLiteral("带阻");
    default:
        return QStringLiteral("未知");
    }
}

kfr::zpk makeScientificFilterPrototype(const ScientificFilterConfig& config)
{
    switch (normalizeScientificFilterPrototype(config.prototype)) {
    case ScientificFilterBessel:
        return kfr::bessel(config.order);
    case ScientificFilterButterworth:
        return kfr::butterworth(config.order);
    case ScientificFilterChebyshevI:
        return kfr::chebyshev1(config.order, config.passbandRippleDb);
    case ScientificFilterElliptic:
        return kfr::elliptic(config.order, config.passbandRippleDb, config.stopbandAttenuationDb);
    default:
        return kfr::butterworth(config.order);
    }
}

void setScientificFilterParams(
    ScientificFilterDesign& design,
    kfr::iir_params<double>&& params)
{
    design.params.clear();
    design.params.insert(design.params.end(), params.cbegin(), params.cend());
}

ScientificFilterDesign makeScientificFilterDesign(
    int sampleRate,
    ScientificFilterConfig config,
    bool requireEnabled)
{
    ScientificFilterDesign design;
    config = normalizeScientificFilterConfig(config);
    if (requireEnabled && !config.enabled) {
        return design;
    }

    const double sampleRateHz = static_cast<double>(resolveSampleRate(sampleRate));
    const double maximumFrequencyHz = scientificFilterMaximumFrequencyHz(sampleRate);
    if (maximumFrequencyHz < kMinimumScientificFilterFrequencyHz + kMinimumScientificFilterGapHz) {
        design.errorMessage = QStringLiteral("采样率过低，无法设计科学滤波器");
        return design;
    }

    const bool usesSingleCutoff =
        config.filterType == ScientificFilterLowPass ||
        config.filterType == ScientificFilterHighPass;
    if (usesSingleCutoff &&
        (config.cutoffFrequencyHz < kMinimumScientificFilterFrequencyHz ||
         config.cutoffFrequencyHz > maximumFrequencyHz)) {
        design.errorMessage =
            QStringLiteral("截止频率需位于 %1-%2 Hz")
                .arg(kMinimumScientificFilterFrequencyHz, 0, 'f', 0)
                .arg(maximumFrequencyHz, 0, 'f', 0);
        return design;
    }

    if (!usesSingleCutoff &&
        (config.lowCutoffFrequencyHz < kMinimumScientificFilterFrequencyHz ||
         config.highCutoffFrequencyHz > maximumFrequencyHz ||
         config.highCutoffFrequencyHz - config.lowCutoffFrequencyHz <
             kMinimumScientificFilterGapHz)) {
        design.errorMessage =
            QStringLiteral("低端/高端频率需位于 %1-%2 Hz，且高端大于低端")
                .arg(kMinimumScientificFilterFrequencyHz, 0, 'f', 0)
                .arg(maximumFrequencyHz, 0, 'f', 0);
        return design;
    }

    const kfr::zpk prototype = makeScientificFilterPrototype(config);
    switch (config.filterType) {
    case ScientificFilterLowPass:
        setScientificFilterParams(
            design,
            kfr::to_sos<double>(
                kfr::iir_lowpass(prototype, config.cutoffFrequencyHz, sampleRateHz)));
        break;
    case ScientificFilterHighPass:
        setScientificFilterParams(
            design,
            kfr::to_sos<double>(
                kfr::iir_highpass(prototype, config.cutoffFrequencyHz, sampleRateHz)));
        break;
    case ScientificFilterBandPass:
        setScientificFilterParams(
            design,
            kfr::to_sos<double>(
                kfr::iir_bandpass(
                    prototype,
                    config.lowCutoffFrequencyHz,
                    config.highCutoffFrequencyHz,
                    sampleRateHz)));
        break;
    case ScientificFilterBandStop:
        setScientificFilterParams(
            design,
            kfr::to_sos<double>(
                kfr::iir_bandstop(
                    prototype,
                    config.lowCutoffFrequencyHz,
                    config.highCutoffFrequencyHz,
                    sampleRateHz)));
        break;
    default:
        break;
    }

    if (design.params.size() > ScientificFilterSectionCapacity) {
        design.params.clear();
        design.errorMessage = QStringLiteral("滤波器阶数过高，SOS 节数量超过容量");
    }
    return design;
}

kfr::iir_params<double, ScientificFilterSectionCapacity> scientificFilterFixedParams(
    const ScientificFilterDesign& design)
{
    return kfr::iir_params<double, ScientificFilterSectionCapacity>(
        design.params.data(),
        design.params.size());
}

std::complex<double> evaluateScientificFilterResponse(
    const kfr::iir_params<double>& params,
    double frequencyHz,
    double sampleRateHz)
{
    const double omega = 2.0 * kPi * frequencyHz / sampleRateHz;
    const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -omega));
    const std::complex<double> z2 = z1 * z1;
    std::complex<double> response(1.0, 0.0);
    for (const kfr::biquad_section<double>& section : params) {
        const std::complex<double> numerator =
            section.b0 + section.b1 * z1 + section.b2 * z2;
        const std::complex<double> denominator =
            section.a0 + section.a1 * z1 + section.a2 * z2;
        if (std::abs(denominator) <= std::numeric_limits<double>::epsilon()) {
            continue;
        }
        response *= numerator / denominator;
    }
    return response;
}

QVariantMap pointMap(double x, double y)
{
    return QVariantMap{
        {QStringLiteral("x"), x},
        {QStringLiteral("y"), y}
    };
}

bool isFrequencyInScientificPassband(
    double frequencyHz,
    const ScientificFilterConfig& config,
    double maximumFrequencyHz)
{
    const double transitionHz =
        std::max(kMinimumScientificFilterBandwidthHz, config.transitionBandwidthHz);
    switch (config.filterType) {
    case ScientificFilterLowPass:
        return frequencyHz >= kMinimumScientificFilterFrequencyHz &&
            frequencyHz <= std::max(
                kMinimumScientificFilterFrequencyHz,
                config.cutoffFrequencyHz - transitionHz);
    case ScientificFilterHighPass:
        return frequencyHz >= std::min(
                maximumFrequencyHz,
                config.cutoffFrequencyHz + transitionHz) &&
            frequencyHz <= maximumFrequencyHz;
    case ScientificFilterBandPass:
        return frequencyHz >= config.lowCutoffFrequencyHz + transitionHz &&
            frequencyHz <= config.highCutoffFrequencyHz - transitionHz;
    case ScientificFilterBandStop:
        return (frequencyHz >= kMinimumScientificFilterFrequencyHz &&
                frequencyHz <= config.lowCutoffFrequencyHz - transitionHz) ||
            (frequencyHz >= config.highCutoffFrequencyHz + transitionHz &&
             frequencyHz <= maximumFrequencyHz);
    default:
        return false;
    }
}

bool assignBool(bool& target, bool value)
{
    if (target == value) {
        return false;
    }
    target = value;
    return true;
}

bool assignInt(int& target, int value)
{
    if (target == value) {
        return false;
    }
    target = value;
    return true;
}

bool assignDouble(double& target, double value)
{
    if (nearlyEqual(target, value)) {
        return false;
    }
    target = value;
    return true;
}

std::array<double, kPowerlineNotchCount> fixedPowerlineFrequencies()
{
    return kPowerlineFrequenciesHz;
}

void appendPowerlineNotchSection(
    std::array<kfr::biquad_section<double>, kPreFilterSectionCount>& sections,
    size_t& sectionCount,
    double centerFrequencyHz,
    double qFactor,
    double sampleRateHz)
{
    if (sectionCount >= sections.size()) {
        return;
    }

    const double normalizedFrequency = centerFrequencyHz / sampleRateHz;
    sections[sectionCount++] = kfr::biquad_notch(normalizedFrequency, qFactor);
}

void appendNativeBandpassSections(
    PreFilterDesign& design,
    double lowCutoffHz,
    double highCutoffHz,
    double sampleRateHz)
{
    const kfr::iir_params<double> bandpassSections =
        kfr::to_sos<double>(kfr::iir_bandpass(
            // KFR doubles the low-pass prototype order when transforming to band-pass.
            kfr::butterworth(kNativeBandpassOrder / 2),
            lowCutoffHz,
            highCutoffHz,
            sampleRateHz));

    for (const kfr::biquad_section<double>& section : bandpassSections) {
        if (design.sectionCount >= design.sections.size()) {
            qWarning() << "SignalPreprocessing: native bandpass section count exceeds capacity";
            break;
        }

        design.sections[design.sectionCount++] = section;
    }
}

PreFilterDesign makeBandpassFilterDesign(int sampleRate, bool enabled)
{
    PreFilterDesign design{};
    const double sampleRateHz = static_cast<double>(sampleRate);
    const double nyquistHz = sampleRateHz / 2.0;
    if (enabled &&
        isFrequencyWithinNyquist(kLowCutoffHz + kMinimumCutoffGapHz, sampleRate)) {
        const double effectiveHighCutoffHz =
            std::min(kHighCutoffHz, nyquistHz - kMinimumCutoffGapHz);
        if (effectiveHighCutoffHz > kLowCutoffHz) {
            appendNativeBandpassSections(
                design,
                kLowCutoffHz,
                effectiveHighCutoffHz,
                sampleRateHz);
        }
    }

    return design;
}

int maximumFirBandpassTapCount(int sampleRate)
{
    const int tapsByImpulseDuration = static_cast<int>(
        std::round(static_cast<double>(sampleRate) * kFirBandpassImpulseSeconds));
    return std::clamp(
        tapsByImpulseDuration,
        kMinimumFirBandpassOrder + 1,
        kMaximumFirBandpassOrder + 1);
}

size_t selectFirBandpassTapCount(const kfr::univector<double>& impulseResponse)
{
    if (impulseResponse.empty()) {
        return 0;
    }

    const size_t minimumTapCount = std::min(
        impulseResponse.size(),
        static_cast<size_t>(kMinimumFirBandpassOrder + 1));

    double totalEnergy = 0.0;
    for (double sample : impulseResponse) {
        totalEnergy += sample * sample;
    }
    if (totalEnergy <= 0.0 || !std::isfinite(totalEnergy)) {
        return minimumTapCount;
    }

    double tailEnergy = totalEnergy;
    for (size_t index = 0; index < impulseResponse.size(); ++index) {
        tailEnergy -= impulseResponse[index] * impulseResponse[index];
        const size_t tapCount = index + 1;
        if (tapCount >= minimumTapCount &&
            tailEnergy <= totalEnergy * kFirBandpassTailEnergyRatio) {
            return tapCount;
        }
    }

    return impulseResponse.size();
}

FirBandpassDesign makeFirBandpassFilterDesign(int sampleRate, bool enabled)
{
    FirBandpassDesign design{};
    if (!enabled) {
        return design;
    }

    const PreFilterDesign iirDesign = makeBandpassFilterDesign(sampleRate, true);
    if (!iirDesign.hasSections()) {
        return design;
    }

    const int maximumTapCount = maximumFirBandpassTapCount(sampleRate);
    kfr::univector<double> impulse(static_cast<size_t>(maximumTapCount));
    for (size_t index = 0; index < impulse.size(); ++index) {
        impulse[index] = index == 0 ? 1.0 : 0.0;
    }

    kfr::univector<double> impulseResponse(static_cast<size_t>(maximumTapCount));
    kfr::iir_state<double, 5> impulseState{ iirDesign.params() };
    kfr::process(impulseResponse, kfr::iir(impulse, std::ref(impulseState)));

    const size_t selectedTapCount = selectFirBandpassTapCount(impulseResponse);
    if (selectedTapCount == 0) {
        return design;
    }

    design.taps = kfr::univector<double>(selectedTapCount);
    for (size_t index = 0; index < selectedTapCount; ++index) {
        design.taps[index] = impulseResponse[index];
    }

    return design;
}

PreFilterDesign makeNotchFilterDesign(
    int sampleRate,
    bool enabled,
    const std::array<double, kPowerlineNotchCount>& frequenciesHz)
{
    PreFilterDesign design{};
    if (!enabled) {
        return design;
    }

    const double sampleRateHz = static_cast<double>(sampleRate);
    for (int index = 0; index < kPowerlineNotchCount; ++index) {
        const double centerFrequencyHz = frequenciesHz[static_cast<size_t>(index)];
        if (isFrequencyWithinNyquist(centerFrequencyHz + kMinimumCutoffGapHz, sampleRate)) {
            appendPowerlineNotchSection(
                design.sections,
                design.sectionCount,
                centerFrequencyHz,
                kPowerlineQFactors[static_cast<size_t>(index)],
                sampleRateHz);
        }
    }

    return design;
}

PreFilterDesign makePreFilterDesign(
    int sampleRate,
    const SignalPreprocessOptions& options)
{
    PreFilterDesign design{};
    const double sampleRateHz = static_cast<double>(sampleRate);
    const double nyquistHz = sampleRateHz / 2.0;
    if (options.bandpassEnabled &&
        isFrequencyWithinNyquist(kLowCutoffHz + kMinimumCutoffGapHz, sampleRate)) {
        const double effectiveHighCutoffHz =
            std::min(kHighCutoffHz, nyquistHz - kMinimumCutoffGapHz);
        if (effectiveHighCutoffHz > kLowCutoffHz) {
            appendNativeBandpassSections(
                design,
                kLowCutoffHz,
                effectiveHighCutoffHz,
                sampleRateHz);
        }
    }

    if (options.notchEnabled &&
        isFrequencyWithinNyquist(kPowerlineFundamentalHz + kMinimumCutoffGapHz, sampleRate)) {
        appendPowerlineNotchSection(
            design.sections,
            design.sectionCount,
            kPowerlineFundamentalHz,
            kPowerlineFundamentalQFactor,
            sampleRateHz);
    }

    if (options.notchEnabled &&
        isFrequencyWithinNyquist(kPowerlineSecondHarmonicHz + kMinimumCutoffGapHz, sampleRate)) {
        appendPowerlineNotchSection(
            design.sections,
            design.sectionCount,
            kPowerlineSecondHarmonicHz,
            kPowerlineSecondHarmonicQFactor,
            sampleRateHz);
    }

    if (options.notchEnabled &&
        isFrequencyWithinNyquist(kPowerlineThirdHarmonicHz + kMinimumCutoffGapHz, sampleRate)) {
        appendPowerlineNotchSection(
            design.sections,
            design.sectionCount,
            kPowerlineThirdHarmonicHz,
            kPowerlineThirdHarmonicQFactor,
            sampleRateHz);
    }

    return design;
}

QVector<float> applyIirPreFilter(
    const QVector<float>& rawData,
    kfr::iir_state<double, 5>& filterState)
{
    if (rawData.isEmpty()) {
        return {};
    }

    kfr::univector<double> inputDouble(static_cast<size_t>(rawData.size()));
    std::transform(
        rawData.cbegin(),
        rawData.cend(),
        inputDouble.begin(),
        [](float value) { return static_cast<double>(value); });

    kfr::univector<double> outputDouble(static_cast<size_t>(rawData.size()));
    kfr::process(outputDouble, kfr::iir(inputDouble, std::ref(filterState)));

    QVector<float> filteredData(rawData.size());
    std::transform(
        outputDouble.begin(),
        outputDouble.end(),
        filteredData.begin(),
        [](double value) { return static_cast<float>(value); });

    return filteredData;
}

QVector<float> applyScientificIirPreFilter(
    const QVector<float>& rawData,
    kfr::iir_state<double, ScientificFilterSectionCapacity>& filterState)
{
    if (rawData.isEmpty()) {
        return {};
    }

    kfr::univector<double> inputDouble(static_cast<size_t>(rawData.size()));
    std::transform(
        rawData.cbegin(),
        rawData.cend(),
        inputDouble.begin(),
        [](float value) { return static_cast<double>(value); });

    kfr::univector<double> outputDouble(static_cast<size_t>(rawData.size()));
    kfr::process(outputDouble, kfr::iir(inputDouble, std::ref(filterState)));

    QVector<float> filteredData(rawData.size());
    std::transform(
        outputDouble.begin(),
        outputDouble.end(),
        filteredData.begin(),
        [](double value) { return static_cast<float>(value); });

    return filteredData;
}

QVector<float> applyFirPreFilter(
    const QVector<float>& rawData,
    kfr::fir_state<double, double>& filterState)
{
    if (rawData.isEmpty()) {
        return {};
    }

    kfr::univector<double> inputDouble(static_cast<size_t>(rawData.size()));
    std::transform(
        rawData.cbegin(),
        rawData.cend(),
        inputDouble.begin(),
        [](float value) { return static_cast<double>(value); });

    kfr::univector<double> outputDouble(static_cast<size_t>(rawData.size()));
    kfr::process(outputDouble, kfr::fir(inputDouble, std::ref(filterState)));

    QVector<float> filteredData(rawData.size());
    std::transform(
        outputDouble.begin(),
        outputDouble.end(),
        filteredData.begin(),
        [](double value) { return static_cast<float>(value); });

    return filteredData;
}

QVector<float> applyPreFilterDesign(
    const QVector<float>& rawData,
    const PreFilterDesign& design)
{
    if (rawData.isEmpty() || !design.hasSections()) {
        return rawData;
    }

    kfr::iir_state<double, 5> filterState{ design.params() };
    return applyIirPreFilter(rawData, filterState);
}

void resetAdaptiveNotchTracking(SignalPreFilterState& state)
{
    state.adaptiveNotchHistory.clear();
    state.adaptiveNotchFrequencies = fixedPowerlineFrequencies();
    state.adaptiveNotchMissedFrames.fill(0);
}

void initializePreFilterState(
    SignalPreFilterState& state,
    const PreFilterDesign& bandpassDesign,
    const FirBandpassDesign& firBandpassDesign,
    bool useFirBandpass,
    const ScientificFilterDesign& scientificFilterDesign,
    bool useScientificFilter,
    const PreFilterDesign& notchDesign)
{
    state.bandpassState = !useFirBandpass && bandpassDesign.hasSections()
        ? kfr::iir_state<double, 5>{ bandpassDesign.params() }
        : kfr::iir_state<double, 5>{ kfr::iir_params<double, 5>() };
    state.bandpassFirState = useFirBandpass && firBandpassDesign.hasTaps()
        ? std::make_unique<kfr::fir_state<double, double>>(
              kfr::fir_params<double>{ firBandpassDesign.taps })
        : nullptr;
    state.bandpassFirOrder = useFirBandpass ? firBandpassDesign.order() : 0;
    state.scientificFilterState = useScientificFilter && scientificFilterDesign.hasSections()
        ? kfr::iir_state<double, ScientificFilterSectionCapacity>{
              scientificFilterFixedParams(scientificFilterDesign) }
        : kfr::iir_state<double, ScientificFilterSectionCapacity>{
              kfr::iir_params<double, ScientificFilterSectionCapacity>() };
    state.notchState = notchDesign.hasSections()
        ? kfr::iir_state<double, 5>{ notchDesign.params() }
        : kfr::iir_state<double, 5>{ kfr::iir_params<double, 5>() };
    resetAdaptiveNotchTracking(state);
}

void initializePreFilterState(
    SignalPreFilterState& state,
    int sampleRate,
    const SignalPreprocessOptions& options,
    bool* hasBandpassFilterState = nullptr,
    bool* hasScientificFilterState = nullptr,
    bool* hasNotchFilterState = nullptr)
{
    const PreFilterDesign bandpassDesign =
        makeBandpassFilterDesign(sampleRate, options.bandpassEnabled);
    const FirBandpassDesign firBandpassDesign =
        makeFirBandpassFilterDesign(
            sampleRate,
            options.bandpassEnabled && options.firFilterEnabled);
    const ScientificFilterDesign scientificFilterDesign =
        makeScientificFilterDesign(sampleRate, options.scientificFilter, true);
    const PreFilterDesign notchDesign =
        makeNotchFilterDesign(sampleRate, options.notchEnabled, fixedPowerlineFrequencies());

    if (hasBandpassFilterState != nullptr) {
        *hasBandpassFilterState = options.firFilterEnabled
            ? firBandpassDesign.hasTaps()
            : bandpassDesign.hasSections();
    }
    if (hasScientificFilterState != nullptr) {
        *hasScientificFilterState = scientificFilterDesign.hasSections();
    }
    if (hasNotchFilterState != nullptr) {
        *hasNotchFilterState = notchDesign.hasSections();
    }

    initializePreFilterState(
        state,
        bandpassDesign,
        firBandpassDesign,
        options.firFilterEnabled,
        scientificFilterDesign,
        options.scientificFilter.enabled,
        notchDesign);
}

QVector<float> applyBandpassFilterFrame(
    const QVector<float>& rawData,
    int sampleRate,
    const SignalPreprocessOptions& options,
    SignalPreFilterState* filterState,
    bool hasBandpassFilterState)
{
    if (rawData.isEmpty() || !options.bandpassEnabled) {
        return rawData;
    }

    if (options.firFilterEnabled) {
        if (filterState != nullptr &&
            hasBandpassFilterState &&
            filterState->bandpassFirState != nullptr) {
            return applyFirPreFilter(rawData, *filterState->bandpassFirState);
        }

        const FirBandpassDesign firBandpassDesign =
            makeFirBandpassFilterDesign(sampleRate, true);
        if (!firBandpassDesign.hasTaps()) {
            return rawData;
        }

        kfr::fir_state<double, double> temporaryState{
            kfr::fir_params<double>{ firBandpassDesign.taps }
        };
        return applyFirPreFilter(rawData, temporaryState);
    }

    const PreFilterDesign bandpassDesign =
        makeBandpassFilterDesign(sampleRate, options.bandpassEnabled);
    if (!bandpassDesign.hasSections()) {
        return rawData;
    }

    if (filterState != nullptr && hasBandpassFilterState) {
        return applyIirPreFilter(rawData, filterState->bandpassState);
    }

    kfr::iir_state<double, 5> temporaryState{ bandpassDesign.params() };
    return applyIirPreFilter(rawData, temporaryState);
}

QVector<float> applyScientificFilterFrame(
    const QVector<float>& bandpassData,
    int sampleRate,
    const SignalPreprocessOptions& options,
    SignalPreFilterState* filterState,
    bool hasScientificFilterState)
{
    if (bandpassData.isEmpty() || !options.scientificFilter.enabled) {
        return bandpassData;
    }

    if (filterState != nullptr && hasScientificFilterState) {
        return applyScientificIirPreFilter(
            bandpassData,
            filterState->scientificFilterState);
    }

    const ScientificFilterDesign scientificFilterDesign =
        makeScientificFilterDesign(sampleRate, options.scientificFilter, true);
    if (!scientificFilterDesign.hasSections()) {
        return bandpassData;
    }

    kfr::iir_state<double, ScientificFilterSectionCapacity> temporaryState{
        scientificFilterFixedParams(scientificFilterDesign)
    };
    return applyScientificIirPreFilter(bandpassData, temporaryState);
}

int adaptiveNotchHistorySampleCount(int sampleRate)
{
    return std::max(
        1,
        static_cast<int>(std::round(static_cast<double>(sampleRate) * kAdaptiveNotchWindowSeconds)));
}

void appendAdaptiveNotchHistory(
    SignalPreFilterState& state,
    const QVector<float>& frameData,
    int sampleRate)
{
    const int maximumHistorySize = adaptiveNotchHistorySampleCount(sampleRate);
    state.adaptiveNotchHistory.reserve(maximumHistorySize + frameData.size());
    for (float sample : frameData) {
        state.adaptiveNotchHistory.append(sample);
    }

    const int excessSampleCount = state.adaptiveNotchHistory.size() - maximumHistorySize;
    if (excessSampleCount > 0) {
        state.adaptiveNotchHistory.erase(
            state.adaptiveNotchHistory.begin(),
            state.adaptiveNotchHistory.begin() + excessSampleCount);
    }
}

QVector<double> buildAdaptiveNotchDetectionWindow(
    const QVector<float>& history,
    int sampleRate,
    double& detectionSampleRate)
{
    const int decimationStride = std::max(
        1,
        static_cast<int>(
            std::ceil(static_cast<double>(sampleRate) /
                      static_cast<double>(kAdaptiveNotchMaxDetectionSampleRate))));
    detectionSampleRate = static_cast<double>(sampleRate) /
        static_cast<double>(decimationStride);

    QVector<double> decimatedSamples;
    decimatedSamples.reserve((history.size() + decimationStride - 1) / decimationStride);
    double mean = 0.0;
    int finiteSampleCount = 0;
    for (int index = 0; index < history.size(); index += decimationStride) {
        const double sample = std::isfinite(history[index])
            ? static_cast<double>(history[index])
            : 0.0;
        decimatedSamples.append(sample);
        mean += sample;
        ++finiteSampleCount;
    }

    if (finiteSampleCount > 0) {
        mean /= static_cast<double>(finiteSampleCount);
    }

    const int sampleCount = decimatedSamples.size();
    for (int index = 0; index < sampleCount; ++index) {
        const double window = sampleCount > 1
            ? 0.5 * (1.0 - std::cos((2.0 * kPi * static_cast<double>(index)) /
                                    static_cast<double>(sampleCount - 1)))
            : 1.0;
        decimatedSamples[index] = (decimatedSamples[index] - mean) * window;
    }

    return decimatedSamples;
}

double goertzelMagnitude(
    const QVector<double>& windowedSamples,
    double sampleRate,
    double frequencyHz)
{
    if (windowedSamples.isEmpty() ||
        sampleRate <= 0.0 ||
        frequencyHz <= 0.0 ||
        frequencyHz >= sampleRate / 2.0) {
        return 0.0;
    }

    const double omega = 2.0 * kPi * frequencyHz / sampleRate;
    const double coefficient = 2.0 * std::cos(omega);
    double q0 = 0.0;
    double q1 = 0.0;
    double q2 = 0.0;
    for (double sample : windowedSamples) {
        q0 = coefficient * q1 - q2 + sample;
        q2 = q1;
        q1 = q0;
    }

    const double real = q1 - q2 * std::cos(omega);
    const double imag = q2 * std::sin(omega);
    return std::hypot(real, imag);
}

struct AdaptiveNotchPeak
{
    bool valid = false;
    double frequencyHz = 0.0;
};

AdaptiveNotchPeak findAdaptiveNotchPeak(
    const QVector<double>& windowedSamples,
    double sampleRate,
    double nominalFrequencyHz)
{
    AdaptiveNotchPeak peak{};
    double bestMagnitude = 0.0;
    for (double frequencyHz = nominalFrequencyHz - kAdaptiveNotchSearchHalfWidthHz;
         frequencyHz <= nominalFrequencyHz + kAdaptiveNotchSearchHalfWidthHz + 0.0001;
         frequencyHz += kAdaptiveNotchSearchStepHz) {
        if (frequencyHz <= 0.0 || frequencyHz >= sampleRate / 2.0) {
            continue;
        }

        const double magnitude = goertzelMagnitude(windowedSamples, sampleRate, frequencyHz);
        if (magnitude > bestMagnitude) {
            bestMagnitude = magnitude;
            peak.frequencyHz = frequencyHz;
        }
    }

    double guardMagnitudeSum = 0.0;
    int guardMagnitudeCount = 0;
    for (double offsetHz : kAdaptiveNotchGuardOffsetsHz) {
        const double guardFrequencyHz = nominalFrequencyHz + offsetHz;
        if (guardFrequencyHz <= 0.0 || guardFrequencyHz >= sampleRate / 2.0) {
            continue;
        }

        guardMagnitudeSum += goertzelMagnitude(windowedSamples, sampleRate, guardFrequencyHz);
        ++guardMagnitudeCount;
    }

    if (guardMagnitudeCount <= 0) {
        return peak;
    }

    const double guardMagnitude =
        guardMagnitudeSum / static_cast<double>(guardMagnitudeCount);
    peak.valid =
        bestMagnitude > kMinimumAdaptiveNotchMagnitude &&
        bestMagnitude >= std::max(guardMagnitude, kMinimumAdaptiveNotchMagnitude) *
            kAdaptiveNotchGuardRatio;
    return peak;
}

double moveTrackedFrequency(
    double currentFrequencyHz,
    double targetFrequencyHz,
    double nominalFrequencyHz,
    double alpha)
{
    if (!std::isfinite(currentFrequencyHz)) {
        currentFrequencyHz = nominalFrequencyHz;
    }

    const double proposedFrequencyHz =
        currentFrequencyHz + alpha * (targetFrequencyHz - currentFrequencyHz);
    const double deltaHz = std::clamp(
        proposedFrequencyHz - currentFrequencyHz,
        -kAdaptiveNotchMaxStepHz,
        kAdaptiveNotchMaxStepHz);
    return std::clamp(
        currentFrequencyHz + deltaHz,
        nominalFrequencyHz - kAdaptiveNotchSearchHalfWidthHz,
        nominalFrequencyHz + kAdaptiveNotchSearchHalfWidthHz);
}

void updateAdaptiveNotchFrequencies(
    SignalPreFilterState& state,
    const QVector<float>& preNotchData,
    int sampleRate)
{
    appendAdaptiveNotchHistory(state, preNotchData, sampleRate);
    if (state.adaptiveNotchHistory.size() < adaptiveNotchHistorySampleCount(sampleRate)) {
        return;
    }

    double detectionSampleRate = static_cast<double>(sampleRate);
    const QVector<double> windowedSamples =
        buildAdaptiveNotchDetectionWindow(
            state.adaptiveNotchHistory,
            sampleRate,
            detectionSampleRate);
    for (int index = 0; index < kPowerlineNotchCount; ++index) {
        const double nominalFrequencyHz = kPowerlineFrequenciesHz[static_cast<size_t>(index)];
        if (!isFrequencyWithinNyquist(
                nominalFrequencyHz + kAdaptiveNotchSearchHalfWidthHz,
                sampleRate)) {
            state.adaptiveNotchFrequencies[static_cast<size_t>(index)] = nominalFrequencyHz;
            state.adaptiveNotchMissedFrames[static_cast<size_t>(index)] = 0;
            continue;
        }

        const AdaptiveNotchPeak peak =
            findAdaptiveNotchPeak(windowedSamples, detectionSampleRate, nominalFrequencyHz);
        if (peak.valid) {
            state.adaptiveNotchMissedFrames[static_cast<size_t>(index)] = 0;
            state.adaptiveNotchFrequencies[static_cast<size_t>(index)] =
                moveTrackedFrequency(
                    state.adaptiveNotchFrequencies[static_cast<size_t>(index)],
                    peak.frequencyHz,
                    nominalFrequencyHz,
                    kAdaptiveNotchTrackingAlpha);
            continue;
        }

        ++state.adaptiveNotchMissedFrames[static_cast<size_t>(index)];
        if (state.adaptiveNotchMissedFrames[static_cast<size_t>(index)] >=
            kAdaptiveNotchReturnAfterMissedFrames) {
            state.adaptiveNotchFrequencies[static_cast<size_t>(index)] =
                moveTrackedFrequency(
                    state.adaptiveNotchFrequencies[static_cast<size_t>(index)],
                    nominalFrequencyHz,
                    nominalFrequencyHz,
                    kAdaptiveNotchReturnAlpha);
        }
    }
}

QVector<float> applyNotchFilterFrame(
    const QVector<float>& preNotchData,
    int sampleRate,
    const SignalPreprocessOptions& options,
    SignalPreFilterState* filterState,
    bool hasNotchFilterState)
{
    if (preNotchData.isEmpty() || !options.notchEnabled) {
        return preNotchData;
    }

    const bool adaptiveMode =
        normalizeNotchFrequencyMode(options.notchFrequencyMode) == NotchFrequencyAdaptive;
    if (adaptiveMode) {
        if (filterState == nullptr) {
            const PreFilterDesign notchDesign =
                makeNotchFilterDesign(sampleRate, true, fixedPowerlineFrequencies());
            return applyPreFilterDesign(preNotchData, notchDesign);
        }

        updateAdaptiveNotchFrequencies(*filterState, preNotchData, sampleRate);
        const PreFilterDesign notchDesign =
            makeNotchFilterDesign(sampleRate, true, filterState->adaptiveNotchFrequencies);
        if (!notchDesign.hasSections()) {
            return preNotchData;
        }

        filterState->notchState.params = notchDesign.params();
        return applyIirPreFilter(preNotchData, filterState->notchState);
    }

    const PreFilterDesign notchDesign =
        makeNotchFilterDesign(sampleRate, true, fixedPowerlineFrequencies());
    if (!notchDesign.hasSections()) {
        return preNotchData;
    }

    if (filterState != nullptr && hasNotchFilterState) {
        return applyIirPreFilter(preNotchData, filterState->notchState);
    }

    kfr::iir_state<double, 5> temporaryState{ notchDesign.params() };
    return applyIirPreFilter(preNotchData, temporaryState);
}

QVector<float> applyStreamingPreFilter(
    const QVector<float>& rawData,
    int sampleRate,
    const SignalPreprocessOptions& options,
    SignalPreFilterState* filterState,
    bool hasBandpassFilterState,
    bool hasScientificFilterState,
    bool hasNotchFilterState)
{
    if (rawData.isEmpty()) {
        return {};
    }

    const QVector<float> bandpassData = applyBandpassFilterFrame(
        rawData,
        sampleRate,
        options,
        filterState,
        hasBandpassFilterState);
    const QVector<float> scientificFilterData = applyScientificFilterFrame(
        bandpassData,
        sampleRate,
        options,
        filterState,
        hasScientificFilterState);
    return applyNotchFilterFrame(
        scientificFilterData,
        sampleRate,
        options,
        filterState,
        hasNotchFilterState);
}

double elapsedMilliseconds(const QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
}

void appendFrame(QVector<float>& destination, const QVector<float>& frame)
{
    for (float sample : frame) {
        destination.append(sample);
    }
}

QVector<float> applyImportPreFilterPipeline(
    const QVector<float>& rawData,
    int sampleRate,
    const SignalPreprocessOptions& options,
    QVariantMap& timingSummary)
{
    if (rawData.isEmpty()) {
        return {};
    }

    SignalPreFilterState importFilterState;
    bool hasBandpassFilterState = false;
    bool hasScientificFilterState = false;
    bool hasNotchFilterState = false;
    initializePreFilterState(
        importFilterState,
        sampleRate,
        options,
        &hasBandpassFilterState,
        &hasScientificFilterState,
        &hasNotchFilterState);
    if (options.bandpassEnabled && options.firFilterEnabled && hasBandpassFilterState) {
        timingSummary.insert(
            QStringLiteral("bandpassFirOrder"),
            importFilterState.bandpassFirOrder);
    }

    QVector<float> preprocessedData;
    preprocessedData.reserve(rawData.size());
    const qsizetype frameSampleCount = std::max<qsizetype>(
        1,
        static_cast<qsizetype>(
            std::round(static_cast<double>(sampleRate) * kAdaptiveNotchFrameSeconds)));

    double bandpassMs = 0.0;
    double scientificFilterMs = 0.0;
    double notchMs = 0.0;
    for (qsizetype startIndex = 0; startIndex < rawData.size(); startIndex += frameSampleCount) {
        const qsizetype currentFrameSampleCount =
            std::min(frameSampleCount, rawData.size() - startIndex);
        QVector<float> frameData = rawData.mid(startIndex, currentFrameSampleCount);

        if (options.bandpassEnabled) {
            QElapsedTimer bandpassTimer;
            bandpassTimer.start();
            frameData = applyBandpassFilterFrame(
                frameData,
                sampleRate,
                options,
                &importFilterState,
                hasBandpassFilterState);
            bandpassMs += elapsedMilliseconds(bandpassTimer);
        }

        if (options.scientificFilter.enabled) {
            QElapsedTimer scientificFilterTimer;
            scientificFilterTimer.start();
            frameData = applyScientificFilterFrame(
                frameData,
                sampleRate,
                options,
                &importFilterState,
                hasScientificFilterState);
            scientificFilterMs += elapsedMilliseconds(scientificFilterTimer);
        }

        if (options.notchEnabled) {
            QElapsedTimer notchTimer;
            notchTimer.start();
            frameData = applyNotchFilterFrame(
                frameData,
                sampleRate,
                options,
                &importFilterState,
                hasNotchFilterState);
            notchMs += elapsedMilliseconds(notchTimer);
        }

        appendFrame(preprocessedData, frameData);
    }

    if (options.bandpassEnabled) {
        timingSummary.insert(QStringLiteral("bandpassMs"), bandpassMs);
    }
    if (options.scientificFilter.enabled) {
        timingSummary.insert(QStringLiteral("scientificFilterMs"), scientificFilterMs);
    }
    if (options.notchEnabled) {
        timingSummary.insert(QStringLiteral("notchMs"), notchMs);
    }

    return preprocessedData;
}

double normalizeRealtimeGain(double gain)
{
    if (!std::isfinite(gain)) {
        return 1.0;
    }

    return std::clamp(gain, kMinimumRealtimeGain, kMaximumRealtimeGain);
}

QVector<float> applyRealtimeGain(const QVector<float>& source, double gain)
{
    if (source.isEmpty() || std::abs(gain - 1.0) <= kRealtimeGainEpsilon) {
        return source;
    }

    QVector<float> adjustedData(source.size());
    std::transform(
        source.cbegin(),
        source.cend(),
        adjustedData.begin(),
        [gain](float value) {
            return static_cast<float>(static_cast<double>(value) * gain);
        });
    return adjustedData;
}

double rmsChangeDb(double inputRms, double outputRms)
{
    if (inputRms <= kMinimumRmsForDb || !std::isfinite(inputRms)) {
        return 0.0;
    }

    if (!std::isfinite(outputRms)) {
        return 0.0;
    }

    const double safeOutputRms =
        std::max(outputRms, kMinimumRmsForDb);
    return 20.0 * std::log10(safeOutputRms / inputRms);
}

void logAncDebugMetrics(
    int channel,
    int sampleCount,
    int referenceSampleCount,
    const ActiveNoiseCancellation::Metrics& metrics)
{
    if (!ActiveNoiseCancellation::debugLoggingEnabled() ||
        channel < 0 ||
        channel >= kRealtimeChannelCount) {
        return;
    }

    static std::array<int, kRealtimeChannelCount> frameCounters{};
    int& frameCounter = frameCounters[static_cast<size_t>(channel)];
    ++frameCounter;
    if (frameCounter % kAncDebugLogEveryFrames != 0) {
        return;
    }

    qDebug() << "ANC debug" // ANC实时指标日志前缀。
             << "ch" << channel // 目标信号通道编号，范围为0-6。
             << "samples" << sampleCount // ANC处理后的目标帧采样点数。
             << "refSamples" << referenceSampleCount // 参考噪声帧采样点数。
             << "valid" << metrics.referenceValid // 参考通道是否满足ANC使用条件。
             << "bypassed" << metrics.bypassed // ANC是否旁路，即直接返回原目标信号。
             << "frozen" << metrics.adaptationFrozen // 自适应滤波器权重是否暂停更新。
             << "cancelActive" << metrics.cancellationActive
             << "cancelGain" << metrics.cancellationGain
             << "highCorrFrames" << metrics.highCorrelationFrameCount
             << "corr" << metrics.correlation // 目标信号与参考噪声的最佳归一化相关系数。
             << "delaySamples" << metrics.selectedDelaySamples // 估计出的参考噪声延迟，单位为采样点。
             << "step" << metrics.stepSize // 当前NLMS自适应更新步长。
             << "targetRms" << metrics.targetRms // ANC处理前目标信号的RMS。
             << "refRms" << metrics.referenceRms // 原始参考噪声信号的RMS。
             << "outputRms" << metrics.outputRms // ANC处理后输出信号的RMS。
             << "rmsDeltaDb" << rmsChangeDb(metrics.targetRms, metrics.outputRms); // 输出RMS相对输入RMS的变化，单位为dB。
}

QVector<float> runRealtimePreprocessPipeline(
    int channel,
    const QVector<float>& rawData,
    int sampleRate,
    const SignalPreprocessOptions& options,
    const QVector<float>* referenceNoise = nullptr,
    ActiveNoiseCancellation::StreamingState* ancStreamingState = nullptr,
    SignalPreFilterState* preFilterState = nullptr,
    bool hasBandpassFilterState = false,
    bool hasScientificFilterState = false,
    bool hasNotchFilterState = false,
    AdaptiveNoiseReduction::StreamingState* adaptiveStreamingState = nullptr)
{
    if (rawData.isEmpty()) {
        return {};
    }

    const int effectiveSampleRate = resolveSampleRate(sampleRate);
    QVector<float> processedData = applyStreamingPreFilter(
        rawData,
        effectiveSampleRate,
        options,
        preFilterState,
        hasBandpassFilterState,
        hasScientificFilterState,
        hasNotchFilterState);

    if (options.activeNoiseCancellationEnabled &&
        referenceNoise != nullptr &&
        ancStreamingState != nullptr) {
        const ActiveNoiseCancellation::Parameters ancParameters =
            ActiveNoiseCancellation::makeParameters(effectiveSampleRate);
        ActiveNoiseCancellation::Metrics metrics;
        processedData = ActiveNoiseCancellation::cancel(
            processedData,
            *referenceNoise,
            effectiveSampleRate,
            *ancStreamingState,
            ancParameters,
            &metrics);
        logAncDebugMetrics(
            channel,
            processedData.size(),
            referenceNoise->size(),
            metrics);
    }

    if (options.adaptiveNoiseReductionEnabled) {
        if (adaptiveStreamingState != nullptr) {
            processedData = AdaptiveNoiseReduction::denoiseStreaming(
                processedData,
                effectiveSampleRate,
                *adaptiveStreamingState,
                options.adaptiveNoiseReductionParameters);
        } else {
            processedData = AdaptiveNoiseReduction::denoise(
                processedData,
                effectiveSampleRate,
                options.adaptiveNoiseReductionParameters);
        }
    }

    return applyRealtimeGain(processedData, options.gain);
}

int resolveEffectiveSelectedChannel(int selectedChannel)
{
    if (DaqDeviceManager::instance()->isChannelActive(selectedChannel)) {
        return selectedChannel;
    }

    for (int channel = 0; channel < kRealtimeChannelCount; ++channel) {
        if (DaqDeviceManager::instance()->isChannelActive(channel)) {
            return channel;
        }
    }

    return -1;
}
} // namespace

SignalPreprocessing* SignalPreprocessing::m_instance = nullptr;

SignalPreprocessing::SignalPreprocessing(QObject* parent)
    : QObject(parent)
{
    m_thread = new QThread(this);
    m_worker = new PreprocessingWorker();
    m_worker->moveToThread(m_thread);

    connect(
        this,
        &SignalPreprocessing::channelChanged,
        m_worker,
        &PreprocessingWorker::updateChannel);
    connect(
        m_worker,
        &PreprocessingWorker::resultReady,
        this,
        &SignalPreprocessing::updateDataCache);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();
}

SignalPreprocessing::~SignalPreprocessing()
{
    if (m_thread != nullptr && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait();
    }
    if (m_timer != nullptr) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
}

void SignalPreprocessing::setCurrentChannel(int channel)
{
    if (m_currentChannel == channel) {
        return;
    }

    m_currentChannel = channel;
    qDebug() << "Current preprocessing channel:" << m_currentChannel;
    emit channelChanged(m_currentChannel);
}

bool SignalPreprocessing::importAllProcessingEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importBandpassEnabled &&
        m_importNotchEnabled &&
        m_importAdaptiveNoiseReductionEnabled &&
        m_importWaveletDenoisingEnabled &&
        m_importTransientNoiseSuppressionEnabled &&
        m_importMotionArtifactReductionEnabled;
}

bool SignalPreprocessing::importBandpassEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importBandpassEnabled;
}

bool SignalPreprocessing::importNotchEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importNotchEnabled;
}

int SignalPreprocessing::importNotchFrequencyMode() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importNotchFrequencyMode;
}

bool SignalPreprocessing::importAdaptiveNoiseReductionEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importAdaptiveNoiseReductionEnabled;
}

int SignalPreprocessing::importAdaptiveNoiseReductionLevel() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importAdaptiveNoiseReductionParameters.noiseSuppressionLevel;
}

bool SignalPreprocessing::importAdaptiveNoiseReductionHighPassFilterEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importAdaptiveNoiseReductionParameters.highPassFilterEnabled;
}

bool SignalPreprocessing::importAdaptiveNoiseReductionAutomaticGainControlEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importAdaptiveNoiseReductionParameters.automaticGainControlEnabled;
}

bool SignalPreprocessing::importAdaptiveNoiseReductionTransientSuppressionEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importAdaptiveNoiseReductionParameters.transientSuppressionEnabled;
}

bool SignalPreprocessing::importWaveletDenoisingEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importWaveletDenoisingEnabled;
}

bool SignalPreprocessing::importTransientNoiseSuppressionEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importTransientNoiseSuppressionEnabled;
}

bool SignalPreprocessing::importMotionArtifactReductionEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importMotionArtifactReductionEnabled;
}

bool SignalPreprocessing::importFirFilterEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importFirFilterEnabled;
}

bool SignalPreprocessing::importScientificFilterEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importScientificFilterConfig.enabled;
}

int SignalPreprocessing::importScientificFilterPrototype() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importScientificFilterConfig.prototype;
}

int SignalPreprocessing::importScientificFilterType() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importScientificFilterConfig.filterType;
}

int SignalPreprocessing::importScientificFilterOrder() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importScientificFilterConfig.order;
}

double SignalPreprocessing::importScientificFilterCutoffFrequencyHz() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importScientificFilterConfig.cutoffFrequencyHz;
}

double SignalPreprocessing::importScientificFilterLowCutoffFrequencyHz() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importScientificFilterConfig.lowCutoffFrequencyHz;
}

double SignalPreprocessing::importScientificFilterHighCutoffFrequencyHz() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importScientificFilterConfig.highCutoffFrequencyHz;
}

double SignalPreprocessing::importScientificFilterTransitionBandwidthHz() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importScientificFilterConfig.transitionBandwidthHz;
}

double SignalPreprocessing::importScientificFilterStopbandAttenuationDb() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importScientificFilterConfig.stopbandAttenuationDb;
}

double SignalPreprocessing::importScientificFilterPassbandRippleDb() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importScientificFilterConfig.passbandRippleDb;
}

bool SignalPreprocessing::realtimeAllProcessingEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeBandpassEnabled &&
        m_realtimeNotchEnabled &&
        m_realtimeActiveNoiseCancellationEnabled &&
        m_realtimeAdaptiveNoiseReductionEnabled &&
        m_realtimeWaveletDenoisingEnabled &&
        m_realtimeTransientNoiseSuppressionEnabled &&
        m_realtimeMotionArtifactReductionEnabled;
}

bool SignalPreprocessing::realtimeBandpassEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeBandpassEnabled;
}

bool SignalPreprocessing::realtimeNotchEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeNotchEnabled;
}

int SignalPreprocessing::realtimeNotchFrequencyMode() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeNotchFrequencyMode;
}

bool SignalPreprocessing::realtimeActiveNoiseCancellationEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeActiveNoiseCancellationEnabled;
}

bool SignalPreprocessing::realtimeAdaptiveNoiseReductionEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeAdaptiveNoiseReductionEnabled;
}

int SignalPreprocessing::realtimeAdaptiveNoiseReductionLevel() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeAdaptiveNoiseReductionParameters.noiseSuppressionLevel;
}

bool SignalPreprocessing::realtimeAdaptiveNoiseReductionHighPassFilterEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeAdaptiveNoiseReductionParameters.highPassFilterEnabled;
}

bool SignalPreprocessing::realtimeAdaptiveNoiseReductionAutomaticGainControlEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeAdaptiveNoiseReductionParameters.automaticGainControlEnabled;
}

bool SignalPreprocessing::realtimeAdaptiveNoiseReductionTransientSuppressionEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeAdaptiveNoiseReductionParameters.transientSuppressionEnabled;
}

bool SignalPreprocessing::realtimeWaveletDenoisingEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeWaveletDenoisingEnabled;
}

bool SignalPreprocessing::realtimeTransientNoiseSuppressionEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeTransientNoiseSuppressionEnabled;
}

bool SignalPreprocessing::realtimeMotionArtifactReductionEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeMotionArtifactReductionEnabled;
}

bool SignalPreprocessing::realtimeFirFilterEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeFirFilterEnabled;
}

bool SignalPreprocessing::realtimeScientificFilterEnabled() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeScientificFilterConfig.enabled;
}

int SignalPreprocessing::realtimeScientificFilterPrototype() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeScientificFilterConfig.prototype;
}

int SignalPreprocessing::realtimeScientificFilterType() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeScientificFilterConfig.filterType;
}

int SignalPreprocessing::realtimeScientificFilterOrder() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeScientificFilterConfig.order;
}

double SignalPreprocessing::realtimeScientificFilterCutoffFrequencyHz() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeScientificFilterConfig.cutoffFrequencyHz;
}

double SignalPreprocessing::realtimeScientificFilterLowCutoffFrequencyHz() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeScientificFilterConfig.lowCutoffFrequencyHz;
}

double SignalPreprocessing::realtimeScientificFilterHighCutoffFrequencyHz() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeScientificFilterConfig.highCutoffFrequencyHz;
}

double SignalPreprocessing::realtimeScientificFilterTransitionBandwidthHz() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeScientificFilterConfig.transitionBandwidthHz;
}

double SignalPreprocessing::realtimeScientificFilterStopbandAttenuationDb() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeScientificFilterConfig.stopbandAttenuationDb;
}

double SignalPreprocessing::realtimeScientificFilterPassbandRippleDb() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeScientificFilterConfig.passbandRippleDb;
}

double SignalPreprocessing::realtimeGain() const
{
    QMutexLocker locker(&m_realtimeSettingsMutex);
    return m_realtimeGain;
}

void SignalPreprocessing::setAllImportProcessingEnabled(bool enabled)
{
    bool bandpassChanged = false;
    bool notchChanged = false;
    bool adaptiveNoiseReductionChanged = false;
    bool waveletChanged = false;
    bool transientSuppressionChanged = false;
    bool motionArtifactChanged = false;
    {
        QMutexLocker locker(&m_importSettingsMutex);
        bandpassChanged = m_importBandpassEnabled != enabled;
        notchChanged = m_importNotchEnabled != enabled;
        adaptiveNoiseReductionChanged = m_importAdaptiveNoiseReductionEnabled != enabled;
        waveletChanged = m_importWaveletDenoisingEnabled != enabled;
        transientSuppressionChanged = m_importTransientNoiseSuppressionEnabled != enabled;
        motionArtifactChanged = m_importMotionArtifactReductionEnabled != enabled;
        if (!bandpassChanged &&
            !notchChanged &&
            !adaptiveNoiseReductionChanged &&
            !waveletChanged &&
            !transientSuppressionChanged &&
            !motionArtifactChanged) {
            return;
        }

        m_importBandpassEnabled = enabled;
        m_importNotchEnabled = enabled;
        m_importAdaptiveNoiseReductionEnabled = enabled;
        m_importWaveletDenoisingEnabled = enabled;
        m_importTransientNoiseSuppressionEnabled = enabled;
        m_importMotionArtifactReductionEnabled = enabled;
    }

    if (bandpassChanged) {
        emit importBandpassEnabledChanged();
    }
    if (notchChanged) {
        emit importNotchEnabledChanged();
    }
    if (adaptiveNoiseReductionChanged) {
        emit importAdaptiveNoiseReductionEnabledChanged();
    }
    if (waveletChanged) {
        emit importWaveletDenoisingEnabledChanged();
    }
    if (transientSuppressionChanged) {
        emit importTransientNoiseSuppressionEnabledChanged();
    }
    if (motionArtifactChanged) {
        emit importMotionArtifactReductionEnabledChanged();
    }
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportBandpassEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_importSettingsMutex);
        if (m_importBandpassEnabled == enabled) {
            return;
        }
        m_importBandpassEnabled = enabled;
    }

    emit importBandpassEnabledChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportNotchEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_importSettingsMutex);
        if (m_importNotchEnabled == enabled) {
            return;
        }
        m_importNotchEnabled = enabled;
    }

    emit importNotchEnabledChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportNotchFrequencyMode(int mode)
{
    const int normalizedMode = normalizeNotchFrequencyMode(mode);
    {
        QMutexLocker locker(&m_importSettingsMutex);
        if (m_importNotchFrequencyMode == normalizedMode) {
            return;
        }
        m_importNotchFrequencyMode = normalizedMode;
    }

    emit importNotchFrequencyModeChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportAdaptiveNoiseReductionEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_importSettingsMutex);
        if (m_importAdaptiveNoiseReductionEnabled == enabled) {
            return;
        }
        m_importAdaptiveNoiseReductionEnabled = enabled;
    }

    emit importAdaptiveNoiseReductionEnabledChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportAdaptiveNoiseReductionLevel(int level)
{
    const int normalizedLevel = normalizeWebRtcNoiseSuppressionLevel(level);
    {
        QMutexLocker locker(&m_importSettingsMutex);
        if (m_importAdaptiveNoiseReductionParameters.noiseSuppressionLevel ==
            normalizedLevel) {
            return;
        }
        m_importAdaptiveNoiseReductionParameters.noiseSuppressionLevel =
            normalizedLevel;
    }

    emit importAdaptiveNoiseReductionParametersChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportAdaptiveNoiseReductionHighPassFilterEnabled(
    bool enabled)
{
    {
        QMutexLocker locker(&m_importSettingsMutex);
        if (m_importAdaptiveNoiseReductionParameters.highPassFilterEnabled ==
            enabled) {
            return;
        }
        m_importAdaptiveNoiseReductionParameters.highPassFilterEnabled = enabled;
    }

    emit importAdaptiveNoiseReductionParametersChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportAdaptiveNoiseReductionAutomaticGainControlEnabled(
    bool enabled)
{
    {
        QMutexLocker locker(&m_importSettingsMutex);
        if (m_importAdaptiveNoiseReductionParameters
                .automaticGainControlEnabled == enabled) {
            return;
        }
        m_importAdaptiveNoiseReductionParameters.automaticGainControlEnabled =
            enabled;
    }

    emit importAdaptiveNoiseReductionParametersChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportAdaptiveNoiseReductionTransientSuppressionEnabled(
    bool enabled)
{
    {
        QMutexLocker locker(&m_importSettingsMutex);
        if (m_importAdaptiveNoiseReductionParameters
                .transientSuppressionEnabled == enabled) {
            return;
        }
        m_importAdaptiveNoiseReductionParameters.transientSuppressionEnabled =
            enabled;
    }

    emit importAdaptiveNoiseReductionParametersChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportWaveletDenoisingEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_importSettingsMutex);
        if (m_importWaveletDenoisingEnabled == enabled) {
            return;
        }
        m_importWaveletDenoisingEnabled = enabled;
    }

    emit importWaveletDenoisingEnabledChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportTransientNoiseSuppressionEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_importSettingsMutex);
        if (m_importTransientNoiseSuppressionEnabled == enabled) {
            return;
        }
        m_importTransientNoiseSuppressionEnabled = enabled;
    }

    emit importTransientNoiseSuppressionEnabledChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportMotionArtifactReductionEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_importSettingsMutex);
        if (m_importMotionArtifactReductionEnabled == enabled) {
            return;
        }
        m_importMotionArtifactReductionEnabled = enabled;
    }

    emit importMotionArtifactReductionEnabledChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportFirFilterEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_importSettingsMutex);
        if (m_importFirFilterEnabled == enabled) {
            return;
        }
        m_importFirFilterEnabled = enabled;
    }

    emit importFirFilterEnabledChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportScientificFilterEnabled(bool enabled)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_importSettingsMutex);
        changed = assignBool(m_importScientificFilterConfig.enabled, enabled);
    }
    if (!changed) {
        return;
    }
    emit importScientificFilterChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportScientificFilterPrototype(int prototype)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_importSettingsMutex);
        changed = assignInt(
            m_importScientificFilterConfig.prototype,
            normalizeScientificFilterPrototype(prototype));
    }
    if (!changed) {
        return;
    }
    emit importScientificFilterChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportScientificFilterType(int filterType)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_importSettingsMutex);
        changed = assignInt(
            m_importScientificFilterConfig.filterType,
            normalizeScientificFilterType(filterType));
    }
    if (!changed) {
        return;
    }
    emit importScientificFilterChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportScientificFilterOrder(int order)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_importSettingsMutex);
        changed = assignInt(
            m_importScientificFilterConfig.order,
            normalizeScientificFilterOrder(order));
    }
    if (!changed) {
        return;
    }
    emit importScientificFilterChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportScientificFilterCutoffFrequencyHz(double frequencyHz)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_importSettingsMutex);
        changed = assignDouble(
            m_importScientificFilterConfig.cutoffFrequencyHz,
            normalizeScientificFilterFrequency(frequencyHz, ScientificFilterConfig{}.cutoffFrequencyHz));
    }
    if (!changed) {
        return;
    }
    emit importScientificFilterChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportScientificFilterLowCutoffFrequencyHz(double frequencyHz)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_importSettingsMutex);
        changed = assignDouble(
            m_importScientificFilterConfig.lowCutoffFrequencyHz,
            normalizeScientificFilterFrequency(frequencyHz, ScientificFilterConfig{}.lowCutoffFrequencyHz));
    }
    if (!changed) {
        return;
    }
    emit importScientificFilterChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportScientificFilterHighCutoffFrequencyHz(double frequencyHz)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_importSettingsMutex);
        changed = assignDouble(
            m_importScientificFilterConfig.highCutoffFrequencyHz,
            normalizeScientificFilterFrequency(frequencyHz, ScientificFilterConfig{}.highCutoffFrequencyHz));
    }
    if (!changed) {
        return;
    }
    emit importScientificFilterChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportScientificFilterTransitionBandwidthHz(double bandwidthHz)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_importSettingsMutex);
        changed = assignDouble(
            m_importScientificFilterConfig.transitionBandwidthHz,
            normalizeScientificFilterPositiveValue(
                bandwidthHz,
                ScientificFilterConfig{}.transitionBandwidthHz,
                kMinimumScientificFilterBandwidthHz));
    }
    if (!changed) {
        return;
    }
    emit importScientificFilterChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportScientificFilterStopbandAttenuationDb(double attenuationDb)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_importSettingsMutex);
        changed = assignDouble(
            m_importScientificFilterConfig.stopbandAttenuationDb,
            normalizeScientificFilterPositiveValue(
                attenuationDb,
                ScientificFilterConfig{}.stopbandAttenuationDb,
                kMinimumScientificFilterAttenuationDb));
    }
    if (!changed) {
        return;
    }
    emit importScientificFilterChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setImportScientificFilterPassbandRippleDb(double rippleDb)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_importSettingsMutex);
        changed = assignDouble(
            m_importScientificFilterConfig.passbandRippleDb,
            normalizeScientificFilterPositiveValue(
                rippleDb,
                ScientificFilterConfig{}.passbandRippleDb,
                kMinimumScientificFilterRippleDb));
    }
    if (!changed) {
        return;
    }
    emit importScientificFilterChanged();
    emit importProcessingSettingsChanged();
}

void SignalPreprocessing::setAllRealtimeProcessingEnabled(bool enabled)
{
    bool bandpassChanged = false;
    bool notchChanged = false;
    bool activeNoiseCancellationChanged = false;
    bool adaptiveNoiseReductionChanged = false;
    bool waveletChanged = false;
    bool transientSuppressionChanged = false;
    bool motionArtifactChanged = false;
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        bandpassChanged = m_realtimeBandpassEnabled != enabled;
        notchChanged = m_realtimeNotchEnabled != enabled;
        activeNoiseCancellationChanged = m_realtimeActiveNoiseCancellationEnabled != enabled;
        adaptiveNoiseReductionChanged = m_realtimeAdaptiveNoiseReductionEnabled != enabled;
        waveletChanged = m_realtimeWaveletDenoisingEnabled != enabled;
        transientSuppressionChanged = m_realtimeTransientNoiseSuppressionEnabled != enabled;
        motionArtifactChanged = m_realtimeMotionArtifactReductionEnabled != enabled;
        if (!bandpassChanged &&
            !notchChanged &&
            !activeNoiseCancellationChanged &&
            !adaptiveNoiseReductionChanged &&
            !waveletChanged &&
            !transientSuppressionChanged &&
            !motionArtifactChanged) {
            return;
        }

        m_realtimeBandpassEnabled = enabled;
        m_realtimeNotchEnabled = enabled;
        m_realtimeActiveNoiseCancellationEnabled = enabled;
        m_realtimeAdaptiveNoiseReductionEnabled = enabled;
        m_realtimeWaveletDenoisingEnabled = enabled;
        m_realtimeTransientNoiseSuppressionEnabled = enabled;
        m_realtimeMotionArtifactReductionEnabled = enabled;
    }

    if (bandpassChanged) {
        emit realtimeBandpassEnabledChanged();
    }
    if (notchChanged) {
        emit realtimeNotchEnabledChanged();
    }
    if (activeNoiseCancellationChanged) {
        emit realtimeActiveNoiseCancellationEnabledChanged();
    }
    if (adaptiveNoiseReductionChanged) {
        emit realtimeAdaptiveNoiseReductionEnabledChanged();
    }
    if (waveletChanged) {
        emit realtimeWaveletDenoisingEnabledChanged();
    }
    if (transientSuppressionChanged) {
        emit realtimeTransientNoiseSuppressionEnabledChanged();
    }
    if (motionArtifactChanged) {
        emit realtimeMotionArtifactReductionEnabledChanged();
    }
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeBandpassEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeBandpassEnabled == enabled) {
            return;
        }
        m_realtimeBandpassEnabled = enabled;
    }

    emit realtimeBandpassEnabledChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeNotchEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeNotchEnabled == enabled) {
            return;
        }
        m_realtimeNotchEnabled = enabled;
    }

    emit realtimeNotchEnabledChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeNotchFrequencyMode(int mode)
{
    const int normalizedMode = normalizeNotchFrequencyMode(mode);
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeNotchFrequencyMode == normalizedMode) {
            return;
        }
        m_realtimeNotchFrequencyMode = normalizedMode;
    }

    emit realtimeNotchFrequencyModeChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeActiveNoiseCancellationEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeActiveNoiseCancellationEnabled == enabled) {
            return;
        }
        m_realtimeActiveNoiseCancellationEnabled = enabled;
    }

    emit realtimeActiveNoiseCancellationEnabledChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeAdaptiveNoiseReductionEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeAdaptiveNoiseReductionEnabled == enabled) {
            return;
        }
        m_realtimeAdaptiveNoiseReductionEnabled = enabled;
    }

    emit realtimeAdaptiveNoiseReductionEnabledChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeAdaptiveNoiseReductionLevel(int level)
{
    const int normalizedLevel = normalizeWebRtcNoiseSuppressionLevel(level);
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeAdaptiveNoiseReductionParameters.noiseSuppressionLevel ==
            normalizedLevel) {
            return;
        }
        m_realtimeAdaptiveNoiseReductionParameters.noiseSuppressionLevel =
            normalizedLevel;
    }

    emit realtimeAdaptiveNoiseReductionParametersChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeAdaptiveNoiseReductionHighPassFilterEnabled(
    bool enabled)
{
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeAdaptiveNoiseReductionParameters.highPassFilterEnabled ==
            enabled) {
            return;
        }
        m_realtimeAdaptiveNoiseReductionParameters.highPassFilterEnabled = enabled;
    }

    emit realtimeAdaptiveNoiseReductionParametersChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeAdaptiveNoiseReductionAutomaticGainControlEnabled(
    bool enabled)
{
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeAdaptiveNoiseReductionParameters
                .automaticGainControlEnabled == enabled) {
            return;
        }
        m_realtimeAdaptiveNoiseReductionParameters.automaticGainControlEnabled =
            enabled;
    }

    emit realtimeAdaptiveNoiseReductionParametersChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeAdaptiveNoiseReductionTransientSuppressionEnabled(
    bool enabled)
{
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeAdaptiveNoiseReductionParameters
                .transientSuppressionEnabled == enabled) {
            return;
        }
        m_realtimeAdaptiveNoiseReductionParameters.transientSuppressionEnabled =
            enabled;
    }

    emit realtimeAdaptiveNoiseReductionParametersChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeWaveletDenoisingEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeWaveletDenoisingEnabled == enabled) {
            return;
        }
        m_realtimeWaveletDenoisingEnabled = enabled;
    }

    emit realtimeWaveletDenoisingEnabledChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeTransientNoiseSuppressionEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeTransientNoiseSuppressionEnabled == enabled) {
            return;
        }
        m_realtimeTransientNoiseSuppressionEnabled = enabled;
    }

    emit realtimeTransientNoiseSuppressionEnabledChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeMotionArtifactReductionEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeMotionArtifactReductionEnabled == enabled) {
            return;
        }
        m_realtimeMotionArtifactReductionEnabled = enabled;
    }

    emit realtimeMotionArtifactReductionEnabledChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeFirFilterEnabled(bool enabled)
{
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (m_realtimeFirFilterEnabled == enabled) {
            return;
        }
        m_realtimeFirFilterEnabled = enabled;
    }

    emit realtimeFirFilterEnabledChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeScientificFilterEnabled(bool enabled)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        changed = assignBool(m_realtimeScientificFilterConfig.enabled, enabled);
    }
    if (!changed) {
        return;
    }
    emit realtimeScientificFilterChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeScientificFilterPrototype(int prototype)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        changed = assignInt(
            m_realtimeScientificFilterConfig.prototype,
            normalizeScientificFilterPrototype(prototype));
    }
    if (!changed) {
        return;
    }
    emit realtimeScientificFilterChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeScientificFilterType(int filterType)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        changed = assignInt(
            m_realtimeScientificFilterConfig.filterType,
            normalizeScientificFilterType(filterType));
    }
    if (!changed) {
        return;
    }
    emit realtimeScientificFilterChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeScientificFilterOrder(int order)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        changed = assignInt(
            m_realtimeScientificFilterConfig.order,
            normalizeScientificFilterOrder(order));
    }
    if (!changed) {
        return;
    }
    emit realtimeScientificFilterChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeScientificFilterCutoffFrequencyHz(double frequencyHz)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        changed = assignDouble(
            m_realtimeScientificFilterConfig.cutoffFrequencyHz,
            normalizeScientificFilterFrequency(frequencyHz, ScientificFilterConfig{}.cutoffFrequencyHz));
    }
    if (!changed) {
        return;
    }
    emit realtimeScientificFilterChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeScientificFilterLowCutoffFrequencyHz(double frequencyHz)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        changed = assignDouble(
            m_realtimeScientificFilterConfig.lowCutoffFrequencyHz,
            normalizeScientificFilterFrequency(frequencyHz, ScientificFilterConfig{}.lowCutoffFrequencyHz));
    }
    if (!changed) {
        return;
    }
    emit realtimeScientificFilterChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeScientificFilterHighCutoffFrequencyHz(double frequencyHz)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        changed = assignDouble(
            m_realtimeScientificFilterConfig.highCutoffFrequencyHz,
            normalizeScientificFilterFrequency(frequencyHz, ScientificFilterConfig{}.highCutoffFrequencyHz));
    }
    if (!changed) {
        return;
    }
    emit realtimeScientificFilterChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeScientificFilterTransitionBandwidthHz(double bandwidthHz)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        changed = assignDouble(
            m_realtimeScientificFilterConfig.transitionBandwidthHz,
            normalizeScientificFilterPositiveValue(
                bandwidthHz,
                ScientificFilterConfig{}.transitionBandwidthHz,
                kMinimumScientificFilterBandwidthHz));
    }
    if (!changed) {
        return;
    }
    emit realtimeScientificFilterChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeScientificFilterStopbandAttenuationDb(double attenuationDb)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        changed = assignDouble(
            m_realtimeScientificFilterConfig.stopbandAttenuationDb,
            normalizeScientificFilterPositiveValue(
                attenuationDb,
                ScientificFilterConfig{}.stopbandAttenuationDb,
                kMinimumScientificFilterAttenuationDb));
    }
    if (!changed) {
        return;
    }
    emit realtimeScientificFilterChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeScientificFilterPassbandRippleDb(double rippleDb)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        changed = assignDouble(
            m_realtimeScientificFilterConfig.passbandRippleDb,
            normalizeScientificFilterPositiveValue(
                rippleDb,
                ScientificFilterConfig{}.passbandRippleDb,
                kMinimumScientificFilterRippleDb));
    }
    if (!changed) {
        return;
    }
    emit realtimeScientificFilterChanged();
    emit realtimeProcessingSettingsChanged();
}

void SignalPreprocessing::setRealtimeGain(double gain)
{
    const double normalizedGain = normalizeRealtimeGain(gain);
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        if (std::abs(m_realtimeGain - normalizedGain) <= kRealtimeGainEpsilon) {
            return;
        }
        m_realtimeGain = normalizedGain;
    }

    emit realtimeGainChanged();
}

QVariantMap SignalPreprocessing::scientificFilterResponse(
    const QVariantMap& config,
    int sampleRate) const
{
    const int effectiveSampleRate = resolveSampleRate(sampleRate);
    ScientificFilterConfig scientificConfig =
        scientificFilterConfigFromVariantMap(config);
    const ScientificFilterDesign design =
        makeScientificFilterDesign(effectiveSampleRate, scientificConfig, false);

    QVariantMap response;
    response.insert(QStringLiteral("frequencyResponse"), QVariantList{});
    response.insert(QStringLiteral("phase"), QVariantList{});
    response.insert(QStringLiteral("groupDelay"), QVariantList{});
    response.insert(QStringLiteral("actualRippleDb"), 0.0);
    response.insert(QStringLiteral("minimumFrequencyHz"), kPreviewMinimumScientificFilterFrequencyHz);
    response.insert(
        QStringLiteral("maximumFrequencyHz"),
        scientificFilterMaximumFrequencyHz(effectiveSampleRate));
    response.insert(QStringLiteral("error"), design.errorMessage);
    if (!design.errorMessage.isEmpty()) {
        return response;
    }
    if (!design.hasSections()) {
        response.insert(QStringLiteral("error"), QStringLiteral("无法生成滤波器响应"));
        return response;
    }

    const double sampleRateHz = static_cast<double>(effectiveSampleRate);
    const double maximumFrequencyHz = scientificFilterMaximumFrequencyHz(effectiveSampleRate);
    const int pointCount = std::max(2, kScientificFilterResponsePointCount);
    QVector<double> frequencies;
    QVector<double> magnitudeDbValues;
    QVector<double> phaseRadians;
    frequencies.reserve(pointCount);
    magnitudeDbValues.reserve(pointCount);
    phaseRadians.reserve(pointCount);

    QVariantList frequencyResponsePoints;
    frequencyResponsePoints.reserve(pointCount);
    QVariantList phasePoints;
    phasePoints.reserve(pointCount);
    QVariantList groupDelayPoints;
    groupDelayPoints.reserve(pointCount);

    double previousPhase = 0.0;
    bool hasPreviousPhase = false;
    double passbandMinimumDb = std::numeric_limits<double>::infinity();
    double passbandMaximumDb = -std::numeric_limits<double>::infinity();

    for (int index = 0; index < pointCount; ++index) {
        const double ratio = pointCount <= 1
            ? 0.0
            : static_cast<double>(index) / static_cast<double>(pointCount - 1);
        const double frequencyHz = kPreviewMinimumScientificFilterFrequencyHz +
            (maximumFrequencyHz - kPreviewMinimumScientificFilterFrequencyHz) * ratio;
        const std::complex<double> complexResponse =
            evaluateScientificFilterResponse(design.params, frequencyHz, sampleRateHz);
        const double magnitudeDb =
            20.0 * std::log10(std::max(std::abs(complexResponse), kMinimumRmsForDb));
        double phase = std::atan2(complexResponse.imag(), complexResponse.real());
        if (hasPreviousPhase) {
            while (phase - previousPhase > kPi) {
                phase -= 2.0 * kPi;
            }
            while (phase - previousPhase < -kPi) {
                phase += 2.0 * kPi;
            }
        }
        previousPhase = phase;
        hasPreviousPhase = true;

        frequencies.append(frequencyHz);
        magnitudeDbValues.append(magnitudeDb);
        phaseRadians.append(phase);
        frequencyResponsePoints.append(pointMap(frequencyHz, magnitudeDb));
        phasePoints.append(pointMap(frequencyHz, phase * 180.0 / kPi));

        if (isFrequencyInScientificPassband(
                frequencyHz,
                scientificConfig,
                maximumFrequencyHz)) {
            passbandMinimumDb = std::min(passbandMinimumDb, magnitudeDb);
            passbandMaximumDb = std::max(passbandMaximumDb, magnitudeDb);
        }
    }

    for (int index = 0; index < pointCount; ++index) {
        const int leftIndex = std::max(0, index - 1);
        const int rightIndex = std::min(pointCount - 1, index + 1);
        const double deltaFrequencyHz = frequencies[rightIndex] - frequencies[leftIndex];
        const double deltaOmega =
            2.0 * kPi * deltaFrequencyHz / sampleRateHz;
        const double groupDelaySamples = deltaOmega > 0.0
            ? -(phaseRadians[rightIndex] - phaseRadians[leftIndex]) / deltaOmega
            : 0.0;
        const double groupDelayMilliseconds =
            groupDelaySamples / sampleRateHz * 1000.0;
        groupDelayPoints.append(
            pointMap(frequencies[index], groupDelayMilliseconds));
    }

    const double actualRippleDb =
        std::isfinite(passbandMinimumDb) && std::isfinite(passbandMaximumDb)
        ? std::max(0.0, passbandMaximumDb - passbandMinimumDb)
        : 0.0;
    response.insert(QStringLiteral("frequencyResponse"), frequencyResponsePoints);
    response.insert(QStringLiteral("phase"), phasePoints);
    response.insert(QStringLiteral("groupDelay"), groupDelayPoints);
    response.insert(QStringLiteral("actualRippleDb"), actualRippleDb);
    response.insert(QStringLiteral("error"), QString());
    return response;
}

/** @brief 启动实时预处理定时器，开始周期性从DAQ设备读取数据并处理。 */
void SignalPreprocessing::startPreprocessing()
{
    const int processingSession = ++m_processingSession;
    {
        QMutexLocker locker(&m_dataCacheMutex);
        m_preprocessedDataCache.clear();
        m_processingActive = true;
    }

    if (m_worker != nullptr &&
        m_thread != nullptr &&
        m_thread->isRunning() &&
        QThread::currentThread() != m_thread) {
        QMetaObject::invokeMethod(
            m_worker,
            [this, processingSession]() {
                m_worker->updateProcessingSession(processingSession);
            },
            Qt::BlockingQueuedConnection);
    }

    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setInterval(COLLECTION_INTERVAL_MS);
        connect(m_timer, &QTimer::timeout, m_worker, &PreprocessingWorker::processing);
    }

    m_timer->start();
}

void SignalPreprocessing::stopPreprocessing()
{
    if (m_timer != nullptr) {
        m_timer->stop();
    }

    if (m_worker != nullptr &&
        m_thread != nullptr &&
        m_thread->isRunning() &&
        QThread::currentThread() != m_thread) {
        QMetaObject::invokeMethod(
            m_worker,
            &PreprocessingWorker::flushProcessing,
            Qt::BlockingQueuedConnection);
    }

    m_processingActive = false;
}

void SignalPreprocessing::updateDataCache(const QVector<float>& preprocessedData, int processingSession)
{
    if (!m_processingActive || processingSession != m_processingSession) {
        return;
    }

    QMutexLocker locker(&m_dataCacheMutex);
    m_preprocessedDataCache = preprocessedData;
}

QVector<float> SignalPreprocessing::getDataCache() const
{
    QMutexLocker locker(&m_dataCacheMutex);
    return m_preprocessedDataCache;
}

/**
 * @brief 对导入信号执行完整预处理管道：带通滤波、陷波、自适应降噪、小波去噪、瞬态抑制和运动伪迹削减。
 * @param samplingRate 信号采样率（Hz）。
 * @param rawData 原始浮点信号。
 * @returns 预处理后的信号。
 */
QVector<float> SignalPreprocessing::filterDataImport(
    int samplingRate,
    const QVector<float>& rawData)
{
    const int effectiveSampleRate = resolveSampleRate(samplingRate);
    const SignalPreprocessOptions importOptions = importPreprocessOptions();
    QVector<float> preprocessedData = rawData;
    QVariantMap timingSummary;
    timingSummary.insert(QStringLiteral("bandpassMs"), -1.0);
    timingSummary.insert(QStringLiteral("notchMs"), -1.0);
    timingSummary.insert(QStringLiteral("adaptiveNoiseReductionMs"), -1.0);
    timingSummary.insert(QStringLiteral("waveletDenoisingMs"), -1.0);
    timingSummary.insert(QStringLiteral("transientNoiseSuppressionMs"), -1.0);
    timingSummary.insert(QStringLiteral("motionArtifactReductionMs"), -1.0);
    timingSummary.insert(QStringLiteral("downsamplingMs"), -1.0);

    if (importOptions.bandpassEnabled ||
        importOptions.scientificFilter.enabled ||
        importOptions.notchEnabled) {
        preprocessedData = applyImportPreFilterPipeline(
            preprocessedData,
            effectiveSampleRate,
            importOptions,
            timingSummary);
    }

    if (importOptions.adaptiveNoiseReductionEnabled) {
        QElapsedTimer adaptiveTimer;
        adaptiveTimer.start();
        preprocessedData = AdaptiveNoiseReduction::denoise(
            preprocessedData,
            effectiveSampleRate,
            importOptions.adaptiveNoiseReductionParameters);
        timingSummary.insert(
            QStringLiteral("adaptiveNoiseReductionMs"),
            elapsedMilliseconds(adaptiveTimer));
    }

    if (importOptions.waveletEnabled) {
        QElapsedTimer waveletTimer;
        waveletTimer.start();
        preprocessedData = WaveletTransform::denoise(
            preprocessedData,
            effectiveSampleRate);
        timingSummary.insert(
            QStringLiteral("waveletDenoisingMs"),
            elapsedMilliseconds(waveletTimer));
    }

    if (importOptions.transientNoiseSuppressionEnabled) {
        QElapsedTimer transientTimer;
        transientTimer.start();
        preprocessedData = TransientNoiseSuppressor::suppress(
            preprocessedData,
            effectiveSampleRate);
        timingSummary.insert(
            QStringLiteral("transientNoiseSuppressionMs"),
            elapsedMilliseconds(transientTimer));
    }

    if (importOptions.motionArtifactReductionEnabled) {
        QElapsedTimer motionArtifactTimer;
        motionArtifactTimer.start();
        preprocessedData = MotionArtifactReduction::reduce(
            preprocessedData,
            effectiveSampleRate);
        timingSummary.insert(
            QStringLiteral("motionArtifactReductionMs"),
            elapsedMilliseconds(motionArtifactTimer));
    }

    const int bandpassFirOrder =
        timingSummary.take(QStringLiteral("bandpassFirOrder")).toInt();

    qDebug() << "SignalPreprocessing: imported preprocessing complete"
             << "sampleRate:" << effectiveSampleRate
             << "sampleCount:" << preprocessedData.size();
    DataManager::instance()->storeImportedTimeDomainData(
        effectiveSampleRate,
        preprocessedData);
    QElapsedTimer downsamplingTimer;
    downsamplingTimer.start();
    DataManager::instance()->storeImportedDownsampledData(
        AdaptiveDownsampling::buildCurrentLevelDownsampledPoints(
            preprocessedData,
            effectiveSampleRate));
    timingSummary.insert(
        QStringLiteral("downsamplingMs"),
        elapsedMilliseconds(downsamplingTimer));
    DataManager::instance()->updateImportedAnalysisSummary({
        {QStringLiteral("processing"), QVariantMap{
             {QStringLiteral("bandpassEnabled"), importOptions.bandpassEnabled},
             {QStringLiteral("notchEnabled"), importOptions.notchEnabled},
             {QStringLiteral("notchFrequencyMode"),
              normalizeNotchFrequencyMode(importOptions.notchFrequencyMode) == NotchFrequencyAdaptive
                  ? QStringLiteral("adaptive")
                  : QStringLiteral("fixed")},
             {QStringLiteral("firFilterEnabled"), importOptions.firFilterEnabled},
             {QStringLiteral("filterType"),
              importOptions.firFilterEnabled
                  ? QStringLiteral("FIR")
                  : QStringLiteral("IIR")},
              {QStringLiteral("bandpassFilterType"),
               importOptions.firFilterEnabled
                   ? QStringLiteral("FIR")
                   : QStringLiteral("IIR")},
              {QStringLiteral("bandpassFirOrder"), bandpassFirOrder},
              {QStringLiteral("scientificFilterEnabled"),
               importOptions.scientificFilter.enabled},
              {QStringLiteral("scientificFilterPrototype"),
               scientificFilterPrototypeLabel(importOptions.scientificFilter.prototype)},
              {QStringLiteral("scientificFilterType"),
               scientificFilterTypeLabel(importOptions.scientificFilter.filterType)},
              {QStringLiteral("scientificFilterOrder"),
               importOptions.scientificFilter.order},
              {QStringLiteral("scientificFilterCutoffFrequencyHz"),
               importOptions.scientificFilter.cutoffFrequencyHz},
              {QStringLiteral("scientificFilterLowCutoffFrequencyHz"),
               importOptions.scientificFilter.lowCutoffFrequencyHz},
              {QStringLiteral("scientificFilterHighCutoffFrequencyHz"),
               importOptions.scientificFilter.highCutoffFrequencyHz},
              {QStringLiteral("adaptiveNoiseReductionEnabled"),
               importOptions.adaptiveNoiseReductionEnabled},
             {QStringLiteral("waveletDenoisingEnabled"), importOptions.waveletEnabled},
             {QStringLiteral("transientNoiseSuppressionEnabled"),
              importOptions.transientNoiseSuppressionEnabled},
             {QStringLiteral("motionArtifactReductionEnabled"),
              importOptions.motionArtifactReductionEnabled},
             {QStringLiteral("motionArtifactReductionMethod"),
              importOptions.motionArtifactReductionEnabled
                  ? QStringLiteral("EMD-IMF-FD (per-channel)")
                  : QString()}
         }},
        {QStringLiteral("timings"), timingSummary}
    });
    return preprocessedData;
}

SignalPreprocessOptions SignalPreprocessing::importPreprocessOptions() const
{
    SignalPreprocessOptions options;
    {
        QMutexLocker locker(&m_importSettingsMutex);
        options.bandpassEnabled = m_importBandpassEnabled;
        options.notchEnabled = m_importNotchEnabled;
        options.notchFrequencyMode = m_importNotchFrequencyMode;
        options.firFilterEnabled = m_importFirFilterEnabled;
        options.scientificFilter =
            normalizeScientificFilterConfig(m_importScientificFilterConfig);
        options.adaptiveNoiseReductionEnabled = m_importAdaptiveNoiseReductionEnabled;
        options.adaptiveNoiseReductionParameters =
            normalizeAdaptiveNoiseReductionParameters(
                m_importAdaptiveNoiseReductionParameters);
        options.waveletEnabled = m_importWaveletDenoisingEnabled;
        options.transientNoiseSuppressionEnabled = m_importTransientNoiseSuppressionEnabled;
        options.motionArtifactReductionEnabled = m_importMotionArtifactReductionEnabled;
    }

    return options;
}

SignalPreprocessOptions SignalPreprocessing::realtimePreprocessOptions() const
{
    SignalPreprocessOptions options;
    {
        QMutexLocker locker(&m_realtimeSettingsMutex);
        options.bandpassEnabled = m_realtimeBandpassEnabled;
        options.notchEnabled = m_realtimeNotchEnabled;
        options.notchFrequencyMode = m_realtimeNotchFrequencyMode;
        options.firFilterEnabled = m_realtimeFirFilterEnabled;
        options.scientificFilter =
            normalizeScientificFilterConfig(m_realtimeScientificFilterConfig);
        options.activeNoiseCancellationEnabled = m_realtimeActiveNoiseCancellationEnabled;
        options.adaptiveNoiseReductionEnabled = m_realtimeAdaptiveNoiseReductionEnabled;
        options.adaptiveNoiseReductionParameters =
            normalizeAdaptiveNoiseReductionParameters(
                m_realtimeAdaptiveNoiseReductionParameters);
        options.waveletEnabled = m_realtimeWaveletDenoisingEnabled;
        options.transientNoiseSuppressionEnabled = m_realtimeTransientNoiseSuppressionEnabled;
        options.motionArtifactReductionEnabled = m_realtimeMotionArtifactReductionEnabled;
        options.gain = m_realtimeGain;
    }

    return options;
}

PreprocessingWorker::PreprocessingWorker(QObject* parent)
    : QObject(parent)
{
    initPreFilter(
        DataManager::instance()->configuredSampleRate(),
        SignalPreprocessOptions{});
}

PreprocessingWorker::~PreprocessingWorker() = default;

void PreprocessingWorker::initPreFilter(
    int sampleRate,
    const SignalPreprocessOptions& options)
{
    m_preFilterSampleRate = resolveSampleRate(sampleRate);
    m_preFilterBandpassEnabled = options.bandpassEnabled;
    m_preFilterBandpassFirFilterEnabled = options.firFilterEnabled;
    m_preFilterScientificFilterConfig =
        normalizeScientificFilterConfig(options.scientificFilter);
    m_preFilterNotchEnabled = options.notchEnabled;
    m_preFilterNotchFrequencyMode =
        normalizeNotchFrequencyMode(options.notchFrequencyMode);

    const PreFilterDesign bandpassDesign =
        makeBandpassFilterDesign(m_preFilterSampleRate, options.bandpassEnabled);
    const FirBandpassDesign firBandpassDesign =
        makeFirBandpassFilterDesign(
            m_preFilterSampleRate,
            options.bandpassEnabled && options.firFilterEnabled);
    const ScientificFilterDesign scientificFilterDesign =
        makeScientificFilterDesign(
            m_preFilterSampleRate,
            options.scientificFilter,
            true);
    const PreFilterDesign notchDesign =
        makeNotchFilterDesign(
            m_preFilterSampleRate,
            options.notchEnabled,
            fixedPowerlineFrequencies());

    const bool hasBandpassFilterState = options.firFilterEnabled
        ? firBandpassDesign.hasTaps()
        : bandpassDesign.hasSections();
    const bool hasScientificFilterState = scientificFilterDesign.hasSections();
    const bool hasNotchFilterState = notchDesign.hasSections();
    for (SignalPreFilterState& channelState : m_preFilterStates) {
        initializePreFilterState(
            channelState,
            bandpassDesign,
            firBandpassDesign,
            options.firFilterEnabled,
            scientificFilterDesign,
            options.scientificFilter.enabled,
            notchDesign);
    }
    m_hasBandpassFilterState = hasBandpassFilterState;
    m_hasScientificFilterState = hasScientificFilterState;
    m_hasNotchFilterState = hasNotchFilterState;
}

void PreprocessingWorker::updateChannel(int channel)
{
    QMutexLocker locker(&m_dataMutex);
    m_currentChannel = channel;
}

void PreprocessingWorker::updateProcessingSession(int session)
{
    QMutexLocker locker(&m_dataMutex);
    m_processingSession = session;
}

/**
 * @brief 定时执行实时预处理：从DAQ设备拉取各通道数据，依次通过预滤波器、ANC和自适应降噪，写入缓存并通知主线程。
 */
void PreprocessingWorker::processing()
{
    int selectedChannel = 0;
    int processingSession = 0;
    {
        QMutexLocker locker(&m_dataMutex);
        selectedChannel = m_currentChannel;
        processingSession = m_processingSession;
    }

    const int configuredSampleRate = DataManager::instance()->configuredSampleRate();
    const SignalPreprocessOptions realtimeOptions =
        SignalPreprocessing::instance()->realtimePreprocessOptions();

    const bool denoiseConfigChanged =
        m_realtimeDenoiseSampleRate != configuredSampleRate ||
        m_realtimeAncEnabled != realtimeOptions.activeNoiseCancellationEnabled ||
        m_realtimeAdaptiveEnabled != realtimeOptions.adaptiveNoiseReductionEnabled ||
        !adaptiveNoiseReductionParametersEqual(
            m_realtimeAdaptiveParameters,
            realtimeOptions.adaptiveNoiseReductionParameters);

    if (denoiseConfigChanged) {
        resetRealtimeDenoiseStates();
        m_realtimeDenoiseSampleRate = configuredSampleRate;
        m_realtimeAncEnabled = realtimeOptions.activeNoiseCancellationEnabled;
        m_realtimeAdaptiveEnabled = realtimeOptions.adaptiveNoiseReductionEnabled;
        m_realtimeAdaptiveParameters =
            realtimeOptions.adaptiveNoiseReductionParameters;
    }

    const bool preFilterConfigChanged =
        m_preFilterSampleRate != configuredSampleRate ||
        m_preFilterBandpassEnabled != realtimeOptions.bandpassEnabled ||
        m_preFilterBandpassFirFilterEnabled != realtimeOptions.firFilterEnabled ||
        !scientificFilterConfigsEqual(
            m_preFilterScientificFilterConfig,
            normalizeScientificFilterConfig(realtimeOptions.scientificFilter)) ||
        m_preFilterNotchEnabled != realtimeOptions.notchEnabled ||
        m_preFilterNotchFrequencyMode !=
            normalizeNotchFrequencyMode(realtimeOptions.notchFrequencyMode);

    if (preFilterConfigChanged) {
        initPreFilter(configuredSampleRate, realtimeOptions);
        resetRealtimeAncStates();
    }

    const int effectiveSelectedChannel = resolveEffectiveSelectedChannel(selectedChannel);
    if (effectiveSelectedChannel < 0) {
        qWarning() << "SignalPreprocessing: all channels are deactivated, skipping cache write";
        return;
    }

    QVector<float> selectedPreprocessedData;
    bool selectedChannelReady = false;
    const QVector<float> referenceNoiseData =
        DaqDeviceManager::instance()->getDataCache(kRealtimeReferenceNoiseChannel);
    const QVector<float> filteredReferenceNoiseData = applyStreamingPreFilter(
        referenceNoiseData,
        configuredSampleRate,
        realtimeOptions,
        &m_preFilterStates[static_cast<size_t>(kRealtimeReferenceNoiseChannel)],
        m_hasBandpassFilterState,
        m_hasScientificFilterState,
        m_hasNotchFilterState);
    DataManager::instance()->splicRealtimeChannelTimeDomainData(
        kRealtimeReferenceNoiseChannel,
        filteredReferenceNoiseData,
        configuredSampleRate);

    for (int channel = 0; channel < kRealtimeChannelCount; ++channel) {
        if (!DaqDeviceManager::instance()->isChannelActive(channel)) {
            continue;
        }

        const QVector<float> rawData = DaqDeviceManager::instance()->getDataCache(channel);
        if (rawData.isEmpty()) {
            continue;
        }

        QVector<float> preprocessedData;
        if (channel == effectiveSelectedChannel) {
            preprocessedData = runRealtimePreprocessPipeline(
                channel,
                rawData,
                m_preFilterSampleRate,
                realtimeOptions,
                &filteredReferenceNoiseData,
                &m_realtimeAncStates[static_cast<size_t>(channel)],
                &m_preFilterStates[static_cast<size_t>(channel)],
                m_hasBandpassFilterState,
                m_hasScientificFilterState,
                m_hasNotchFilterState,
                &m_realtimeAdaptiveStates[static_cast<size_t>(channel)]);
            selectedPreprocessedData = preprocessedData;
            selectedChannelReady = true;
        } else {
            preprocessedData = runRealtimePreprocessPipeline(
                channel,
                rawData,
                configuredSampleRate,
                realtimeOptions,
                &filteredReferenceNoiseData,
                &m_realtimeAncStates[static_cast<size_t>(channel)],
                &m_preFilterStates[static_cast<size_t>(channel)],
                m_hasBandpassFilterState,
                m_hasScientificFilterState,
                m_hasNotchFilterState,
                &m_realtimeAdaptiveStates[static_cast<size_t>(channel)]);
        }

        DataManager::instance()->splicRealtimeChannelTimeDomainData(
            channel,
            preprocessedData,
            configuredSampleRate);
    }

    if (!selectedChannelReady) {
        qWarning() << "SignalPreprocessing: no data read from selected active channel"
                   << effectiveSelectedChannel;
        return;
    }

    DataManager::instance()->splicTimeDomainData(
        selectedPreprocessedData,
        configuredSampleRate);
    emit resultReady(selectedPreprocessedData, processingSession);
}

void PreprocessingWorker::flushProcessing()
{
    const int configuredSampleRate = DataManager::instance()->configuredSampleRate();
    const int effectiveSampleRate = resolveSampleRate(configuredSampleRate);
    const SignalPreprocessOptions realtimeOptions =
        SignalPreprocessing::instance()->realtimePreprocessOptions();
    const int selectedChannel = SignalPreprocessing::instance()->currentChannel();
    const int effectiveSelectedChannel = resolveEffectiveSelectedChannel(selectedChannel);

    QVector<float> selectedPreprocessedData;
    bool selectedChannelReady = false;

    for (int channel = 0; channel < kRealtimeChannelCount; ++channel) {
        if (!DaqDeviceManager::instance()->isChannelActive(channel)) {
            continue;
        }

        ActiveNoiseCancellation::Metrics metrics;
        QVector<float> preprocessedData =
            ActiveNoiseCancellation::flush(
                m_realtimeAncStates[static_cast<size_t>(channel)],
                &metrics);

        if (realtimeOptions.activeNoiseCancellationEnabled &&
            !preprocessedData.isEmpty()) {
            logAncDebugMetrics(channel, preprocessedData.size(), 0, metrics);
        }

        if (realtimeOptions.adaptiveNoiseReductionEnabled) {
            if (!preprocessedData.isEmpty()) {
                preprocessedData = AdaptiveNoiseReduction::denoiseStreaming(
                    preprocessedData,
                    effectiveSampleRate,
                    m_realtimeAdaptiveStates[static_cast<size_t>(channel)],
                    realtimeOptions.adaptiveNoiseReductionParameters);
            }
            const QVector<float> adaptiveTail =
                AdaptiveNoiseReduction::flushStreaming(
                    effectiveSampleRate,
                    m_realtimeAdaptiveStates[static_cast<size_t>(channel)],
                    realtimeOptions.adaptiveNoiseReductionParameters);
            preprocessedData += adaptiveTail;
        }

        if (preprocessedData.isEmpty()) {
            continue;
        }

        preprocessedData = applyRealtimeGain(preprocessedData, realtimeOptions.gain);

        DataManager::instance()->splicRealtimeChannelTimeDomainData(
            channel,
            preprocessedData,
            effectiveSampleRate);

        if (channel == effectiveSelectedChannel) {
            selectedPreprocessedData = preprocessedData;
            selectedChannelReady = true;
        }
    }

    if (selectedChannelReady) {
        DataManager::instance()->splicTimeDomainData(
            selectedPreprocessedData,
            effectiveSampleRate);
        emit resultReady(selectedPreprocessedData, m_processingSession);
    }
}

void PreprocessingWorker::resetRealtimeAncStates()
{
    for (ActiveNoiseCancellation::StreamingState& ancState : m_realtimeAncStates) {
        ActiveNoiseCancellation::resetStreamingState(ancState);
    }
}

void PreprocessingWorker::resetRealtimeDenoiseStates()
{
    resetRealtimeAncStates();

    for (AdaptiveNoiseReduction::StreamingState& adaptiveState : m_realtimeAdaptiveStates) {
        AdaptiveNoiseReduction::resetStreamingState(adaptiveState);
    }
}
