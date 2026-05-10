#include "MotionArtifactReduction.h"
#include "DataManager.h"
#include "FractalDimensionUtils.h"
#include "KfrDftUtils.h"

#include <QDebug>        // 用于量化输出
#include <algorithm>
#include <atomic>
#include <cmath>
#include <numeric>
#include <utility>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kMinimumPower = 1e-12;
constexpr double kNormalizationEpsilon = 1e-8;

std::atomic_bool g_debugLoggingEnabled = true;

struct EmdDecomposition
{
    QVector<QVector<double>> imfs;
    QVector<double> residual;
};

// ---------- 辅助工具 ----------
int normalizeSampleRate(int sampleRate)
{
    if (sampleRate > 0) return sampleRate;
    return DataManager::DEFAULT_SAMPLE_RATE;
}

int nextPowerOfTwo(int value)
{
    if (value <= 1) return 1;
    int power = 1;
    while (power < value) power <<= 1;
    return power;
}

QVector<double> buildSqrtHannWindow(int frameLength)
{
    QVector<double> window(frameLength, 1.0);
    if (frameLength <= 1) return window;
    for (int i = 0; i < frameLength; ++i) {
        const double phase = (2.0 * kPi * i) / (frameLength - 1);
        const double hann = 0.5 * (1.0 - std::cos(phase));
        window[i] = std::sqrt(std::max(hann, 0.0));
    }
    return window;
}

double computeRms(const QVector<float>& samples)
{
    if (samples.isEmpty()) return 0.0;
    double energy = 0.0;
    for (float s : samples) {
        double v = std::isfinite(s) ? static_cast<double>(s) : 0.0;
        energy += v * v;
    }
    return std::sqrt(energy / samples.size());
}

double computeCrestFactor(const QVector<float>& samples)
{
    double rms = computeRms(samples);
    if (rms <= kMinimumPower) return 0.0;
    double peak = 0.0;
    for (float s : samples)
        peak = std::max(peak, std::abs(static_cast<double>(s)));
    return peak / rms;
}

// ---------- EMD 相关（未修改核心算法）----------
QVector<int> findLocalExtremaIndices(const QVector<double>& signal, bool maxima)
{
    QVector<int> indices;
    if (signal.size() < 3) return indices;
    for (int i = 1; i < signal.size() - 1; ++i) {
        double prev = signal[i-1], curr = signal[i], next = signal[i+1];
        bool extremum = maxima
                            ? ((curr >= prev && curr > next) || (curr > prev && curr >= next))
                            : ((curr <= prev && curr < next) || (curr < prev && curr <= next));
        if (extremum) indices.append(i);
    }
    return indices;
}

int countLocalExtrema(const QVector<double>& signal)
{
    return findLocalExtremaIndices(signal, true).size() +
           findLocalExtremaIndices(signal, false).size();
}

QVector<int> buildSupportIndices(const QVector<double>& signal, const QVector<int>& extrema)
{
    QVector<int> support;
    if (signal.isEmpty()) return support;
    support.reserve(extrema.size() + 2);
    support.append(0);
    for (int idx : extrema) {
        if (idx > 0 && idx < signal.size() - 1 && support.last() != idx)
            support.append(idx);
    }
    if (support.last() != signal.size() - 1)
        support.append(signal.size() - 1);
    return support;
}

QVector<double> buildShapePreservingEnvelope(const QVector<double>& signal, const QVector<int>& support)
{
    QVector<double> envelope(signal.size(), 0.0);
    if (signal.isEmpty() || support.isEmpty()) return envelope;
    if (support.size() == 1) {
        std::fill(envelope.begin(), envelope.end(), signal[support.first()]);
        return envelope;
    }

    QVector<double> slopes(support.size(), 0.0);
    QVector<double> segSlopes(std::max(0, (int)support.size()-1), 0.0);
    for (int i = 0; i < support.size()-1; ++i) {
        int left = support[i], right = support[i+1];
        if (right <= left) continue;
        segSlopes[i] = (signal[right] - signal[left]) / (right - left);
    }

    slopes.first() = segSlopes.first();
    slopes.last() = segSlopes.last();
    for (int i = 1; i < support.size()-1; ++i) {
        double p = segSlopes[i-1], n = segSlopes[i];
        slopes[i] = (p * n <= 0.0) ? 0.0 : (2.0 * p * n / (p + n));
    }

    const int firstIdx = support.first();
    for (int i = 0; i <= firstIdx && i < envelope.size(); ++i)
        envelope[i] = signal[firstIdx];

    for (int i = 0; i < support.size()-1; ++i) {
        int left = support[i], right = support[i+1];
        if (right <= left) continue;
        double Lv = signal[left], Rv = signal[right];
        double len = right - left;
        double lowB = std::min(Lv, Rv), upB = std::max(Lv, Rv);
        for (int j = left; j <= right; ++j) {
            double t = (double)(j - left) / len;
            double t2 = t*t, t3 = t2*t;
            double h00 = 2.0*t3 - 3.0*t2 + 1.0;
            double h10 = t3 - 2.0*t2 + t;
            double h01 = -2.0*t3 + 3.0*t2;
            double h11 = t3 - t2;
            double interp = h00*Lv + h10*len*slopes[i] + h01*Rv + h11*len*slopes[i+1];
            envelope[j] = std::clamp(interp, lowB, upB);
        }
    }

    const int lastIdx = support.last();
    for (int i = lastIdx; i < envelope.size(); ++i)
        envelope[i] = signal[lastIdx];
    return envelope;
}

QVector<double> siftImf(const QVector<double>& residual, const MotionArtifactReduction::Parameters& params)
{
    QVector<double> mode = residual;
    for (int iter = 0; iter < params.maxSiftIterations; ++iter) {
        QVector<int> maxIdx = findLocalExtremaIndices(mode, true);
        QVector<int> minIdx = findLocalExtremaIndices(mode, false);
        if (maxIdx.isEmpty() || minIdx.isEmpty() || maxIdx.size()+minIdx.size() < params.minimumExtremaCount)
            break;
        QVector<double> up = buildShapePreservingEnvelope(mode, buildSupportIndices(mode, maxIdx));
        QVector<double> low = buildShapePreservingEnvelope(mode, buildSupportIndices(mode, minIdx));
        if (up.size() != mode.size() || low.size() != mode.size()) break;

        QVector<double> newMode(mode.size());
        double meanEnvE = 0.0, modeE = 0.0;
        for (int i = 0; i < mode.size(); ++i) {
            double m = 0.5 * (up[i] + low[i]);
            newMode[i] = mode[i] - m;
            meanEnvE += m*m;
            modeE += newMode[i]*newMode[i];
        }
        mode.swap(newMode);
        double ratio = meanEnvE / std::max(modeE, kMinimumPower);
        if (ratio < params.minimumImprovementRatio) break;
    }
    return mode;
}

EmdDecomposition decomposeSignal(const QVector<float>& samples,
                                 const MotionArtifactReduction::Parameters& params)
{
    EmdDecomposition dec;
    if (samples.isEmpty()) return dec;
    dec.residual.resize(samples.size());
    std::transform(samples.cbegin(), samples.cend(), dec.residual.begin(),
                   [](float v) { return static_cast<double>(v); });

    for (int i = 0; i < params.maxImfCount; ++i) {
        if (countLocalExtrema(dec.residual) < params.minimumExtremaCount) break;
        QVector<double> imf = siftImf(dec.residual, params);
        if (imf.size() != dec.residual.size()) break;
        double energy = 0.0;
        for (double v : imf) energy += v*v;
        if (energy <= kMinimumPower) break;
        dec.imfs.append(imf);
        for (int j = 0; j < dec.residual.size(); ++j)
            dec.residual[j] -= imf[j];
    }
    return dec;
}

// ---------- 特征计算（优化）----------
double computeBandEnergyRatio(const QVector<double>& imf, int sampleRate,
                              const MotionArtifactReduction::Parameters& params)
{
    if (imf.isEmpty()) return 0.0;
    // 转为 float 做频谱
    QVector<float> imfF(imf.size());
    std::transform(imf.cbegin(), imf.cend(), imfF.begin(),
                   [](double v) { return static_cast<float>(v); });
    KfrDftUtils::RealDftSpectrumResult specRes =
        KfrDftUtils::computeRealSpectrumMagnitudes(imfF, KfrDftUtils::WindowFunction::Hann, true);
    if (specRes.magnitudes.isEmpty() || specRes.fftSize <= 0) return 0.0;
    double freqRes = (double)sampleRate / specRes.fftSize;
    double totalE = 0.0, inBandE = 0.0;
    for (int k = 0; k < specRes.magnitudes.size(); ++k) {
        double mag = static_cast<double>(specRes.magnitudes[k]);
        double power = mag * mag;
        totalE += power;
        double freqHz = k * freqRes;
        if (freqHz >= params.lowerFrequencyHz && freqHz <= params.upperFrequencyHz)
            inBandE += power;
    }
    return totalE > kMinimumPower ? inBandE / totalE : 0.0;
}

double medianValue(QVector<double> values)
{
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    int mid = values.size() / 2;
    if (values.size() % 2 == 0)
        return 0.5 * (values[mid-1] + values[mid]);
    return values[mid];
}

double computeFdScore(const QVector<double>& imf,
                      const MotionArtifactReduction::Parameters& params)
{
    if (imf.isEmpty()) return 0.0;
    const int N = imf.size();
    const int win = std::clamp(params.fdWindowSamples, 2, std::max(2, N));
    const int hop = std::clamp(params.fdHopSamples, 1, win);
    if (N <= win)
        return FractalDimensionUtils::computeWaveformComplexity(imf);

    QVector<double> scores;
    for (int start = 0; start + win <= N; start += hop) {
        QVector<double> frame(win);
        for (int i = 0; i < win; ++i) frame[i] = imf[start+i];
        scores.append(FractalDimensionUtils::computeWaveformComplexity(frame));
    }
    // 尾部补一帧
    int tail = N - win;
    if (tail >= 0 && (scores.isEmpty() || tail != (scores.size()-1)*hop)) {
        QVector<double> frame(win);
        for (int i = 0; i < win; ++i) frame[i] = imf[tail+i];
        scores.append(FractalDimensionUtils::computeWaveformComplexity(frame));
    }
    if (scores.isEmpty()) return FractalDimensionUtils::computeWaveformComplexity(imf);
    double sum = std::accumulate(scores.cbegin(), scores.cend(), 0.0);
    return sum / scores.size();
}

// 平滑伪迹评分（在 reduceFrame 内使用，对 artifactScore 做帧间平滑？这里只提供函数）
double smoothArtifactScore(double bandRatio, double fdScore, double fdThreshold,
                           const MotionArtifactReduction::Parameters& params)
{
    double bandScore = std::clamp(
        (bandRatio - params.candidateBandEnergyRatio) / std::max(params.softDecisionWidth, 0.01),
        0.0, 1.0);
    if (bandScore <= 0.0) return 0.0;
    double fdNorm = std::clamp(
        (fdThreshold - fdScore) / std::max(fdThreshold - params.fdThresholdMinimum, 0.03),
        0.0, 1.0);
    double score = bandScore * (0.58 + 0.42 * fdNorm);
    return std::clamp(score, 0.0, 1.0);
}

// ---------- 核心帧处理（大幅优化）----------
QVector<float> reduceFrame(const QVector<float>& frame, int sampleRate,
                           const MotionArtifactReduction::Parameters& params)
{
    if (frame.size() < 16) return frame;

    // ---------- 瞬态保护：极高的波峰因子直接判定为异常 ----------
    double crest = computeCrestFactor(frame);
    if (crest > params.transientCrestThreshold) {
        // 大幅衰减整帧，保留微弱肠鸣音（衰减 10dB）
        QVector<float> out(frame.size());
        for (int i = 0; i < frame.size(); ++i) out[i] = frame[i] * 0.25f;
        return out;
    }

    EmdDecomposition dec = decomposeSignal(frame, params);
    if (dec.imfs.isEmpty()) return frame;

    // 仅对最后低频的几个 IMF 进行运动伪迹检测（通常后 4 个）
    const int totalImfs = dec.imfs.size();
    const int firstArtifactImf = std::max(0, totalImfs - params.maxArtifactImfCount);
    // 被检测的 IMF 索引从 firstArtifactImf 到 totalImfs-1

    QVector<double> bandRatios(totalImfs, 0.0);
    QVector<double> fdScores(totalImfs, 0.0);
    QVector<double> candidateFdPool;

    for (int i = 0; i < totalImfs; ++i) {
        bandRatios[i] = computeBandEnergyRatio(dec.imfs[i], sampleRate, params);
        // 仅对候选低频带且频带能量比达标者计算 FD
        if (i >= firstArtifactImf && bandRatios[i] >= params.candidateBandEnergyRatio) {
            fdScores[i] = computeFdScore(dec.imfs[i], params);
            candidateFdPool.append(fdScores[i]);
        }
    }

    // 若没找到可疑IMF，直接返回原信号
    if (candidateFdPool.isEmpty()) return frame;

    // 改用稳健的阈值：中位数 - 1.5 * MAD，或直接使用预设上限
    double fdMedian = medianValue(candidateFdPool);
    // 计算 MAD
    QVector<double> absDev(candidateFdPool.size());
    for (int i = 0; i < candidateFdPool.size(); ++i)
        absDev[i] = std::abs(candidateFdPool[i] - fdMedian);
    double mad = medianValue(absDev);
    double fdThreshold = std::clamp(
        fdMedian - 1.5 * mad,
        params.fdThresholdMinimum, params.fdThresholdMaximum);
    // 若中位数偏离严重，退回安全值
    if (fdThreshold < params.fdThresholdAbsoluteMin)
        fdThreshold = params.fdThresholdAbsoluteMin;

    // 重构信号：residual 加回 IMF，对低频可疑 IMF 根据评分衰减
    QVector<double> reconstructed = dec.residual;
    if (reconstructed.size() != frame.size()) reconstructed.fill(0.0, frame.size());

    int processedArtifactImfs = 0;
    for (int i = 0; i < totalImfs; ++i) {
        double scale = 1.0;
        if (i >= firstArtifactImf) {
            // 计算该 IMF 的伪迹评分
            double score = smoothArtifactScore(bandRatios[i], fdScores[i], fdThreshold, params);
            // 评分向衰减因子映射
            scale = 1.0 - score * (1.0 - params.attenuationFactor);
            if (score > 0.1) processedArtifactImfs++;
        }
        const QVector<double>& imf = dec.imfs[i];
        for (int j = 0; j < reconstructed.size(); ++j)
            reconstructed[j] += scale * imf[j];
    }

    QVector<float> reduced(reconstructed.size());
    std::transform(reconstructed.cbegin(), reconstructed.cend(), reduced.begin(),
                   [](double v) { return static_cast<float>(v); });

    // 帧级 RMS 保护
    double inRms = computeRms(frame);
    double outRms = computeRms(reduced);
    double minOutRms = inRms * std::clamp(params.minimumOutputRmsRatio, 0.2, 0.95);
    if (inRms > kMinimumPower && outRms < minOutRms) {
        double blend = std::clamp((minOutRms - outRms) / std::max(minOutRms, kMinimumPower), 0.0, 0.55);
        for (int i = 0; i < reduced.size(); ++i)
            reduced[i] = (1.0-blend)*reduced[i] + blend*frame[i];
    }

    if (g_debugLoggingEnabled.load(std::memory_order_relaxed) && processedArtifactImfs > 0) {
        double peakIn = 0.0, peakOut = 0.0;
        for (float v : frame) peakIn = qMax(peakIn, std::abs(v));
        for (float v : reduced) peakOut = qMax(peakOut, std::abs(v));
        qDebug().nospace()
            << "[MAFrame] crest=" << crest
            << " bandIdx=" << firstArtifactImf
            << " #imfs=" << totalImfs
            << " #atten=" << processedArtifactImfs
            << " fdThr=" << fdThreshold
            << " atten=" << (10.0*log10(std::max(outRms*outRms/(inRms*inRms+1e-12),1e-12)))
            << "dB pkKeep=" << (peakIn>1e-12?peakOut/peakIn:1.0);
    }
    return reduced;
}

// ---------- 流式状态初始化 ----------
void initializeStreamingState(MotionArtifactReduction::StreamingState& state,
                              int sampleRate, const MotionArtifactReduction::Parameters& params)
{
    state.sampleRate = normalizeSampleRate(sampleRate);
    state.parameters = params;
    int frameLen = params.frameLength;
    int hopLen = params.hopLength;
    int overlap = std::max(0, frameLen - hopLen);
    state.window = buildSqrtHannWindow(frameLen);
    state.analysisBuffer.clear();
    state.synthesisOverlap.fill(0.0, overlap);
    state.normalizationOverlap.fill(0.0, overlap);
    state.analysisFrame.fill(0.0f, frameLen);
    state.initialized = frameLen > 0 && hopLen > 0;
}
} // namespace

// ========== 公有方法实现 ==========

MotionArtifactReduction::Parameters MotionArtifactReduction::makeParameters(
    int sampleRate, int sampleCount)
{
    Parameters params;
    if (sampleCount <= 0) return params;

    const int targetFrameLen = std::max(256, static_cast<int>(std::lround(sampleRate * 0.12)));
    const int upperLimit = sampleRate >= 100000 ? 4096 : 2048;
    params.frameLength = std::clamp(nextPowerOfTwo(targetFrameLen), 256, upperLimit);
    if (sampleCount < params.frameLength / 2)
        params.frameLength = std::clamp(nextPowerOfTwo(std::max(sampleCount, 64)), 128, params.frameLength);

    params.hopLength = std::max(64, params.frameLength / 2);
    params.fdWindowSamples = std::clamp(static_cast<int>(std::lround(sampleRate * 0.04)), 16, params.frameLength);
    params.fdHopSamples = std::clamp(static_cast<int>(std::lround(sampleRate * 0.02)), 8, params.fdWindowSamples);

    // ---------- 运动伪迹特定参数（显式设置）----------
    params.lowerFrequencyHz = 0.5;          // 伪迹低频下限
    params.upperFrequencyHz = 25.0;         // 伪迹低频上限
    params.candidateBandEnergyRatio = 0.35; // 频带能量占比阈值
    params.softDecisionWidth = 0.25;        // 评分映射宽度
    params.fdThresholdMinimum = 1.05;       // 分形维度下限（平滑信号）
    params.fdThresholdMaximum = 1.40;       // 分形维度上限（复杂信号）
    params.fdThresholdAbsoluteMin = 1.10;   // 绝对最低安全阈值
    params.maxArtifactImfCount = 4;         // 后 4 个 IMF 才可能是伪迹
    params.transientCrestThreshold = 10.0;  // 波峰因子阈值
    params.attenuationFactor = 0.08;        // 伪迹衰减保留比例（-22dB）
    params.minimumOutputRmsRatio = 0.45;    // RMS 保护更宽松

    return params;
}

// ---------- 批量处理 ----------
QVector<float> MotionArtifactReduction::reduce(const QVector<float>& input, int sampleRate)
{
    Parameters params = makeParameters(normalizeSampleRate(sampleRate), input.size());
    return reduce(input, sampleRate, params);
}

QVector<float> MotionArtifactReduction::reduce(const QVector<float>& input, int sampleRate,
                                               const Parameters& params)
{
    if (input.isEmpty()) return {};
    if (params.frameLength <= 0 || params.hopLength <= 0) return input;

    int effSR = normalizeSampleRate(sampleRate);
    int leadPad = params.frameLength / 2;
    int procLen = input.size() + leadPad * 2;
    int frameCnt = std::max(1, 1 + std::max(0, procLen - params.frameLength + params.hopLength - 1) / params.hopLength);
    int paddedLen = (frameCnt - 1) * params.hopLength + params.frameLength;

    // ---------- 改进边界延拓：镜像对称 ----------
    QVector<double> padded(paddedLen, 0.0);
    // 前延拓
    for (int i = 0; i < leadPad; ++i) {
        int src = leadPad - i;   // 镜像反射
        padded[i] = static_cast<double>(input[qMin(src, input.size()-1)]);
    }
    // 主数据
    for (int i = 0; i < input.size(); ++i)
        padded[leadPad + i] = static_cast<double>(input[i]);
    // 后延拓
    int endStart = leadPad + input.size();
    for (int i = endStart; i < procLen; ++i) {
        int dist = i - endStart + 1;
        int src = input.size() - 1 - dist;
        padded[i] = static_cast<double>(input[std::max(src, 0)]);
    }

    QVector<double> window = buildSqrtHannWindow(params.frameLength);
    QVector<double> outAcc(paddedLen, 0.0), normAcc(paddedLen, 0.0);
    QVector<float> analysisFrame(params.frameLength, 0.0f);

    for (int fi = 0; fi < frameCnt; ++fi) {
        int fStart = fi * params.hopLength;
        for (int i = 0; i < params.frameLength; ++i)
            analysisFrame[i] = static_cast<float>(padded[fStart + i] * window[i]);

        QVector<float> reducedFrame = reduceFrame(analysisFrame, effSR, params);
        if (reducedFrame.size() != params.frameLength) continue;

        for (int i = 0; i < params.frameLength; ++i) {
            int dest = fStart + i;
            double ws = static_cast<double>(reducedFrame[i]) * window[i];
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
        for (float v : input) maxIn = qMax(maxIn, std::abs(v));
        for (float v : output) maxOut = qMax(maxOut, std::abs(v));
        double attenDb = 10.0 * log10(std::max(outRms*outRms/(inRms*inRms+1e-12), 1e-12));
        qDebug().nospace()
            << "=== MotionArtifact::reduce ==="
            << " Frames:" << frameCnt
            << " RMS In:" << 20*log10(inRms) << "dB"
            << " Out:" << 20*log10(outRms) << "dB"
            << " Attenuation:" << attenDb << "dB"
            << " PeakRetention:" << (maxIn>1e-12?maxOut/maxIn:1.0);
    }

    return output;
}

// ---------- 流式处理（与原始结构一致，内部调用优化后的 reduceFrame）----------
QVector<float> MotionArtifactReduction::reduceStreaming(const QVector<float>& input, int sampleRate,
                                                        StreamingState& state)
{
    Parameters params = makeParameters(normalizeSampleRate(sampleRate), qMax(input.size(), 256));
    return reduceStreaming(input, sampleRate, state, params);
}

QVector<float> MotionArtifactReduction::reduceStreaming(const QVector<float>& input, int sampleRate,
                                                        StreamingState& state, const Parameters& params)
{
    if (input.isEmpty()) return {};
    int effSR = normalizeSampleRate(sampleRate);
    if (!state.initialized || state.sampleRate != effSR ||
        state.parameters.frameLength != params.frameLength ||
        state.parameters.hopLength != params.hopLength) {
        initializeStreamingState(state, effSR, params);
    }
    if (!state.initialized) return input;

    int frameLen = params.frameLength;
    int hopLen = params.hopLength;
    int overlapLen = std::max(0, frameLen - hopLen);

    state.analysisBuffer.reserve(state.analysisBuffer.size() + input.size());
    for (float s : input) state.analysisBuffer.append(static_cast<double>(s));

    QVector<float> output;
    output.reserve(input.size());

    while (state.analysisBuffer.size() >= frameLen) {
        for (int i = 0; i < frameLen; ++i)
            state.analysisFrame[i] = static_cast<float>(state.analysisBuffer[i] * state.window[i]);

        QVector<float> reducedFrame = reduceFrame(state.analysisFrame, effSR, state.parameters);
        if (reducedFrame.size() != frameLen) break;

        QVector<double> hopNum(hopLen, 0.0), hopDen(hopLen, 0.0);
        for (int i = 0; i < overlapLen; ++i) {
            hopNum[i] = state.synthesisOverlap[i];
            hopDen[i] = state.normalizationOverlap[i];
        }

        QVector<double> nextSyn(overlapLen, 0.0), nextNorm(overlapLen, 0.0);
        for (int i = 0; i < frameLen; ++i) {
            double ws = static_cast<double>(reducedFrame[i]) * state.window[i];
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
        state.analysisBuffer.erase(state.analysisBuffer.begin(), state.analysisBuffer.begin() + hopLen);
    }

    // 剩余样本直接复制
    while (output.size() < input.size()) {
        int missing = input.size() - output.size();
        for (int i = 0; i < missing; ++i)
            output.append(input[input.size() - missing + i]);
    }
    if (output.size() > input.size()) output.resize(input.size());

    if (g_debugLoggingEnabled.load(std::memory_order_relaxed)) {
        double inRms = computeRms(input);
        double outRms = computeRms(output);
        double maxIn = 0.0, maxOut = 0.0;
        for (float v : input) maxIn = qMax(maxIn, std::abs(v));
        for (float v : output) maxOut = qMax(maxOut, std::abs(v));
        double attenDb = 10.0 * log10(qMax(outRms*outRms/(inRms*inRms+1e-12), 1e-12));
        qDebug().nospace()
            << "=== MotionArtifact::reduceStreaming ==="
            << " RMS In:" << 20*log10(inRms) << "dB"
            << " Out:" << 20*log10(outRms) << "dB"
            << " Attenuation:" << attenDb << "dB"
            << " PeakRetention:" << (maxIn>1e-12?maxOut/maxIn:1.0);
    }

    return output;
}

void MotionArtifactReduction::resetStreamingState(StreamingState& state)
{
    state = StreamingState{};
}

void MotionArtifactReduction::setDebugLoggingEnabled(bool enabled)
{
    g_debugLoggingEnabled.store(enabled, std::memory_order_relaxed);
}

bool MotionArtifactReduction::debugLoggingEnabled()
{
    return g_debugLoggingEnabled.load(std::memory_order_relaxed);
}