/**
 * @file TransientNoiseSuppressor.cpp
 * @brief 瞬态噪声抑制算法实现，含峭度因子检测、肠鸣音保护和 OLA 合成。
 */
#include "TransientNoiseSuppressor.h"
#include <QDebug>
#include <atomic>
#include <cmath>
#include <limits>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kMinimumPower = 1e-12;
constexpr double kNormalizationEpsilon = 1e-8;

std::atomic_bool g_debugLoggingEnabled = true;

/** @brief 构造 sqrt-Hann 分析/合成窗。 */
QVector<double> buildSqrtHannWindow(int frameLength)
{
    QVector<double> w(frameLength, 1.0);
    if (frameLength <= 1) return w;
    for (int i = 0; i < frameLength; ++i) {
        double phase = (2.0 * kPi * i) / (frameLength - 1);
        double hann = 0.5 * (1.0 - std::cos(phase));
        w[i] = std::sqrt(std::max(hann, 0.0));
    }
    return w;
}

/** @brief 计算信号均方根值。 */
double computeRms(const QVector<float>& samples)
{
    if (samples.isEmpty()) return 0.0;
    double sum = 0.0;
    for (float s : samples) {
        double v = std::isfinite(s) ? static_cast<double>(s) : 0.0;
        sum += v * v;
    }
    return std::sqrt(sum / samples.size());
}

/** @brief 计算峭度因子（峰值/RMS），用于检测瞬态事件。 */
double computeCrestFactor(const QVector<float>& samples)
{
    double rms = computeRms(samples);
    if (rms <= kMinimumPower) return 0.0;
    double peak = 0.0;
    for (float s : samples)
        peak = std::max(peak, std::abs(static_cast<double>(s)));
    return peak / rms;
}

/** @brief 计算过零率，辅助区分噪声与生物信号。 */
int computeZeroCrossingRate(const QVector<float>& samples)
{
    if (samples.size() < 2) return 0;
    int crossings = 0;
    for (int i = 1; i < samples.size(); ++i) {
        double a = static_cast<double>(samples[i-1]);
        double b = static_cast<double>(samples[i]);
        if (std::isfinite(a) && std::isfinite(b) && a * b < 0.0)
            ++crossings;
    }
    return crossings;
}

/** @brief 计算低频能量占比，使用一阶 IIR 低通近似。 */
double lowFreqEnergyFraction(const QVector<float>& samples, int sampleRate,
                             double upperHz)
{
    if (samples.size() < 4) return 0.5;
    double a = 1.0 - std::exp(-2.0 * kPi * upperHz / sampleRate);
    double y = 0.0;
    double lowE = 0.0, totalE = 0.0;
    for (int i = 0; i < samples.size(); ++i) {
        double x = static_cast<double>(samples[i]);
        totalE += x * x;
        y += a * (x - y);
        lowE += y * y;
    }
    return totalE > kMinimumPower ? lowE / totalE : 0.0;
}

/**
 * @brief 对单个分析帧进行瞬态检测与衰减。
 * @param isBurst 输出参数，标记当前帧是否被判定为瞬态。
 */
QVector<float> suppressFrame(const QVector<float>& frame, int sampleRate,
                             const TransientNoiseSuppressor::Parameters& params,
                             double localMeanEnergy, int consecutiveBurstFrames,
                             bool &isBurst)
{
    isBurst = false;
    const int N = frame.size();
    QVector<float> out(N);

    double crest = computeCrestFactor(frame);
    double rms = computeRms(frame);
    double energy = rms * rms * N;
    int zcr = computeZeroCrossingRate(frame);

    bool candidate = (crest > params.transientThreshold) &&
                     (energy > localMeanEnergy * params.minEnergyRatio) &&
                     (energy < localMeanEnergy * params.maxEnergyRatio);

    if (!candidate) {
        out = frame;
        return out;
    }

    if (consecutiveBurstFrames * (params.frameLength / static_cast<double>(sampleRate) * 1000.0)
        > params.maxDurationMs) {
        out = frame;
        return out;
    }

    double lowFrac = lowFreqEnergyFraction(frame, sampleRate, params.lowFreqUpperHz);
    double effectiveAttenuation = params.attenuationGain;
    if (lowFrac > params.bowelProtectFraction) {
        effectiveAttenuation = 0.5 * (1.0 + params.attenuationGain);
    }

    isBurst = true;
    for (int i = 0; i < N; ++i) {
        out[i] = frame[i] * effectiveAttenuation;
    }

    return out;
}

/** @brief 初始化或重新配置流式处理状态。 */
void initializeStreamingState(TransientNoiseSuppressor::StreamingState& state,
                              int sampleRate,
                              const TransientNoiseSuppressor::Parameters& params)
{
    state.sampleRate = sampleRate;
    state.parameters = params;
    state.hopLength = params.hopLength;
    state.overlapLength = std::max(0, params.frameLength - params.hopLength);
    state.window = buildSqrtHannWindow(params.frameLength);
    state.analysisBuffer.clear();
    state.synthesisOverlap.fill(0.0, state.overlapLength);
    state.normalizationOverlap.fill(0.0, state.overlapLength);
    state.analysisFrame.fill(0.0f, params.frameLength);
    state.initialized = true;
}
} // namespace

TransientNoiseSuppressor::Parameters
TransientNoiseSuppressor::makeParameters(int sampleRate, int sampleCount)
{
    Parameters p;
    p.frameLength = std::max(128, static_cast<int>(std::lround(sampleRate * 0.021)));
    p.hopLength = p.frameLength / 2;
    return p;
}

QVector<float> TransientNoiseSuppressor::suppress(const QVector<float>& input, int sampleRate)
{
    Parameters params = makeParameters(sampleRate, input.size());
    return suppress(input, sampleRate, params);
}

QVector<float> TransientNoiseSuppressor::suppress(const QVector<float>& input, int sampleRate,
                                                  const Parameters& params)
{
    if (input.isEmpty()) return {};
    if (params.frameLength <= 0 || params.hopLength <= 0) return input;

    int leadPad = params.frameLength / 2;
    int procLen = input.size() + leadPad * 2;
    int frameCnt = std::max(1, 1 + (procLen - params.frameLength + params.hopLength - 1) / params.hopLength);
    int paddedLen = (frameCnt - 1) * params.hopLength + params.frameLength;

    QVector<double> padded(paddedLen, 0.0);
    for (int i = 0; i < leadPad; ++i) {
        int src = leadPad - i;
        padded[i] = static_cast<double>(input[std::min(src, static_cast<int>(input.size()) - 1)]);
    }
    for (int i = 0; i < input.size(); ++i)
        padded[leadPad + i] = static_cast<double>(input[i]);
    for (int i = leadPad + input.size(); i < procLen; ++i) {
        int dist = i - (leadPad + input.size()) + 1;
        int src = input.size() - 1 - dist;
        padded[i] = static_cast<double>(input[std::max(src, 0)]);
    }

    QVector<double> window = buildSqrtHannWindow(params.frameLength);
    QVector<double> outAcc(paddedLen, 0.0);
    QVector<double> normAcc(paddedLen, 0.0);
    QVector<float> analysisFrame(params.frameLength, 0.0f);

    const int energyBufferSize = 5;
    QVector<double> recentEnergy;
    int burstStreak = 0;
    int totalBurstFrames = 0;

    for (int fi = 0; fi < frameCnt; ++fi) {
        int fStart = fi * params.hopLength;
        for (int i = 0; i < params.frameLength; ++i)
            analysisFrame[i] = static_cast<float>(padded[fStart + i] * window[i]);

        double frameEnergy = 0.0;
        for (float s : analysisFrame) {
            double v = static_cast<double>(s);
            frameEnergy += v * v;
        }

        if (recentEnergy.size() >= energyBufferSize)
            recentEnergy.pop_front();
        recentEnergy.append(frameEnergy);
        double localMeanEnergy = 0.0;
        for (double e : recentEnergy) localMeanEnergy += e;
        localMeanEnergy /= recentEnergy.size();
        localMeanEnergy = std::max(localMeanEnergy, params.rmsFloor * params.frameLength);

        bool isBurst = false;
        QVector<float> suppressed = suppressFrame(analysisFrame, sampleRate, params,
                                                  localMeanEnergy, burstStreak, isBurst);

        if (isBurst) {
            ++burstStreak;
            ++totalBurstFrames;
        } else {
            burstStreak = 0;
        }

        for (int i = 0; i < params.frameLength; ++i) {
            int dest = fStart + i;
            double ws = static_cast<double>(suppressed[i]) * window[i];
            outAcc[dest] += ws;
            normAcc[dest] += window[i] * window[i];
        }
    }

    QVector<float> output(input.size(), 0.0f);
    for (int i = 0; i < input.size(); ++i) {
        int pi = leadPad + i;
        double norm = normAcc[pi];
        double restored = norm > kNormalizationEpsilon ? outAcc[pi] / norm : static_cast<double>(input[i]);
        output[i] = static_cast<float>(restored);
    }

    if (g_debugLoggingEnabled.load(std::memory_order_relaxed)) {
        double inRms = computeRms(input);
        double outRms = computeRms(output);
        double maxIn = 0.0, maxOut = 0.0;
        for (float v : input) maxIn = std::max(maxIn, static_cast<double>(std::abs(v)));
        for (float v : output) maxOut = std::max(maxOut, static_cast<double>(std::abs(v)));
        double attenDb = 10.0 * log10(std::max(outRms*outRms/(inRms*inRms+1e-12), 1e-12));

        qDebug().nospace()
            << "=== TransientNoiseSuppressor::suppress ==="
            << " Frames:" << frameCnt
            << " BurstFrames:" << totalBurstFrames
            << " RMS In:" << 20*log10(inRms) << "dB"
            << " Out:" << 20*log10(outRms) << "dB"
            << " Attenuation:" << attenDb << "dB"
            << " PeakRetention:" << (maxIn>1e-12?maxOut/maxIn:1.0);
    }

    return output;
}

QVector<float> TransientNoiseSuppressor::suppressStreaming(const QVector<float>& input,
                                                           int sampleRate,
                                                           StreamingState& state)
{
    Parameters params = makeParameters(sampleRate, input.size());
    return suppressStreaming(input, sampleRate, state, params);
}

QVector<float> TransientNoiseSuppressor::suppressStreaming(const QVector<float>& input,
                                                           int sampleRate,
                                                           StreamingState& state,
                                                           const Parameters& params)
{
    if (input.isEmpty()) return {};
    if (!state.initialized || state.sampleRate != sampleRate ||
        state.parameters.frameLength != params.frameLength ||
        state.parameters.hopLength != params.hopLength) {
        initializeStreamingState(state, sampleRate, params);
    }
    if (!state.initialized) return input;

    const int frameLen = params.frameLength;
    const int hopLen = params.hopLength;
    const int overlapLen = state.overlapLength;

    state.analysisBuffer.reserve(state.analysisBuffer.size() + input.size());
    for (float s : input) state.analysisBuffer.append(static_cast<double>(s));

    QVector<float> output;
    output.reserve(input.size());

    int burstStreak = 0;
    int totalBurstFrames = 0;
    double localMeanEnergy = 0.01;

    while (state.analysisBuffer.size() >= frameLen) {
        for (int i = 0; i < frameLen; ++i)
            state.analysisFrame[i] = static_cast<float>(state.analysisBuffer[i] * state.window[i]);

        double frameEnergy = 0.0;
        for (float s : state.analysisFrame) {
            double v = static_cast<double>(s);
            frameEnergy += v * v;
        }
        localMeanEnergy = 0.9 * localMeanEnergy + 0.1 * frameEnergy;

        bool isBurst = false;
        QVector<float> suppressed = suppressFrame(state.analysisFrame, sampleRate, params,
                                                  localMeanEnergy, burstStreak, isBurst);
        if (isBurst) {
            ++burstStreak;
            ++totalBurstFrames;
        } else {
            burstStreak = 0;
        }

        QVector<double> hopNum(hopLen, 0.0), hopDen(hopLen, 0.0);
        for (int i = 0; i < overlapLen; ++i) {
            hopNum[i] = state.synthesisOverlap[i];
            hopDen[i] = state.normalizationOverlap[i];
        }

        QVector<double> nextSyn(overlapLen, 0.0), nextNorm(overlapLen, 0.0);
        for (int i = 0; i < frameLen; ++i) {
            double ws = static_cast<double>(suppressed[i]) * state.window[i];
            double wE = state.window[i] * state.window[i];
            if (i < hopLen) {
                hopNum[i] += ws;
                hopDen[i] += wE;
            } else {
                int ovIdx = i - hopLen;
                if (ovIdx < overlapLen) {
                    nextSyn[ovIdx] += ws;
                    nextNorm[ovIdx] += wE;
                }
            }
        }

        for (int i = 0; i < hopLen; ++i) {
            double den = hopDen[i];
            double val = den > kNormalizationEpsilon ? hopNum[i] / den : 0.0;
            output.append(static_cast<float>(val));
        }

        state.synthesisOverlap = std::move(nextSyn);
        state.normalizationOverlap = std::move(nextNorm);
        state.analysisBuffer.erase(state.analysisBuffer.begin(),
                                   state.analysisBuffer.begin() + hopLen);
    }

    while (output.size() < input.size()) {
        int missing = input.size() - output.size();
        for (int i = 0; i < missing; ++i)
            output.append(input[input.size() - missing + i]);
    }
    if (output.size() > input.size()) output.resize(input.size());

    if (g_debugLoggingEnabled.load(std::memory_order_relaxed)) {
        static int callCount = 0;
        if (callCount == 0) {
            double inRms = computeRms(input);
            double outRms = computeRms(output);
            double maxIn = 0.0, maxOut = 0.0;
            for (float v : input) maxIn = std::max(maxIn, static_cast<double>(std::abs(v)));
            for (float v : output) maxOut = std::max(maxOut, static_cast<double>(std::abs(v)));
            double attenDb = 10.0 * log10(std::max(outRms*outRms/(inRms*inRms+1e-12), 1e-12));

            qDebug().nospace()
                << "=== TransientNoiseSuppressor::Stream ==="
                << " BurstFrames:" << totalBurstFrames
                << " RMS In:" << 20*log10(inRms) << "dB"
                << " Out:" << 20*log10(outRms) << "dB"
                << " Attenuation:" << attenDb << "dB"
                << " PeakRetention:" << (maxIn>1e-12?maxOut/maxIn:1.0);
        }
        callCount++;
    }

    return output;
}

void TransientNoiseSuppressor::resetStreamingState(StreamingState& state)
{
    state = StreamingState{};
}

void TransientNoiseSuppressor::setDebugLoggingEnabled(bool enabled)
{
    g_debugLoggingEnabled.store(enabled, std::memory_order_relaxed);
}

bool TransientNoiseSuppressor::debugLoggingEnabled()
{
    return g_debugLoggingEnabled.load(std::memory_order_relaxed);
}
