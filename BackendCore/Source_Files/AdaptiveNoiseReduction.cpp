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

double adaptiveAlphaFromEntropy(
    double entropyContribution,
    double runningMean,
    double runningVariance,
    const AdaptiveNoiseReduction::Parameters& parameters)
{
    const double variance = std::max(runningVariance, kEntropyVarianceFloor);
    const double stdDeviation = std::sqrt(variance);
    const double zScore =
        std::abs(entropyContribution - runningMean) / std::max(stdDeviation, kEntropyEpsilon);
    const double normalizedZScore = std::clamp(
        zScore / std::max(parameters.adaptiveAlphaZScoreRange, 1.0),
        0.0,
        1.0);
    const double alphaSpan =
        parameters.adaptiveAlphaMaximum - parameters.adaptiveAlphaMinimum;

    return std::clamp(
        parameters.adaptiveAlphaMaximum - alphaSpan * normalizedZScore,
        parameters.adaptiveAlphaMinimum,
        parameters.adaptiveAlphaMaximum);
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
    state.entropyContributionMean.fill(0.0, halfBinCount);
    state.entropyContributionVariance.fill(0.0, halfBinCount);

    state.analysisFrame.fill(0.0f, frameLength);
    state.powerSpectrum.fill(kMinimumPower, halfBinCount);

    state.frameIndex = 0;
    state.minimaFrameCounter = 0;
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
    QVector<double> entropyContributionMean(halfBinCount, 0.0);
    QVector<double> entropyContributionVariance(halfBinCount, 0.0);

    QVector<float> analysisFrame(parameters.frameLength, 0.0f);
    QVector<double> powerSpectrum(halfBinCount, kMinimumPower);

    int minimaFrameCounter = 0;

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

        if (frameIndex == 0) {
            for (int bin = 0; bin < halfBinCount; ++bin) {
                smoothedPsd[bin] = powerSpectrum[bin];
                noisePsd[bin] = powerSpectrum[bin];
                minimaCurrent[bin] = powerSpectrum[bin];
                minimaReference[bin] = powerSpectrum[bin];
            }
        } else {
            for (int bin = 0; bin < halfBinCount; ++bin) {
                smoothedPsd[bin] =
                    parameters.spectrumSmoothing * smoothedPsd[bin] +
                    (1.0 - parameters.spectrumSmoothing) * powerSpectrum[bin];
                minimaCurrent[bin] = std::min(minimaCurrent[bin], smoothedPsd[bin]);
            }
        }

        const bool useNoiseInit = frameIndex < parameters.noiseInitFrameCount;
        if (useNoiseInit) {
            const double initBlend = 1.0 / static_cast<double>(frameIndex + 1);
            for (int bin = 0; bin < halfBinCount; ++bin) {
                noisePsd[bin] =
                    (1.0 - initBlend) * noisePsd[bin] +
                    initBlend * powerSpectrum[bin];
            }
        } else {
            ++minimaFrameCounter;
            if (minimaFrameCounter >= parameters.minTrackingFrames) {
                minimaReference = minimaCurrent;
                minimaCurrent = smoothedPsd;
                minimaFrameCounter = 0;
            }
        }

        const double totalPower =
            std::accumulate(powerSpectrum.cbegin(), powerSpectrum.cend(), 0.0);
        const bool transientFrame =
            computeCrestFactor(analysisFrame) >= parameters.transientCrestFactor;
        const double effectiveMinGain = transientFrame
            ? std::max(parameters.minGain, parameters.transientMinGain)
            : parameters.minGain;
        QVector<double> gainCurve(halfBinCount, 1.0);

        for (int bin = 0; bin < halfBinCount; ++bin) {
            const double power = powerSpectrum[bin];

            if (!useNoiseInit) {
                const double minimaEstimate = std::max(
                    std::min(minimaCurrent[bin], minimaReference[bin]),
                    kMinimumPower);
                const double minimaRatio = smoothedPsd[bin] / minimaEstimate;
                const double ratioProbability = std::clamp(
                    (minimaRatio - parameters.ratioSpeechPresenceLower) /
                        (parameters.ratioSpeechPresenceUpper -
                         parameters.ratioSpeechPresenceLower),
                    0.0,
                    1.0);

                const double posteriorFromOldNoise =
                    std::clamp(power / std::max(noisePsd[bin], kMinimumPower), 0.0, 1000.0);
                const double posteriorProbability = std::clamp(
                    (posteriorFromOldNoise - 1.0) /
                        (parameters.posterioriSnrSpeechUpper - 1.0),
                    0.0,
                    1.0);

                const double speechPresenceProbability =
                    std::max(ratioProbability, posteriorProbability);

                const double noiseUpdateAlpha =
                    parameters.noiseUpdateAlphaSpeech * speechPresenceProbability +
                    parameters.noiseUpdateAlphaNoiseOnly * (1.0 - speechPresenceProbability);
                noisePsd[bin] =
                    noiseUpdateAlpha * noisePsd[bin] +
                    (1.0 - noiseUpdateAlpha) * power;
            }

            noisePsd[bin] = std::max(noisePsd[bin], kMinimumPower);
            const double posterioriSnr = std::clamp(power / noisePsd[bin], 0.0, 1000.0);
            const double previousEnhancedSnrEstimate =
                previousGain[bin] * previousGain[bin] * previousPosterioriSnr[bin];
            const double entropyContribution =
                singleFrequencyEntropyContribution(power, totalPower);

            double adaptiveAlpha = parameters.adaptiveAlphaMaximum;
            if (frameIndex == 0) {
                entropyContributionMean[bin] = entropyContribution;
                entropyContributionVariance[bin] = 0.0;
            } else {
                adaptiveAlpha = adaptiveAlphaFromEntropy(
                    entropyContribution,
                    entropyContributionMean[bin],
                    entropyContributionVariance[bin],
                    parameters);
            }

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
                gain = effectiveMinGain;
            }
            gainCurve[bin] = std::clamp(gain, effectiveMinGain, 1.0);

            if (frameIndex > 0) {
                updateEntropyStatistics(
                    entropyContribution,
                    parameters.entropyTrackingRate,
                    entropyContributionMean[bin],
                    entropyContributionVariance[bin]);
            }

            previousPosterioriSnr[bin] = posterioriSnr;
        }

        smoothGainCurve(gainCurve, previousGain, parameters);
        applyGainCurveToSpectrum(spectrum, gainCurve, parameters.frameLength);
        previousGain = std::move(gainCurve);

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

        if (state.frameIndex == 0) {
            for (int bin = 0; bin < halfBinCount; ++bin) {
                state.smoothedPsd[bin] = state.powerSpectrum[bin];
                state.noisePsd[bin] = state.powerSpectrum[bin];
                state.minimaCurrent[bin] = state.powerSpectrum[bin];
                state.minimaReference[bin] = state.powerSpectrum[bin];
            }
        } else {
            for (int bin = 0; bin < halfBinCount; ++bin) {
                state.smoothedPsd[bin] =
                    parameters.spectrumSmoothing * state.smoothedPsd[bin] +
                    (1.0 - parameters.spectrumSmoothing) * state.powerSpectrum[bin];
                state.minimaCurrent[bin] =
                    std::min(state.minimaCurrent[bin], state.smoothedPsd[bin]);
            }
        }

        const bool useNoiseInit = state.frameIndex < parameters.noiseInitFrameCount;
        if (useNoiseInit) {
            const double initBlend = 1.0 / static_cast<double>(state.frameIndex + 1);
            for (int bin = 0; bin < halfBinCount; ++bin) {
                state.noisePsd[bin] =
                    (1.0 - initBlend) * state.noisePsd[bin] +
                    initBlend * state.powerSpectrum[bin];
            }
        } else {
            ++state.minimaFrameCounter;
            if (state.minimaFrameCounter >= parameters.minTrackingFrames) {
                state.minimaReference = state.minimaCurrent;
                state.minimaCurrent = state.smoothedPsd;
                state.minimaFrameCounter = 0;
            }
        }

        const double totalPower =
            std::accumulate(state.powerSpectrum.cbegin(), state.powerSpectrum.cend(), 0.0);
        const bool transientFrame =
            computeCrestFactor(state.analysisFrame) >= parameters.transientCrestFactor;
        const double effectiveMinGain = transientFrame
            ? std::max(parameters.minGain, parameters.transientMinGain)
            : parameters.minGain;
        QVector<double> gainCurve(halfBinCount, 1.0);

        for (int bin = 0; bin < halfBinCount; ++bin) {
            const double power = state.powerSpectrum[bin];

            if (!useNoiseInit) {
                const double minimaEstimate = std::max(
                    std::min(state.minimaCurrent[bin], state.minimaReference[bin]),
                    kMinimumPower);
                const double minimaRatio = state.smoothedPsd[bin] / minimaEstimate;
                const double ratioProbability = std::clamp(
                    (minimaRatio - parameters.ratioSpeechPresenceLower) /
                        (parameters.ratioSpeechPresenceUpper -
                         parameters.ratioSpeechPresenceLower),
                    0.0,
                    1.0);

                const double posteriorFromOldNoise = std::clamp(
                    power / std::max(state.noisePsd[bin], kMinimumPower),
                    0.0,
                    1000.0);
                const double posteriorProbability = std::clamp(
                    (posteriorFromOldNoise - 1.0) /
                        (parameters.posterioriSnrSpeechUpper - 1.0),
                    0.0,
                    1.0);

                const double speechPresenceProbability =
                    std::max(ratioProbability, posteriorProbability);

                const double noiseUpdateAlpha =
                    parameters.noiseUpdateAlphaSpeech * speechPresenceProbability +
                    parameters.noiseUpdateAlphaNoiseOnly *
                        (1.0 - speechPresenceProbability);
                state.noisePsd[bin] =
                    noiseUpdateAlpha * state.noisePsd[bin] +
                    (1.0 - noiseUpdateAlpha) * power;
            }

            state.noisePsd[bin] = std::max(state.noisePsd[bin], kMinimumPower);
            const double posterioriSnr =
                std::clamp(power / state.noisePsd[bin], 0.0, 1000.0);
            const double previousEnhancedSnrEstimate =
                state.previousGain[bin] * state.previousGain[bin] *
                state.previousPosterioriSnr[bin];
            const double entropyContribution =
                singleFrequencyEntropyContribution(power, totalPower);

            double adaptiveAlpha = parameters.adaptiveAlphaMaximum;
            if (state.frameIndex == 0) {
                state.entropyContributionMean[bin] = entropyContribution;
                state.entropyContributionVariance[bin] = 0.0;
            } else {
                adaptiveAlpha = adaptiveAlphaFromEntropy(
                    entropyContribution,
                    state.entropyContributionMean[bin],
                    state.entropyContributionVariance[bin],
                    parameters);
            }

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
                gain = effectiveMinGain;
            }
            gainCurve[bin] = std::clamp(gain, effectiveMinGain, 1.0);

            if (state.frameIndex > 0) {
                updateEntropyStatistics(
                    entropyContribution,
                    parameters.entropyTrackingRate,
                    state.entropyContributionMean[bin],
                    state.entropyContributionVariance[bin]);
            }

            state.previousPosterioriSnr[bin] = posterioriSnr;
        }

        smoothGainCurve(gainCurve, state.previousGain, parameters);
        applyGainCurveToSpectrum(spectrum, gainCurve, frameLength);
        state.previousGain = std::move(gainCurve);

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
