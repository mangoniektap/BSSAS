/**
 * @file TransientNoiseSuppressor.cpp
 * @brief Offline transient noise suppression implementation with crest-factor detection,
 *        low-frequency bowel-sound protection, and OLA synthesis.
 */
#include "TransientNoiseSuppressor.h"

#include <QDebug>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kMinimumPower = 1e-12;
constexpr double kNormalizationEpsilon = 1e-8;

std::atomic_bool g_debugLoggingEnabled = true;

/** @brief Build a sqrt-Hann window for matched analysis and synthesis. */
QVector<double> buildSqrtHannWindow(int frameLength)
{
    QVector<double> window(frameLength, 1.0);
    if (frameLength <= 1) {
        return window;
    }

    for (int i = 0; i < frameLength; ++i) {
        const double phase = (2.0 * kPi * i) / (frameLength - 1);
        const double hann = 0.5 * (1.0 - std::cos(phase));
        window[i] = std::sqrt(std::max(hann, 0.0));
    }
    return window;
}

/** @brief Compute signal RMS while treating non-finite values as silence. */
double computeRms(const QVector<float>& samples)
{
    if (samples.isEmpty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (float sample : samples) {
        const double value = std::isfinite(sample) ? static_cast<double>(sample) : 0.0;
        sum += value * value;
    }
    return std::sqrt(sum / samples.size());
}

/** @brief Compute crest factor, peak divided by RMS, for transient detection. */
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

/** @brief Compute zero-crossing count retained for future transient classifiers. */
int computeZeroCrossingRate(const QVector<float>& samples)
{
    if (samples.size() < 2) {
        return 0;
    }

    int crossings = 0;
    for (int i = 1; i < samples.size(); ++i) {
        const double previous = static_cast<double>(samples[i - 1]);
        const double current = static_cast<double>(samples[i]);
        if (std::isfinite(previous) && std::isfinite(current) && previous * current < 0.0) {
            ++crossings;
        }
    }
    return crossings;
}

/** @brief Estimate low-frequency energy fraction with a first-order IIR lowpass approximation. */
double lowFreqEnergyFraction(const QVector<float>& samples, int sampleRate, double upperHz)
{
    if (samples.size() < 4) {
        return 0.5;
    }

    const double alpha = 1.0 - std::exp(-2.0 * kPi * upperHz / sampleRate);
    double lowpass = 0.0;
    double lowEnergy = 0.0;
    double totalEnergy = 0.0;
    for (float sample : samples) {
        const double value = static_cast<double>(sample);
        totalEnergy += value * value;
        lowpass += alpha * (value - lowpass);
        lowEnergy += lowpass * lowpass;
    }
    return totalEnergy > kMinimumPower ? lowEnergy / totalEnergy : 0.0;
}

/**
 * @brief Detect and attenuate one analysis frame.
 * @param frame Windowed analysis frame.
 * @param sampleRate Sampling rate in Hz.
 * @param params Algorithm parameters.
 * @param localMeanEnergy Recent local mean frame energy.
 * @param consecutiveBurstFrames Number of consecutive frames already classified as bursts.
 * @param isBurst Outputs whether this frame was attenuated as a transient burst.
 * @returns Suppressed frame samples.
 */
QVector<float> suppressFrame(
    const QVector<float>& frame,
    int sampleRate,
    const TransientNoiseSuppressor::Parameters& params,
    double localMeanEnergy,
    int consecutiveBurstFrames,
    bool& isBurst)
{
    isBurst = false;
    const int frameSize = frame.size();
    QVector<float> output(frameSize);

    const double crest = computeCrestFactor(frame);
    const double rms = computeRms(frame);
    const double energy = rms * rms * frameSize;
    const int zeroCrossings = computeZeroCrossingRate(frame);
    (void)zeroCrossings;

    const bool candidate =
        (crest > params.transientThreshold) &&
        (energy > localMeanEnergy * params.minEnergyRatio) &&
        (energy < localMeanEnergy * params.maxEnergyRatio);

    if (!candidate) {
        return frame;
    }

    const double consecutiveDurationMs =
        consecutiveBurstFrames * (params.frameLength / static_cast<double>(sampleRate) * 1000.0);
    if (consecutiveDurationMs > params.maxDurationMs) {
        return frame;
    }

    const double lowFraction = lowFreqEnergyFraction(frame, sampleRate, params.lowFreqUpperHz);
    double effectiveAttenuation = params.attenuationGain;
    if (lowFraction > params.bowelProtectFraction) {
        effectiveAttenuation = 0.5 * (1.0 + params.attenuationGain);
    }

    isBurst = true;
    for (int i = 0; i < frameSize; ++i) {
        output[i] = static_cast<float>(frame[i] * effectiveAttenuation);
    }
    return output;
}
} // namespace

TransientNoiseSuppressor::Parameters
TransientNoiseSuppressor::makeParameters(int sampleRate, int sampleCount)
{
    (void)sampleCount;

    Parameters params;
    params.frameLength = std::max(128, static_cast<int>(std::lround(sampleRate * 0.021)));
    params.hopLength = params.frameLength / 2;
    return params;
}

QVector<float> TransientNoiseSuppressor::suppress(const QVector<float>& input, int sampleRate)
{
    const Parameters params = makeParameters(sampleRate, static_cast<int>(input.size()));
    return suppress(input, sampleRate, params);
}

QVector<float> TransientNoiseSuppressor::suppress(
    const QVector<float>& input,
    int sampleRate,
    const Parameters& params)
{
    if (input.isEmpty()) {
        return {};
    }
    if (params.frameLength <= 0 || params.hopLength <= 0) {
        return input;
    }

    const int inputSize = static_cast<int>(input.size());
    const int leadPad = params.frameLength / 2;
    const int procLen = inputSize + leadPad * 2;
    const int frameCount =
        std::max(1, 1 + (procLen - params.frameLength + params.hopLength - 1) / params.hopLength);
    const int paddedLength = (frameCount - 1) * params.hopLength + params.frameLength;

    QVector<double> padded(paddedLength, 0.0);
    for (int i = 0; i < leadPad; ++i) {
        const int sourceIndex = leadPad - i;
        padded[i] = static_cast<double>(input[std::min(sourceIndex, inputSize - 1)]);
    }
    for (int i = 0; i < inputSize; ++i) {
        padded[leadPad + i] = static_cast<double>(input[i]);
    }
    for (int i = leadPad + inputSize; i < procLen; ++i) {
        const int distance = i - (leadPad + inputSize) + 1;
        const int sourceIndex = inputSize - 1 - distance;
        padded[i] = static_cast<double>(input[std::max(sourceIndex, 0)]);
    }

    const QVector<double> window = buildSqrtHannWindow(params.frameLength);
    QVector<double> outputAccumulator(paddedLength, 0.0);
    QVector<double> normalizationAccumulator(paddedLength, 0.0);
    QVector<float> analysisFrame(params.frameLength, 0.0f);

    constexpr int kEnergyBufferSize = 5;
    QVector<double> recentEnergy;
    int burstStreak = 0;
    int totalBurstFrames = 0;

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const int frameStart = frameIndex * params.hopLength;
        for (int i = 0; i < params.frameLength; ++i) {
            analysisFrame[i] = static_cast<float>(padded[frameStart + i] * window[i]);
        }

        double frameEnergy = 0.0;
        for (float sample : analysisFrame) {
            const double value = static_cast<double>(sample);
            frameEnergy += value * value;
        }

        if (recentEnergy.size() >= kEnergyBufferSize) {
            recentEnergy.pop_front();
        }
        recentEnergy.append(frameEnergy);

        double localMeanEnergy = 0.0;
        for (double energy : recentEnergy) {
            localMeanEnergy += energy;
        }
        localMeanEnergy /= recentEnergy.size();
        localMeanEnergy = std::max(localMeanEnergy, params.rmsFloor * params.frameLength);

        bool isBurst = false;
        const QVector<float> suppressed = suppressFrame(
            analysisFrame,
            sampleRate,
            params,
            localMeanEnergy,
            burstStreak,
            isBurst);

        if (isBurst) {
            ++burstStreak;
            ++totalBurstFrames;
        } else {
            burstStreak = 0;
        }

        for (int i = 0; i < params.frameLength; ++i) {
            const int destination = frameStart + i;
            const double windowedSample = static_cast<double>(suppressed[i]) * window[i];
            outputAccumulator[destination] += windowedSample;
            normalizationAccumulator[destination] += window[i] * window[i];
        }
    }

    QVector<float> output(inputSize, 0.0f);
    for (int i = 0; i < inputSize; ++i) {
        const int paddedIndex = leadPad + i;
        const double norm = normalizationAccumulator[paddedIndex];
        const double restored =
            norm > kNormalizationEpsilon
                ? outputAccumulator[paddedIndex] / norm
                : static_cast<double>(input[i]);
        output[i] = static_cast<float>(restored);
    }

    if (g_debugLoggingEnabled.load(std::memory_order_relaxed)) {
        const double inputRms = computeRms(input);
        const double outputRms = computeRms(output);
        double maxInput = 0.0;
        double maxOutput = 0.0;
        for (float value : input) {
            maxInput = std::max(maxInput, static_cast<double>(std::abs(value)));
        }
        for (float value : output) {
            maxOutput = std::max(maxOutput, static_cast<double>(std::abs(value)));
        }
        const double attenuationDb = 10.0 * std::log10(
            std::max(outputRms * outputRms / (inputRms * inputRms + 1e-12), 1e-12));

        qDebug().nospace()
            << "=== TransientNoiseSuppressor::suppress ==="
            << " Frames:" << frameCount
            << " BurstFrames:" << totalBurstFrames
            << " RMS In:" << 20 * std::log10(inputRms) << "dB"
            << " Out:" << 20 * std::log10(outputRms) << "dB"
            << " Attenuation:" << attenuationDb << "dB"
            << " PeakRetention:" << (maxInput > 1e-12 ? maxOutput / maxInput : 1.0);
    }

    return output;
}

void TransientNoiseSuppressor::setDebugLoggingEnabled(bool enabled)
{
    g_debugLoggingEnabled.store(enabled, std::memory_order_relaxed);
}

bool TransientNoiseSuppressor::debugLoggingEnabled()
{
    return g_debugLoggingEnabled.load(std::memory_order_relaxed);
}
