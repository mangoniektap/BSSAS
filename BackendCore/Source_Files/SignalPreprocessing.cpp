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
#include <functional>

namespace {
constexpr double kLowCutoffHz = 20.0;
constexpr double kHighCutoffHz = 2000.0;
constexpr double kPowerlineFundamentalHz = 50.0;
constexpr double kPowerlineSecondHarmonicHz = 100.0;
constexpr double kNotchQFactor = 30.0;
constexpr double kMinimumCutoffGapHz = 1.0;
constexpr double kMinimumRealtimeGain = 0.5;
constexpr double kMaximumRealtimeGain = 5.0;
constexpr double kRealtimeGainEpsilon = 0.0001;
constexpr int kRealtimeChannelCount = 7;
constexpr int kRealtimeReferenceNoiseChannel = 7;
constexpr int kAncDebugLogEveryFrames = 10;
constexpr double kMinimumRmsForDb = 1e-12;
constexpr size_t kPreFilterSectionCount = 4;

struct PreFilterDesign
{
    std::array<kfr::biquad_section<double>, kPreFilterSectionCount> sections{};
    size_t sectionCount = 0;

    bool hasSections() const
    {
        return sectionCount > 0;
    }

    kfr::iir_params<double, 4> params() const
    {
        return kfr::iir_params<double, 4>{ sections.data(), sectionCount };
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

void appendPowerlineNotchSection(
    std::array<kfr::biquad_section<double>, kPreFilterSectionCount>& sections,
    size_t& sectionCount,
    double centerFrequencyHz,
    double sampleRateHz)
{
    if (sectionCount >= sections.size()) {
        return;
    }

    const double normalizedFrequency = centerFrequencyHz / sampleRateHz;
    sections[sectionCount++] = kfr::biquad_notch(normalizedFrequency, kNotchQFactor);
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
            const double qButterworth = 0.7071067811865476;
            const double normalizedLowCutoff = kLowCutoffHz / sampleRateHz;
            const double normalizedHighCutoff = effectiveHighCutoffHz / sampleRateHz;

            design.sections[design.sectionCount++] =
                kfr::biquad_highpass(normalizedLowCutoff, qButterworth);
            design.sections[design.sectionCount++] =
                kfr::biquad_lowpass(normalizedHighCutoff, qButterworth);
        }
    }

    if (options.notchEnabled &&
        isFrequencyWithinNyquist(kPowerlineFundamentalHz + kMinimumCutoffGapHz, sampleRate)) {
        appendPowerlineNotchSection(
            design.sections,
            design.sectionCount,
            kPowerlineFundamentalHz,
            sampleRateHz);
    }

    if (options.notchEnabled &&
        isFrequencyWithinNyquist(kPowerlineSecondHarmonicHz + kMinimumCutoffGapHz, sampleRate)) {
        appendPowerlineNotchSection(
            design.sections,
            design.sectionCount,
            kPowerlineSecondHarmonicHz,
            sampleRateHz);
    }

    return design;
}

QVector<float> applyIirPreFilter(
    const QVector<float>& rawData,
    kfr::iir_state<double, 4>& filterState)
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

QVector<float> applyPreFilterDesign(
    const QVector<float>& rawData,
    const PreFilterDesign& design)
{
    if (rawData.isEmpty() || !design.hasSections()) {
        return rawData;
    }

    kfr::iir_state<double, 4> filterState{ design.params() };
    return applyIirPreFilter(rawData, filterState);
}

QVector<float> applyRealtimePreFilter(
    const QVector<float>& rawData,
    int sampleRate,
    const SignalPreprocessOptions& options,
    kfr::iir_state<double, 4>* filterState = nullptr,
    bool hasFilterState = false)
{
    if (rawData.isEmpty()) {
        return {};
    }

    const PreFilterDesign preFilterDesign = makePreFilterDesign(sampleRate, options);
    if (!preFilterDesign.hasSections()) {
        return rawData;
    }

    if (filterState != nullptr && hasFilterState) {
        return applyIirPreFilter(rawData, *filterState);
    }

    kfr::iir_state<double, 4> temporaryState{ preFilterDesign.params() };
    return applyIirPreFilter(rawData, temporaryState);
}

double elapsedMilliseconds(const QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
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
    kfr::iir_state<double, 4>* filterState = nullptr,
    bool hasFilterState = false,
    AdaptiveNoiseReduction::StreamingState* adaptiveStreamingState = nullptr)
{
    if (rawData.isEmpty()) {
        return {};
    }

    const int effectiveSampleRate = resolveSampleRate(sampleRate);
    QVector<float> processedData = applyRealtimePreFilter(
        rawData,
        effectiveSampleRate,
        options,
        filterState,
        hasFilterState);

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
                *adaptiveStreamingState);
        } else {
            processedData = AdaptiveNoiseReduction::denoise(
                processedData,
                effectiveSampleRate);
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

bool SignalPreprocessing::importAdaptiveNoiseReductionEnabled() const
{
    QMutexLocker locker(&m_importSettingsMutex);
    return m_importAdaptiveNoiseReductionEnabled;
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

    if (importOptions.bandpassEnabled) {
        SignalPreprocessOptions bandpassOnlyOptions;
        bandpassOnlyOptions.notchEnabled = false;

        const PreFilterDesign bandpassDesign =
            makePreFilterDesign(effectiveSampleRate, bandpassOnlyOptions);
        QElapsedTimer bandpassTimer;
        bandpassTimer.start();
        preprocessedData = applyPreFilterDesign(preprocessedData, bandpassDesign);
        timingSummary.insert(QStringLiteral("bandpassMs"), elapsedMilliseconds(bandpassTimer));
    }

    if (importOptions.notchEnabled) {
        SignalPreprocessOptions notchOnlyOptions;
        notchOnlyOptions.bandpassEnabled = false;

        const PreFilterDesign notchDesign =
            makePreFilterDesign(effectiveSampleRate, notchOnlyOptions);
        QElapsedTimer notchTimer;
        notchTimer.start();
        preprocessedData = applyPreFilterDesign(preprocessedData, notchDesign);
        timingSummary.insert(QStringLiteral("notchMs"), elapsedMilliseconds(notchTimer));
    }

    if (importOptions.adaptiveNoiseReductionEnabled) {
        QElapsedTimer adaptiveTimer;
        adaptiveTimer.start();
        preprocessedData = AdaptiveNoiseReduction::denoise(
            preprocessedData,
            effectiveSampleRate);
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
             {QStringLiteral("firFilterEnabled"), importOptions.firFilterEnabled},
             {QStringLiteral("filterType"),
              importOptions.firFilterEnabled
                  ? QStringLiteral("FIR")
                  : QStringLiteral("IIR")},
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
        options.firFilterEnabled = m_importFirFilterEnabled;
        options.adaptiveNoiseReductionEnabled = m_importAdaptiveNoiseReductionEnabled;
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
        options.firFilterEnabled = m_realtimeFirFilterEnabled;
        options.activeNoiseCancellationEnabled = m_realtimeActiveNoiseCancellationEnabled;
        options.adaptiveNoiseReductionEnabled = m_realtimeAdaptiveNoiseReductionEnabled;
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
    m_preFilterNotchEnabled = options.notchEnabled;
    const PreFilterDesign design = makePreFilterDesign(
        m_preFilterSampleRate,
        options);
    m_hasPreFilterState = design.hasSections();
    for (RealtimePreFilterState& channelState : m_preFilterStates) {
        if (m_hasPreFilterState) {
            channelState.state = kfr::iir_state<double, 4>{ design.params() };
        } else {
            channelState.state =
                kfr::iir_state<double, 4>{ kfr::iir_params<double, 4>() };
        }
    }
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
        m_realtimeAdaptiveEnabled != realtimeOptions.adaptiveNoiseReductionEnabled;

    if (denoiseConfigChanged) {
        resetRealtimeDenoiseStates();
        m_realtimeDenoiseSampleRate = configuredSampleRate;
        m_realtimeAncEnabled = realtimeOptions.activeNoiseCancellationEnabled;
        m_realtimeAdaptiveEnabled = realtimeOptions.adaptiveNoiseReductionEnabled;
    }

    const bool preFilterConfigChanged =
        m_preFilterSampleRate != configuredSampleRate ||
        m_preFilterBandpassEnabled != realtimeOptions.bandpassEnabled ||
        m_preFilterNotchEnabled != realtimeOptions.notchEnabled;

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
    const QVector<float> filteredReferenceNoiseData = applyRealtimePreFilter(
        referenceNoiseData,
        configuredSampleRate,
        realtimeOptions,
        &m_preFilterStates[static_cast<size_t>(kRealtimeReferenceNoiseChannel)].state,
        m_hasPreFilterState);
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
                &m_preFilterStates[static_cast<size_t>(channel)].state,
                m_hasPreFilterState,
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
                &m_preFilterStates[static_cast<size_t>(channel)].state,
                m_hasPreFilterState,
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
        if (preprocessedData.isEmpty()) {
            continue;
        }

        if (realtimeOptions.activeNoiseCancellationEnabled) {
            logAncDebugMetrics(channel, preprocessedData.size(), 0, metrics);
        }

        if (realtimeOptions.adaptiveNoiseReductionEnabled) {
            preprocessedData = AdaptiveNoiseReduction::denoiseStreaming(
                preprocessedData,
                effectiveSampleRate,
                m_realtimeAdaptiveStates[static_cast<size_t>(channel)]);
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
