/** @file AdaptiveNoiseReduction.cpp
 *  @brief 自适应降噪 (ANR) 算法实现。基于短时傅里叶变换 (STFT) 和 MMSE 谱减法，
 *         结合最小谱跟踪噪声估计、语音存在概率估计、自适应先验 SNR 平滑（基于谱熵的 Z-score 驱动）
 *         以及重叠相加 (OLA) 合成，对音频信号进行单通道降噪处理。
 */

#include "AdaptiveNoiseReduction.h"

#include "DataManager.h"
#include "KfrDftUtils.h"

#include <QDebug>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEulerGamma = 0.57721566490153286061;
constexpr double kMinimumPower = 1e-12;
constexpr double kNormalizationEpsilon = 1e-8;
constexpr double kEntropyEpsilon = 1e-12;
constexpr double kEntropyVarianceFloor = 1e-8;
constexpr double kNonStationaryBypassProbability = 0.70;
constexpr double kMinimumLearningProbabilityForUpdate = 0.15;
constexpr double kSpectralFluxBinLower = 0.22;
constexpr double kSpectralFluxBinUpper = 0.95;
constexpr double kSpectralFluxFrameLower = 0.08;
constexpr double kSpectralFluxFrameUpper = 0.32;
constexpr double kBroadbandRiseRatioLower = 1.8;
constexpr double kBroadbandRiseRatioUpper = 5.0;
constexpr double kBroadbandRiseShareLower = 0.18;
constexpr double kBroadbandRiseShareUpper = 0.42;
constexpr double kEntropyZScoreLower = 2.0;
constexpr double kEntropyZScoreUpper = 4.5;
constexpr double kLocalNonStationaryShareLower = 0.10;
constexpr double kLocalNonStationaryShareUpper = 0.32;

std::atomic_bool g_debugLoggingEnabled = true;

void logDenoisingMetrics(
    const QVector<float>& input,
    const QVector<float>& output,
    const char* label)
{
    if (!g_debugLoggingEnabled.load(std::memory_order_relaxed)) {
        return;
    }

    const int sampleCount = std::min(input.size(), output.size());
    if (sampleCount <= 0) {
        return;
    }

    double sumInputSq = 0.0;
    double sumOutputSq = 0.0;
    double sumDiffSq = 0.0;
    double maxInput = 0.0;
    double maxOutput = 0.0;

    for (int index = 0; index < sampleCount; ++index) {
        const double inputValue = std::isfinite(input[index])
            ? static_cast<double>(input[index])
            : 0.0;
        const double outputValue = std::isfinite(output[index])
            ? static_cast<double>(output[index])
            : 0.0;
        const double diff = inputValue - outputValue;

        sumInputSq += inputValue * inputValue;
        sumOutputSq += outputValue * outputValue;
        sumDiffSq += diff * diff;
        maxInput = std::max(maxInput, std::abs(inputValue));
        maxOutput = std::max(maxOutput, std::abs(outputValue));
    }

    const double inputRms =
        std::sqrt(sumInputSq / static_cast<double>(sampleCount));
    const double outputRms =
        std::sqrt(sumOutputSq / static_cast<double>(sampleCount));
    const double inputRmsDb = 20.0 * std::log10(std::max(inputRms, 1e-12));
    const double outputRmsDb = 20.0 * std::log10(std::max(outputRms, 1e-12));
    const double noiseReductionDb =
        (sumInputSq > 1e-12 && sumDiffSq > 1e-12)
            ? 10.0 * std::log10(sumInputSq / sumDiffSq)
            : 99.0;
    const double signalAttenuationDb =
        10.0 * std::log10(
            std::max(sumOutputSq, 1e-12) / std::max(sumInputSq, 1e-12));
    const double peakRetention =
        maxInput > 1e-12 ? maxOutput / maxInput : 1.0;

    qDebug().nospace()
        << "[" << label << "]"
        << " Input RMS:" << QString::number(inputRmsDb, 'f', 1) << "dB"
        << " Output RMS:" << QString::number(outputRmsDb, 'f', 1) << "dB"
        << " NR:" << QString::number(noiseReductionDb, 'f', 1) << "dB"
        << " Attenuation:" << QString::number(signalAttenuationDb, 'f', 1) << "dB"
        << " PeakRetention:" << QString::number(peakRetention, 'f', 3);
}

int normalizeSampleRate(int sampleRate)
{
    if (sampleRate > 0) {
        return sampleRate;
    }

    return DataManager::DEFAULT_SAMPLE_RATE;
}

int nextPowerOfTwo(int value)
{
    if (value <= 1) {
        return 1;
    }

    int power = 1;
    while (power < value) {
        power <<= 1;
    }

    return power;
}

QVector<double> buildSqrtHannWindow(int frameLength)
{
    QVector<double> window(frameLength, 1.0);
    if (frameLength <= 1) {
        return window;
    }

    for (int index = 0; index < frameLength; ++index) {
        const double phase =
            (2.0 * kPi * static_cast<double>(index)) /
            static_cast<double>(frameLength - 1);
        const double hann = 0.5 * (1.0 - std::cos(phase));
        window[index] = std::sqrt(std::max(hann, 0.0));
    }

    return window;
}

/** @brief 计算指数积分 E1(x) = integral_t=x..inf exp(-t)/t dt。
 *         使用级数展开（x<1）或朗斯基连分式展开（x>=1），用于 MMSE 增益计算。
 *  @param x 输入参数 (x > 0)
 *  @returns E1(x) 近似值
 */
double exponentialIntegralE1(double x)
{
    if (x <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }

    constexpr int kMaxIterations = 200;
    constexpr double kTolerance = 1e-10;
    constexpr double kTiny = 1e-30;

    if (x < 1.0) {
        double term = 1.0;
        double series = 0.0;

        for (int n = 1; n < kMaxIterations; ++n) {
            term *= -x / static_cast<double>(n);
            const double delta = -term / static_cast<double>(n);
            series += delta;
            if (std::abs(delta) < kTolerance) {
                break;
            }
        }

        return -std::log(x) - kEulerGamma + series;
    }

    double b = x + 1.0;
    double c = 1.0 / kTiny;
    double d = 1.0 / b;
    double h = d;

    for (int n = 1; n < kMaxIterations; ++n) {
        const double a = -static_cast<double>(n * n);
        b += 2.0;

        double denominator = a * d + b;
        if (std::abs(denominator) < kTiny) {
            denominator = kTiny;
        }
        d = 1.0 / denominator;

        double cValue = b + a / c;
        if (std::abs(cValue) < kTiny) {
            cValue = kTiny;
        }
        c = cValue;

        const double delta = c * d;
        h *= delta;
        if (std::abs(delta - 1.0) < kTolerance) {
            break;
        }
    }

    return h * std::exp(-x);
}

double singleFrequencyEntropyContribution(double power, double totalPower)
{
    if (totalPower <= kMinimumPower || power <= 0.0) {
        return 0.0;
    }

    const double probability = std::clamp(power / totalPower, 0.0, 1.0);
    return -probability * std::log(probability + kEntropyEpsilon);
}

void updateEntropyStatistics(
    double entropyContribution,
    double trackingRate,
    double& runningMean,
    double& runningVariance)
{
    const double effectiveTrackingRate = std::clamp(trackingRate, 0.01, 1.0);
    const double delta = entropyContribution - runningMean;
    runningMean += effectiveTrackingRate * delta;
    runningVariance =
        (1.0 - effectiveTrackingRate) * runningVariance +
        effectiveTrackingRate * delta * delta;
}

/** @brief 初始化或重新初始化 ANR 流式处理状态：分配帧缓冲、窗函数、频谱估计及熵统计数组。
 *  @param state 待初始化的流式状态
 *  @param sampleRate 信号采样率 (Hz)
 *  @param parameters ANR 参数配置
 */
void initializeStreamingState(
    AdaptiveNoiseReduction::StreamingState& state,
    int sampleRate,
    const AdaptiveNoiseReduction::Parameters& parameters)
{
    state.sampleRate = normalizeSampleRate(sampleRate);
    state.parameters = parameters;

    const int frameLength = state.parameters.frameLength;
    const int hopLength = state.parameters.hopLength;
    const int overlapLength = std::max(0, frameLength - hopLength);
    const int halfBinCount = frameLength / 2 + 1;

    state.window = buildSqrtHannWindow(frameLength);
    state.analysisBuffer.clear();
    state.synthesisOverlap.fill(0.0, overlapLength);
    state.normalizationOverlap.fill(0.0, overlapLength);

    state.smoothedPsd.fill(kMinimumPower, halfBinCount);
    state.noisePsd.fill(kMinimumPower, halfBinCount);
    state.minimaCurrent.fill(std::numeric_limits<double>::max(), halfBinCount);
    state.minimaReference.fill(std::numeric_limits<double>::max(), halfBinCount);
    state.previousGain.fill(1.0, halfBinCount);
    state.previousPosterioriSnr.fill(1.0, halfBinCount);
    state.previousPowerSpectrum.fill(kMinimumPower, halfBinCount);
    state.entropyContributionMean.fill(0.0, halfBinCount);
    state.entropyContributionVariance.fill(0.0, halfBinCount);

    state.analysisFrame.fill(0.0f, frameLength);
    state.powerSpectrum.fill(kMinimumPower, halfBinCount);

    state.frameIndex = 0;
    state.minimaFrameCounter = 0;
    state.noiseInitStableFrameCount = 0;
    state.hasPreviousPowerSpectrum = false;
    state.initialized = frameLength > 0 && hopLength > 0;
}

double computeRms(const QVector<float>& samples)
{
    if (samples.isEmpty()) {
        return 0.0;
    }

    double energy = 0.0;
    for (float sample : samples) {
        const double value = std::isfinite(sample) ? static_cast<double>(sample) : 0.0;
        energy += value * value;
    }

    return std::sqrt(energy / static_cast<double>(samples.size()));
}

double computeCrestFactor(const QVector<float>& samples)
{
    const double rms = computeRms(samples);
    if (rms <= kMinimumPower) {
        return 0.0;
    }

    double peak = 0.0;
    for (float sample : samples) {
        peak = std::max(peak, std::abs(static_cast<double>(sample)));
    }

    return peak / rms;
}

void smoothGainCurve(
    QVector<double>& gains,
    const QVector<double>& previousGain,
    const AdaptiveNoiseReduction::Parameters& parameters)
{
    if (gains.isEmpty()) {
        return;
    }

    const double temporalSmoothing =
        std::clamp(parameters.gainTemporalSmoothing, 0.0, 0.95);
    if (previousGain.size() == gains.size() && temporalSmoothing > 0.0) {
        for (int bin = 0; bin < gains.size(); ++bin) {
            gains[bin] =
                temporalSmoothing * previousGain[bin] +
                (1.0 - temporalSmoothing) * gains[bin];
        }
    }

    const double frequencySmoothing =
        std::clamp(parameters.gainFrequencySmoothing, 0.0, 0.45);
    if (gains.size() <= 2 || frequencySmoothing <= 0.0) {
        return;
    }

    QVector<double> smoothed = gains;
    for (int bin = 1; bin < gains.size() - 1; ++bin) {
        const double neighborAverage = 0.5 * (gains[bin - 1] + gains[bin + 1]);
        smoothed[bin] =
            (1.0 - frequencySmoothing) * gains[bin] +
            frequencySmoothing * neighborAverage;
    }

    gains = std::move(smoothed);
}

void applyGainCurveToSpectrum(
    QVector<std::complex<double>>& spectrum,
    const QVector<double>& gains,
    int frameLength)
{
    const int nyquistIndex = frameLength / 2;
    const bool hasNyquistBin = (frameLength % 2 == 0);

    for (int bin = 0; bin < gains.size(); ++bin) {
        const double gain = std::clamp(gains[bin], 0.0, 1.0);
        spectrum[bin] *= gain;
        if (bin > 0 && (!hasNyquistBin || bin != nyquistIndex)) {
            const int mirroredBin = frameLength - bin;
            if (mirroredBin >= 0 && mirroredBin < spectrum.size()) {
                spectrum[mirroredBin] *= gain;
            }
        }
    }
}

double probabilityFromRange(double lower, double upper, double value)
{
    if (upper <= lower) {
        return value >= upper ? 1.0 : 0.0;
    }

    const double normalized = std::clamp((value - lower) / (upper - lower), 0.0, 1.0);
    return normalized * normalized * (3.0 - 2.0 * normalized);
}

double positiveLogRise(double value, double reference)
{
    return std::max(
        0.0,
        std::log(std::max(value, kMinimumPower)) -
            std::log(std::max(reference, kMinimumPower)));
}

double entropyAnomalyProbability(
    double entropyContribution,
    double runningMean,
    double runningVariance)
{
    const double variance = std::max(runningVariance, kEntropyVarianceFloor);
    const double zScore =
        std::abs(entropyContribution - runningMean) /
        std::max(std::sqrt(variance), kEntropyEpsilon);
    return probabilityFromRange(kEntropyZScoreLower, kEntropyZScoreUpper, zScore);
}

double transientProbability(
    const QVector<float>& analysisFrame,
    const AdaptiveNoiseReduction::Parameters& parameters)
{
    const double crestThreshold = std::max(parameters.transientCrestFactor, 1.0);
    return probabilityFromRange(
        std::max(1.0, crestThreshold * 0.65),
        crestThreshold,
        computeCrestFactor(analysisFrame));
}

struct LearningDecision
{
    QVector<double> learningProbability;
    QVector<double> entropyContribution;
    double frameNonStationaryProbability = 0.0;
    bool bypassFrame = false;
};

LearningDecision makeLearningDecision(
    const QVector<float>& analysisFrame,
    const QVector<double>& powerSpectrum,
    const QVector<double>& previousPowerSpectrum,
    bool hasPreviousPowerSpectrum,
    int noiseInitStableFrameCount,
    const QVector<double>& smoothedPsd,
    const QVector<double>& noisePsd,
    const QVector<double>& minimaCurrent,
    const QVector<double>& minimaReference,
    const QVector<double>& entropyContributionMean,
    const QVector<double>& entropyContributionVariance,
    const AdaptiveNoiseReduction::Parameters& parameters)
{
    const int binCount = powerSpectrum.size();
    LearningDecision decision;
    decision.learningProbability.fill(1.0, binCount);
    decision.entropyContribution.fill(0.0, binCount);

    if (binCount <= 0) {
        return decision;
    }

    const bool hasStableNoiseModel =
        noiseInitStableFrameCount > 0 &&
        smoothedPsd.size() == binCount &&
        noisePsd.size() == binCount &&
        minimaCurrent.size() == binCount &&
        minimaReference.size() == binCount;
    const bool hasPreviousSpectrum =
        noiseInitStableFrameCount >= 2 &&
        hasPreviousPowerSpectrum &&
        previousPowerSpectrum.size() == binCount;
    const bool hasEntropyStatistics =
        noiseInitStableFrameCount >= 2 &&
        entropyContributionMean.size() == binCount &&
        entropyContributionVariance.size() == binCount;

    const double totalPower =
        std::accumulate(powerSpectrum.cbegin(), powerSpectrum.cend(), 0.0);
    const double frameTransientProbability =
        transientProbability(analysisFrame, parameters);
    const double spectrumSmoothing =
        std::clamp(parameters.spectrumSmoothing, 0.0, 0.995);

    QVector<double> binSuppressionProbability(binCount, 0.0);

    double spectralFluxSum = 0.0;
    int spectralFluxHighBinCount = 0;
    double broadbandRiseSum = 0.0;
    int broadbandRiseHighBinCount = 0;
    double entropyAnomalySum = 0.0;
    int entropyAnomalyHighBinCount = 0;
    int localNonStationaryBinCount = 0;

    for (int bin = 0; bin < binCount; ++bin) {
        const double power = std::max(powerSpectrum[bin], kMinimumPower);
        decision.entropyContribution[bin] =
            singleFrequencyEntropyContribution(power, totalPower);

        double spectralFluxProbability = 0.0;
        if (hasPreviousSpectrum) {
            const double logRise = positiveLogRise(power, previousPowerSpectrum[bin]);
            spectralFluxSum += logRise;
            spectralFluxProbability =
                probabilityFromRange(kSpectralFluxBinLower, kSpectralFluxBinUpper, logRise);
            if (spectralFluxProbability >= 0.5) {
                ++spectralFluxHighBinCount;
            }
        }

        double speechPresenceProbability = 0.0;
        double broadbandRiseProbability = 0.0;
        if (hasStableNoiseModel) {
            const double minimaEstimate = std::max(
                std::min(minimaCurrent[bin], minimaReference[bin]),
                kMinimumPower);
            const double candidateSmoothedPsd =
                spectrumSmoothing * std::max(smoothedPsd[bin], kMinimumPower) +
                (1.0 - spectrumSmoothing) * power;
            const double minimaRatio = candidateSmoothedPsd / minimaEstimate;
            const double ratioProbability = probabilityFromRange(
                parameters.ratioSpeechPresenceLower,
                parameters.ratioSpeechPresenceUpper,
                minimaRatio);

            const double posteriorFromOldNoise =
                std::clamp(power / std::max(noisePsd[bin], kMinimumPower), 0.0, 1000.0);
            const double posteriorProbability = probabilityFromRange(
                1.0,
                std::max(parameters.posterioriSnrSpeechUpper, 1.01),
                posteriorFromOldNoise);
            speechPresenceProbability =
                std::max(ratioProbability, posteriorProbability);

            const double broadbandLogRise =
                positiveLogRise(power, std::max(noisePsd[bin], minimaEstimate));
            broadbandRiseSum += broadbandLogRise;
            broadbandRiseProbability = probabilityFromRange(
                std::log(kBroadbandRiseRatioLower),
                std::log(kBroadbandRiseRatioUpper),
                broadbandLogRise);
            if (broadbandRiseProbability >= 0.5) {
                ++broadbandRiseHighBinCount;
            }
        }

        double entropyProbability = 0.0;
        if (hasEntropyStatistics) {
            entropyProbability = entropyAnomalyProbability(
                decision.entropyContribution[bin],
                entropyContributionMean[bin],
                entropyContributionVariance[bin]);
            entropyAnomalySum += entropyProbability;
            if (entropyProbability >= 0.5) {
                ++entropyAnomalyHighBinCount;
            }
        }

        binSuppressionProbability[bin] = std::max(
            std::max(speechPresenceProbability, spectralFluxProbability),
            std::max(broadbandRiseProbability, entropyProbability));
        if (binSuppressionProbability[bin] >= 0.55) {
            ++localNonStationaryBinCount;
        }
    }

    const double inverseBinCount = 1.0 / static_cast<double>(binCount);
    const double spectralFluxFrameProbability = hasPreviousSpectrum
        ? std::max(
              probabilityFromRange(
                  kSpectralFluxFrameLower,
                  kSpectralFluxFrameUpper,
                  spectralFluxSum * inverseBinCount),
              probabilityFromRange(
                  kLocalNonStationaryShareLower,
                  kLocalNonStationaryShareUpper,
                  static_cast<double>(spectralFluxHighBinCount) * inverseBinCount))
        : 0.0;
    const double broadbandRiseFrameProbability = hasStableNoiseModel
        ? std::max(
              probabilityFromRange(
                  std::log(1.25),
                  std::log(2.20),
                  broadbandRiseSum * inverseBinCount),
              probabilityFromRange(
                  kBroadbandRiseShareLower,
                  kBroadbandRiseShareUpper,
                  static_cast<double>(broadbandRiseHighBinCount) * inverseBinCount))
        : 0.0;
    const double entropyFrameProbability = hasEntropyStatistics
        ? std::max(
              probabilityFromRange(0.18, 0.45, entropyAnomalySum * inverseBinCount),
              probabilityFromRange(
                  kLocalNonStationaryShareLower,
                  kLocalNonStationaryShareUpper,
                  static_cast<double>(entropyAnomalyHighBinCount) * inverseBinCount))
        : 0.0;
    const double localFrameProbability = probabilityFromRange(
        kLocalNonStationaryShareLower,
        kLocalNonStationaryShareUpper,
        static_cast<double>(localNonStationaryBinCount) * inverseBinCount);

    const double frameSuppressionProbability = std::max(
        std::max(frameTransientProbability, spectralFluxFrameProbability),
        std::max(broadbandRiseFrameProbability, entropyFrameProbability));
    decision.frameNonStationaryProbability =
        std::max(frameSuppressionProbability, localFrameProbability);
    decision.bypassFrame =
        decision.frameNonStationaryProbability >= kNonStationaryBypassProbability;

    for (int bin = 0; bin < binCount; ++bin) {
        const double learningSuppression = std::max(
            binSuppressionProbability[bin],
            frameSuppressionProbability);
        decision.learningProbability[bin] =
            std::clamp(1.0 - learningSuppression, 0.0, 1.0);
    }

    return decision;
}

void updateStationaryNoiseModel(
    const QVector<double>& powerSpectrum,
    const LearningDecision& decision,
    const AdaptiveNoiseReduction::Parameters& parameters,
    QVector<double>& smoothedPsd,
    QVector<double>& noisePsd,
    QVector<double>& minimaCurrent,
    QVector<double>& minimaReference,
    QVector<double>& entropyContributionMean,
    QVector<double>& entropyContributionVariance,
    int& minimaFrameCounter,
    int& noiseInitStableFrameCount)
{
    if (decision.bypassFrame || powerSpectrum.isEmpty()) {
        return;
    }

    const int binCount = powerSpectrum.size();
    const bool firstStableNoiseFrame = noiseInitStableFrameCount <= 0;
    if (firstStableNoiseFrame) {
        for (int bin = 0; bin < binCount; ++bin) {
            const double power = std::max(powerSpectrum[bin], kMinimumPower);
            smoothedPsd[bin] = power;
            noisePsd[bin] = power;
            minimaCurrent[bin] = power;
            minimaReference[bin] = power;
            entropyContributionMean[bin] = decision.entropyContribution[bin];
            entropyContributionVariance[bin] = 0.0;
        }
        noiseInitStableFrameCount = 1;
        return;
    }

    const bool useNoiseInit =
        noiseInitStableFrameCount < parameters.noiseInitFrameCount;
    const double spectrumSmoothing =
        std::clamp(parameters.spectrumSmoothing, 0.0, 0.995);
    const double entropyTrackingRate =
        std::clamp(parameters.entropyTrackingRate, 0.01, 1.0);

    for (int bin = 0; bin < binCount; ++bin) {
        const double learningProbability = decision.learningProbability[bin];
        if (learningProbability < kMinimumLearningProbabilityForUpdate) {
            continue;
        }

        const double power = std::max(powerSpectrum[bin], kMinimumPower);
        const double smoothedCandidate =
            spectrumSmoothing * smoothedPsd[bin] +
            (1.0 - spectrumSmoothing) * power;
        smoothedPsd[bin] =
            (1.0 - learningProbability) * smoothedPsd[bin] +
            learningProbability * smoothedCandidate;
        minimaCurrent[bin] = std::min(minimaCurrent[bin], smoothedPsd[bin]);

        if (useNoiseInit) {
            const double initBlend =
                learningProbability /
                static_cast<double>(noiseInitStableFrameCount + 1);
            noisePsd[bin] =
                (1.0 - initBlend) * noisePsd[bin] +
                initBlend * power;
        } else {
            const double noiseUpdateAlpha =
                parameters.noiseUpdateAlphaSpeech * (1.0 - learningProbability) +
                parameters.noiseUpdateAlphaNoiseOnly * learningProbability;
            noisePsd[bin] =
                noiseUpdateAlpha * noisePsd[bin] +
                (1.0 - noiseUpdateAlpha) * power;
        }
        noisePsd[bin] = std::max(noisePsd[bin], kMinimumPower);

        updateEntropyStatistics(
            decision.entropyContribution[bin],
            entropyTrackingRate,
            entropyContributionMean[bin],
            entropyContributionVariance[bin]);
    }

    if (useNoiseInit) {
        ++noiseInitStableFrameCount;
        return;
    }

    ++minimaFrameCounter;
    if (minimaFrameCounter >= parameters.minTrackingFrames) {
        minimaReference = minimaCurrent;
        minimaCurrent = smoothedPsd;
        minimaFrameCounter = 0;
    }
}

void applyStationaryNoiseReductionGain(
    QVector<std::complex<double>>& spectrum,
    const QVector<double>& powerSpectrum,
    const LearningDecision& decision,
    const AdaptiveNoiseReduction::Parameters& parameters,
    const QVector<double>& noisePsd,
    QVector<double>& previousGain,
    QVector<double>& previousPosterioriSnr)
{
    if (decision.bypassFrame || powerSpectrum.isEmpty()) {
        return;
    }

    const int binCount = powerSpectrum.size();
    QVector<double> gainCurve(binCount, 1.0);
    QVector<double> posterioriSnrValues(binCount, 1.0);
    const double adaptiveAlpha =
        std::clamp(parameters.adaptiveAlphaMaximum, 0.0, 0.995);

    for (int bin = 0; bin < binCount; ++bin) {
        const double learningProbability = decision.learningProbability[bin];
        if (learningProbability < kMinimumLearningProbabilityForUpdate) {
            gainCurve[bin] = 1.0;
            continue;
        }

        const double posterioriSnr =
            std::clamp(
                powerSpectrum[bin] / std::max(noisePsd[bin], kMinimumPower),
                0.0,
                1000.0);
        posterioriSnrValues[bin] = posterioriSnr;

        const double previousEnhancedSnrEstimate =
            previousGain[bin] * previousGain[bin] * previousPosterioriSnr[bin];
        const double prioriSnr = std::clamp(
            adaptiveAlpha * previousEnhancedSnrEstimate +
                (1.0 - adaptiveAlpha) * std::max(posterioriSnr - 1.0, 0.0),
            parameters.minPrioriSnr,
            parameters.maxPrioriSnr);

        const double v = std::max(
            (prioriSnr * posterioriSnr) / (1.0 + prioriSnr),
            kMinimumPower);
        const double mmseWeight = prioriSnr / (1.0 + prioriSnr);
        double gain = mmseWeight * std::exp(0.5 * exponentialIntegralE1(v));
        if (!std::isfinite(gain)) {
            gain = parameters.minGain;
        }

        const double learningAwareGain =
            learningProbability * gain + (1.0 - learningProbability);
        gainCurve[bin] =
            std::clamp(learningAwareGain, parameters.minGain, 1.0);
    }

    smoothGainCurve(gainCurve, previousGain, parameters);
    for (int bin = 0; bin < binCount; ++bin) {
        if (decision.learningProbability[bin] < kMinimumLearningProbabilityForUpdate) {
            gainCurve[bin] = 1.0;
        }
    }

    applyGainCurveToSpectrum(spectrum, gainCurve, parameters.frameLength);

    for (int bin = 0; bin < binCount; ++bin) {
        if (decision.learningProbability[bin] < kMinimumLearningProbabilityForUpdate) {
            continue;
        }
        previousGain[bin] = gainCurve[bin];
        previousPosterioriSnr[bin] = posterioriSnrValues[bin];
    }
}

void processNoiseLearningFrame(
    const QVector<float>& analysisFrame,
    QVector<std::complex<double>>& spectrum,
    const QVector<double>& powerSpectrum,
    const AdaptiveNoiseReduction::Parameters& parameters,
    QVector<double>& smoothedPsd,
    QVector<double>& noisePsd,
    QVector<double>& minimaCurrent,
    QVector<double>& minimaReference,
    QVector<double>& previousGain,
    QVector<double>& previousPosterioriSnr,
    QVector<double>& entropyContributionMean,
    QVector<double>& entropyContributionVariance,
    QVector<double>& previousPowerSpectrum,
    bool& hasPreviousPowerSpectrum,
    int& minimaFrameCounter,
    int& noiseInitStableFrameCount)
{
    const LearningDecision decision = makeLearningDecision(
        analysisFrame,
        powerSpectrum,
        previousPowerSpectrum,
        hasPreviousPowerSpectrum,
        noiseInitStableFrameCount,
        smoothedPsd,
        noisePsd,
        minimaCurrent,
        minimaReference,
        entropyContributionMean,
        entropyContributionVariance,
        parameters);

    if (!decision.bypassFrame) {
        updateStationaryNoiseModel(
            powerSpectrum,
            decision,
            parameters,
            smoothedPsd,
            noisePsd,
            minimaCurrent,
            minimaReference,
            entropyContributionMean,
            entropyContributionVariance,
            minimaFrameCounter,
            noiseInitStableFrameCount);
        applyStationaryNoiseReductionGain(
            spectrum,
            powerSpectrum,
            decision,
            parameters,
            noisePsd,
            previousGain,
            previousPosterioriSnr);
    }

    if (!decision.bypassFrame &&
        noiseInitStableFrameCount > 0 &&
        smoothedPsd.size() == powerSpectrum.size()) {
        previousPowerSpectrum = smoothedPsd;
        hasPreviousPowerSpectrum = true;
    } else if (!hasPreviousPowerSpectrum) {
        previousPowerSpectrum = powerSpectrum;
        hasPreviousPowerSpectrum = true;
    }
}

void protectOutputRms(
    const QVector<float>& input,
    QVector<float>& output,
    const AdaptiveNoiseReduction::Parameters& parameters)
{
    if (input.size() != output.size() || input.isEmpty()) {
        return;
    }

    const double inputRms = computeRms(input);
    const double outputRms = computeRms(output);
    const double minimumOutputRms =
        inputRms * std::clamp(parameters.minimumOutputRmsRatio, 0.1, 0.95);
    if (inputRms <= kMinimumPower || outputRms >= minimumOutputRms) {
        return;
    }

    const double blend = std::clamp(
        (minimumOutputRms - outputRms) / std::max(minimumOutputRms, kMinimumPower),
        0.0,
        0.65);
    for (int index = 0; index < output.size(); ++index) {
        output[index] = static_cast<float>(
            (1.0 - blend) * static_cast<double>(output[index]) +
            blend * static_cast<double>(input[index]));
    }
}
} // namespace

/** @brief 根据采样率和样本数生成自适应降噪的帧级参数。
 *  @param sampleRate 信号采样率 (Hz)
 *  @param sampleCount 信号样本总数
 *  @returns 包含帧长、跳数、噪声跟踪参数等的配置
 */
AdaptiveNoiseReduction::Parameters AdaptiveNoiseReduction::makeParameters(
    int sampleRate,
    int sampleCount)
{
    Parameters parameters;
    if (sampleCount <= 0) {
        return parameters;
    }

    const int targetFrameLength =
        std::max(64, static_cast<int>(std::lround(sampleRate * 0.04)));
    const int upperFrameLimit = sampleRate >= 100000 ? 4096 : 2048;

    parameters.frameLength = std::clamp(
        nextPowerOfTwo(targetFrameLength),
        128,
        upperFrameLimit);

    if (sampleCount < parameters.frameLength / 2) {
        parameters.frameLength = std::clamp(
            nextPowerOfTwo(std::max(sampleCount, 64)),
            128,
            parameters.frameLength);
    }

    parameters.hopLength = std::max(64, parameters.frameLength / 2);

    const double hopSeconds = std::max(
        static_cast<double>(parameters.hopLength) / static_cast<double>(sampleRate),
        1e-3);
    parameters.minTrackingFrames =
        std::max(8, static_cast<int>(std::lround(0.4 / hopSeconds)));
    parameters.noiseInitFrameCount =
        std::clamp(static_cast<int>(std::lround(0.2 / hopSeconds)), 3, 10);

    return parameters;
}

/** @brief 对整段音频信号进行离线自适应降噪处理（非流式）。
 *  @param input 输入音频信号
 *  @param sampleRate 信号采样率 (Hz)
 *  @returns 降噪后的音频信号
 */
QVector<float> AdaptiveNoiseReduction::denoise(
    const QVector<float>& input,
    int sampleRate)
{
    if (input.isEmpty()) {
        return {};
    }

    const int effectiveSampleRate = normalizeSampleRate(sampleRate);
    const Parameters parameters = makeParameters(effectiveSampleRate, input.size());
    if (parameters.frameLength <= 0 || parameters.hopLength <= 0) {
        return input;
    }

    const int leadingPadding = parameters.frameLength / 2;
    const int processingLength = input.size() + leadingPadding * 2;
    const int frameCount = std::max(
        1,
        1 + std::max(
                0,
                processingLength - parameters.frameLength + parameters.hopLength - 1) /
                parameters.hopLength);
    const int paddedLength =
        (frameCount - 1) * parameters.hopLength + parameters.frameLength;

    QVector<double> paddedInput(paddedLength, 0.0);
    const double firstSample = static_cast<double>(input.first());
    const double lastSample = static_cast<double>(input.last());

    for (int index = 0; index < leadingPadding; ++index) {
        paddedInput[index] = firstSample;
    }

    for (int index = 0; index < input.size(); ++index) {
        paddedInput[leadingPadding + index] = static_cast<double>(input[index]);
    }

    for (int index = leadingPadding + input.size(); index < processingLength; ++index) {
        paddedInput[index] = lastSample;
    }

    const QVector<double> window = buildSqrtHannWindow(parameters.frameLength);
    QVector<double> outputAccumulator(paddedLength, 0.0);
    QVector<double> normalizationAccumulator(paddedLength, 0.0);

    const int halfBinCount = parameters.frameLength / 2 + 1;

    QVector<double> smoothedPsd(halfBinCount, kMinimumPower);
    QVector<double> noisePsd(halfBinCount, kMinimumPower);
    QVector<double> minimaCurrent(halfBinCount, std::numeric_limits<double>::max());
    QVector<double> minimaReference(halfBinCount, std::numeric_limits<double>::max());
    QVector<double> previousGain(halfBinCount, 1.0);
    QVector<double> previousPosterioriSnr(halfBinCount, 1.0);
    QVector<double> previousPowerSpectrum(halfBinCount, kMinimumPower);
    QVector<double> entropyContributionMean(halfBinCount, 0.0);
    QVector<double> entropyContributionVariance(halfBinCount, 0.0);

    QVector<float> analysisFrame(parameters.frameLength, 0.0f);
    QVector<double> powerSpectrum(halfBinCount, kMinimumPower);

    int minimaFrameCounter = 0;
    int noiseInitStableFrameCount = 0;
    bool hasPreviousPowerSpectrum = false;

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const int frameStart = frameIndex * parameters.hopLength;
        for (int sampleIndex = 0; sampleIndex < parameters.frameLength; ++sampleIndex) {
            analysisFrame[sampleIndex] = static_cast<float>(
                paddedInput[frameStart + sampleIndex] * window[sampleIndex]);
        }

        QVector<std::complex<double>> spectrum =
            KfrDftUtils::computeComplexDft(analysisFrame);
        if (spectrum.size() != parameters.frameLength) {
            continue;
        }

        for (int bin = 0; bin < halfBinCount; ++bin) {
            powerSpectrum[bin] = std::max(std::norm(spectrum[bin]), kMinimumPower);
        }

        processNoiseLearningFrame(
            analysisFrame,
            spectrum,
            powerSpectrum,
            parameters,
            smoothedPsd,
            noisePsd,
            minimaCurrent,
            minimaReference,
            previousGain,
            previousPosterioriSnr,
            entropyContributionMean,
            entropyContributionVariance,
            previousPowerSpectrum,
            hasPreviousPowerSpectrum,
            minimaFrameCounter,
            noiseInitStableFrameCount);

        const QVector<double> enhancedFrame =
            KfrDftUtils::computeInverseComplexDftReal(spectrum);
        if (enhancedFrame.size() != parameters.frameLength) {
            continue;
        }

        for (int sampleIndex = 0; sampleIndex < parameters.frameLength; ++sampleIndex) {
            const int destinationIndex = frameStart + sampleIndex;
            const double windowedSample = enhancedFrame[sampleIndex] * window[sampleIndex];
            outputAccumulator[destinationIndex] += windowedSample;
            normalizationAccumulator[destinationIndex] +=
                window[sampleIndex] * window[sampleIndex];
        }
    }

    QVector<float> denoised(input.size(), 0.0f);
    for (int index = 0; index < input.size(); ++index) {
        const int paddedIndex = leadingPadding + index;
        const double normalization = normalizationAccumulator[paddedIndex];
        const double restored =
            normalization > kNormalizationEpsilon
                ? outputAccumulator[paddedIndex] / normalization
                : static_cast<double>(input[index]);
        denoised[index] = static_cast<float>(restored);
    }

    protectOutputRms(input, denoised, parameters);
    logDenoisingMetrics(input, denoised, "AdaptiveDenoise");
    return denoised;
}

void AdaptiveNoiseReduction::resetStreamingState(StreamingState& state)
{
    state = StreamingState{};
}

/** @brief 对流式输入音频进行自适应降噪，保持跨帧状态以实现连续处理。
 *  @param input 当前帧输入音频信号
 *  @param sampleRate 信号采样率 (Hz)
 *  @param state 流式处理状态（保持跨帧噪声估计和 OLA 缓冲区）
 *  @returns 当前帧降噪后的音频信号
 */
QVector<float> AdaptiveNoiseReduction::denoiseStreaming(
    const QVector<float>& input,
    int sampleRate,
    StreamingState& state)
{
    if (input.isEmpty()) {
        return {};
    }

    const int effectiveSampleRate = normalizeSampleRate(sampleRate);
    if (!state.initialized ||
        state.sampleRate != effectiveSampleRate ||
        state.parameters.frameLength <= 0 ||
        state.parameters.hopLength <= 0) {
        const Parameters parameters =
            makeParameters(effectiveSampleRate, std::max(static_cast<int>(input.size()), 256));
        initializeStreamingState(state, effectiveSampleRate, parameters);
    }

    if (!state.initialized) {
        return input;
    }

    const Parameters& parameters = state.parameters;
    const int frameLength = parameters.frameLength;
    const int hopLength = parameters.hopLength;
    const int overlapLength = std::max(0, frameLength - hopLength);
    const int halfBinCount = frameLength / 2 + 1;

    state.analysisBuffer.reserve(state.analysisBuffer.size() + input.size());
    for (const float sample : input) {
        state.analysisBuffer.append(static_cast<double>(sample));
    }

    QVector<float> output;
    output.reserve(input.size());

    while (state.analysisBuffer.size() >= frameLength) {
        for (int sampleIndex = 0; sampleIndex < frameLength; ++sampleIndex) {
            state.analysisFrame[sampleIndex] = static_cast<float>(
                state.analysisBuffer[sampleIndex] * state.window[sampleIndex]);
        }

        QVector<std::complex<double>> spectrum =
            KfrDftUtils::computeComplexDft(state.analysisFrame);
        if (spectrum.size() != frameLength) {
            break;
        }

        for (int bin = 0; bin < halfBinCount; ++bin) {
            state.powerSpectrum[bin] =
                std::max(std::norm(spectrum[bin]), kMinimumPower);
        }

        processNoiseLearningFrame(
            state.analysisFrame,
            spectrum,
            state.powerSpectrum,
            parameters,
            state.smoothedPsd,
            state.noisePsd,
            state.minimaCurrent,
            state.minimaReference,
            state.previousGain,
            state.previousPosterioriSnr,
            state.entropyContributionMean,
            state.entropyContributionVariance,
            state.previousPowerSpectrum,
            state.hasPreviousPowerSpectrum,
            state.minimaFrameCounter,
            state.noiseInitStableFrameCount);

        const QVector<double> enhancedFrame =
            KfrDftUtils::computeInverseComplexDftReal(spectrum);
        if (enhancedFrame.size() != frameLength) {
            break;
        }

        QVector<double> hopNumerator(hopLength, 0.0);
        QVector<double> hopDenominator(hopLength, 0.0);

        for (int i = 0; i < overlapLength; ++i) {
            hopNumerator[i] = state.synthesisOverlap[i];
            hopDenominator[i] = state.normalizationOverlap[i];
        }

        QVector<double> nextSynthesisOverlap(overlapLength, 0.0);
        QVector<double> nextNormalizationOverlap(overlapLength, 0.0);

        for (int sampleIndex = 0; sampleIndex < frameLength; ++sampleIndex) {
            const double windowedSample =
                enhancedFrame[sampleIndex] * state.window[sampleIndex];
            const double windowEnergy =
                state.window[sampleIndex] * state.window[sampleIndex];

            if (sampleIndex < hopLength) {
                hopNumerator[sampleIndex] += windowedSample;
                hopDenominator[sampleIndex] += windowEnergy;
            } else {
                const int overlapIndex = sampleIndex - hopLength;
                if (overlapIndex < overlapLength) {
                    nextSynthesisOverlap[overlapIndex] += windowedSample;
                    nextNormalizationOverlap[overlapIndex] += windowEnergy;
                }
            }
        }

        for (int i = 0; i < hopLength; ++i) {
            const double denominator = hopDenominator[i];
            const double restored = denominator > kNormalizationEpsilon
                ? hopNumerator[i] / denominator
                : 0.0;
            output.append(static_cast<float>(restored));
        }

        state.synthesisOverlap = std::move(nextSynthesisOverlap);
        state.normalizationOverlap = std::move(nextNormalizationOverlap);
        state.analysisBuffer.erase(
            state.analysisBuffer.begin(),
            state.analysisBuffer.begin() + hopLength);
        ++state.frameIndex;
    }

    if (output.size() < input.size()) {
        const int missing = input.size() - output.size();
        output.reserve(input.size());
        for (int i = 0; i < missing; ++i) {
            output.append(input[input.size() - missing + i]);
        }
    }

    if (output.size() > input.size()) {
        output.resize(input.size());
    }

    protectOutputRms(input, output, parameters);
    logDenoisingMetrics(input, output, "AdaptiveDenoise");
    return output;
}

void AdaptiveNoiseReduction::setDebugLoggingEnabled(bool enabled)
{
    g_debugLoggingEnabled.store(enabled, std::memory_order_relaxed);
}

bool AdaptiveNoiseReduction::debugLoggingEnabled()
{
    return g_debugLoggingEnabled.load(std::memory_order_relaxed);
}
