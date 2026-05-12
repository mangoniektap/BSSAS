/**
 * @file TransientNoiseSuppressor.cpp
 * @brief 离线瞬态噪声抑制模块，基于波峰因子检测、低频肠鸣音保护和重叠相加（OLA）合成策略实现脉冲式噪声衰减。
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

/** @brief 构建sqrt-Hann窗函数，用于匹配分析和合成。 */
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

/** @brief 计算信号RMS值，将非有限值视为静音。 */
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

/** @brief 计算波峰因子（峰值除以RMS），用于瞬态检测。 */
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

/** @brief 计算过零次数，保留用于未来瞬态分类器。 */
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

/** @brief 使用一阶IIR低通近似估计低频能量占比。 */
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
 * @brief 检测并衰减单个分析帧中的瞬态噪声。
 * @param frame 加窗分析帧。
 * @param sampleRate 采样率（Hz）。
 * @param params 算法参数。
 * @param localMeanEnergy 近期局部平均帧能量。
 * @param consecutiveBurstFrames 已分类为瞬态的连续帧数。
 * @param isBurst 输出该帧是否被识别为瞬态脉冲并衰减。
 * @returns 抑制后的帧采样数据。
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

/**
 * @brief 根据采样率自适应生成抑制参数。
 * @param sampleRate 采样率（Hz）。
 * @param sampleCount 保留参数（未使用）。
 * @returns 配置好的参数结构体。
 */
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

/**
 * @brief 使用指定参数对整个信号进行瞬态噪声抑制（OLA分帧处理）。
 * @param input 输入浮点信号。
 * @param sampleRate 采样率（Hz）。
 * @param params 算法参数。
 * @returns 瞬态噪声抑制后的信号。
 */
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
