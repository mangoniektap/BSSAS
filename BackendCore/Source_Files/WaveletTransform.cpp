#include "WaveletTransform.h"
#include "DataManager.h"

#include <QDebug>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>

namespace {
constexpr int kRequestedLevels = 5;
constexpr int kFilterLength = 12;
constexpr double kMadScale = 0.6745;
constexpr double kVarianceEpsilon = 1e-12;

std::atomic_bool g_debugLoggingEnabled = true;

using FilterTaps = std::array<double, kFilterLength>;

// ---------- Symlet 6 滤波器系数 (不变) ----------
constexpr FilterTaps kSym6DecLo = {
    0.015404109327027373,
    0.0034907120842174702,
    -0.11799011114819057,
    -0.048311742585633,
    0.4910559419267466,
    0.787641141030194,
    0.3379294217276218,
    -0.07263752278646252,
    -0.021060292512300564,
    0.04472490177066578,
    0.0017677118642428036,
    -0.007800708325034148
};

constexpr FilterTaps kSym6DecHi = {
    -0.007800708325034148,
    -0.0017677118642428036,
    0.04472490177066578,
    0.021060292512300564,
    -0.07263752278646252,
    -0.3379294217276218,
    0.787641141030194,
    -0.4910559419267466,
    -0.048311742585633,
    0.11799011114819057,
    0.0034907120842174702,
    -0.015404109327027373
};

constexpr FilterTaps kSym6RecLo = {
    -0.007800708325034148,
    0.0017677118642428036,
    0.04472490177066578,
    -0.021060292512300564,
    -0.07263752278646252,
    0.3379294217276218,
    0.787641141030194,
    0.4910559419267466,
    -0.048311742585633,
    -0.11799011114819057,
    0.0034907120842174702,
    0.015404109327027373
};

constexpr FilterTaps kSym6RecHi = {
    -0.015404109327027373,
    0.0034907120842174702,
    0.11799011114819057,
    -0.048311742585633,
    -0.4910559419267466,
    0.787641141030194,
    -0.3379294217276218,
    -0.07263752278646252,
    0.021060292512300564,
    0.04472490177066578,
    -0.0017677118642428036,
    -0.007800708325034148
};

// ---------- 辅助函数 (大多不变) ----------
int normalizeSampleRate(int sampleRate)
{
    if (sampleRate > 0) return sampleRate;
    qWarning() << "WaveletTransform: invalid sample rate" << sampleRate
               << ", fallback to" << DataManager::DEFAULT_SAMPLE_RATE;
    return DataManager::DEFAULT_SAMPLE_RATE;
}

int symmetricIndex(int index, int size)
{
    if (size <= 1) return 0;
    while (index < 0 || index >= size) {
        if (index < 0) index = -index - 1;
        else           index = 2 * size - index - 1;
    }
    return index;
}

// ---------- SWT 分解/重构 (不变) ----------
void decomposeLevelStationary(
    const QVector<double>& signal, int dilation,
    QVector<double>& approx, QVector<double>& detail)
{
    if (signal.isEmpty()) { approx.clear(); detail.clear(); return; }
    const int N = signal.size();
    approx.resize(N);
    detail.resize(N);
    const double* s = signal.constData();
    double* a = approx.data();
    double* d = detail.data();
    for (int i = 0; i < N; ++i) {
        double lo = 0.0, hi = 0.0;
        for (int k = 0; k < kFilterLength; ++k) {
            int idx = symmetricIndex(i - dilation * k, N);
            double v = s[idx];
            lo += v * kSym6DecLo[k];
            hi += v * kSym6DecHi[k];
        }
        a[i] = lo;
        d[i] = hi;
    }
}

QVector<double> reconstructLevelStationary(
    const QVector<double>& approx, const QVector<double>& detail, int dilation)
{
    const int N = std::min(approx.size(), detail.size());
    if (N <= 0) return {};
    QVector<double> rec(N);
    const double* a = approx.constData();
    const double* d = detail.constData();
    double* r = rec.data();
    for (int i = 0; i < N; ++i) {
        double lo = 0.0, hi = 0.0;
        for (int k = 0; k < kFilterLength; ++k) {
            int idx = symmetricIndex(i + dilation * k, N);
            lo += a[idx] * kSym6RecLo[k];
            hi += d[idx] * kSym6RecHi[k];
        }
        r[i] = 0.5 * (lo + hi);
    }
    return rec;
}

// ---------- 中位数与稳健噪声估计 ----------
double medianAbsoluteValue(QVector<double> absVals)
{
    if (absVals.isEmpty()) return 0.0;
    const int mid = absVals.size() / 2;
    auto midIt = absVals.begin() + mid;
    std::nth_element(absVals.begin(), midIt, absVals.end());
    double upper = *midIt;
    if (absVals.size() % 2 == 0) {
        double lower = *std::max_element(absVals.begin(), midIt);
        return 0.5 * (lower + upper);
    }
    return upper;
}

// 迭代稳健噪声标准差估计：剔除超过 3σ 的点后重新计算 MAD
double estimateNoiseSigmaRobust(const QVector<double>& detail)
{
    if (detail.isEmpty()) return 0.0;
    QVector<double> absVals(detail.size());
    for (int i = 0; i < detail.size(); ++i)
        absVals[i] = std::abs(detail[i]);

    // 第一次估计
    double med = medianAbsoluteValue(absVals);
    double sigma = med / kMadScale;

    // 剔除野值
    double threshold = 3.0 * sigma;
    QVector<double> refined;
    refined.reserve(absVals.size());
    for (double v : absVals)
        if (v < threshold) refined.push_back(v);

    // 如果剔掉太多，保留原估计
    if (refined.size() < detail.size() * 0.3)
        return sigma;

    med = medianAbsoluteValue(refined);
    return med / kMadScale;
}

// ---------- 频带自适应权重 (12kHz 采样率, Nyquist 6kHz) ----------
// 各层频带：
// Level 0: 3 - 6 kHz    (噪声为主)
// Level 1: 1.5 - 3 kHz  (摩擦/环境)
// Level 2: 750 - 1500 Hz (边缘)
// Level 3: 375 - 750 Hz  (肠鸣音核心)
// Level 4: 187.5 - 375 Hz(低频肠鸣音/呼吸)
// 权重越小，阈值越低，保留越多。
constexpr std::array<double, kRequestedLevels> kBowelWeights = {
    1.6,   // Level 0：强力抑制高频噪声
    1.2,   // Level 1：较强抑制
    0.6,   // Level 2：稍微保留
    0.3,   // Level 3：重点保留（核心频段）
    0.5    // Level 4：适度保留，避免呼吸声等低频干扰
};

double levelThresholdWeight(int levelIndex)
{
    if (levelIndex < 0) return 1.0;
    if (levelIndex >= static_cast<int>(kBowelWeights.size()))
        return kBowelWeights.back();
    return kBowelWeights[levelIndex];
}

// ---------- 改进的阈值收缩 (Garrote 替代纯软阈值) ----------
void applyGarroteShrinkage(QVector<double>& detail, double threshold,
                           const QVector<bool>& mask = QVector<bool>())
{
    if (detail.isEmpty() || threshold <= 0.0) return;
    if (!std::isfinite(threshold) || threshold >= std::numeric_limits<double>::max()) {
        std::fill(detail.begin(), detail.end(), 0.0);
        return;
    }

    const double T2 = threshold * threshold;
    for (int i = 0; i < detail.size(); ++i) {
        // 若处于局部脉冲保护区，完全不处理（增益=1）
        if (!mask.isEmpty() && mask[i]) continue;

        double& w = detail[i];
        double absW = std::abs(w);
        if (absW <= threshold) {
            w = 0.0;
        } else {
            // Garrote 收缩：w' = w - T^2 / w
            w -= std::copysign(T2 / absW, w);
        }
    }
}

// ---------- 小波域局部脉冲检测 (保护肠鸣音短时强脉冲) ----------
// 在细节系数上：若某点处能量远高于局部中位绝对偏差，则认为是脉冲
QVector<bool> detectLocalPulses(const QVector<double>& detail, double localWindow = 31)
{
    const int N = detail.size();
    QVector<bool> mask(N, false);
    if (N < 3) return mask;

    // 计算局部中位绝对值 (近似)
    for (int i = 0; i < N; ++i) {
        int left = std::max(0, i - static_cast<int>(localWindow/2));
        int right = std::min(N-1, i + static_cast<int>(localWindow/2));
        // 简易中位数：用前向排序窗口可优化，这里采用复制子段求中位
        QVector<double> window(right - left + 1);
        for (int j = left; j <= right; ++j)
            window[j - left] = std::abs(detail[j]);
        double localMed = medianAbsoluteValue(window);

        // 若当前幅度 > 5倍局部中位值，标记为脉冲（肠鸣音）
        if (std::abs(detail[i]) > 5.0 * localMed) {
            mask[i] = true;
        }
    }
    return mask;
}

// ---------- RMS 保护 (防止过度降噪) ----------
double computeRms(const QVector<double>& values)
{
    if (values.isEmpty()) return 0.0;
    double energy = 0.0;
    for (double v : values) energy += v * v;
    return std::sqrt(energy / values.size());
}

void protectOutputRms(const QVector<double>& input, QVector<double>& denoised)
{
    if (input.size() != denoised.size() || input.isEmpty()) return;
    const double inRms = computeRms(input);
    const double outRms = computeRms(denoised);
    // 最小输出 RMS 设为输入的 0.65 倍 (平衡噪声残留与信号保真)
    const double minOutRms = inRms * 0.65;
    if (inRms <= kVarianceEpsilon || outRms >= minOutRms) return;

    double blend = std::clamp((minOutRms - outRms) / std::max(minOutRms, kVarianceEpsilon), 0.0, 0.55);
    for (int i = 0; i < denoised.size(); ++i)
        denoised[i] = (1.0 - blend) * denoised[i] + blend * input[i];
}

// ---------- 时域峰值保护 (调整权重，保留更大部分原信号) ----------
void preserveTransientPeaks(
    const QVector<double>& input, QVector<double>& denoised, double noiseSigma)
{
    if (input.size() != denoised.size() || input.size() < 3) return;

    const double rms = computeRms(input);
    const double peakThreshold = std::max(3.0 * noiseSigma, 2.0 * rms);
    if (peakThreshold <= kVarianceEpsilon) return;

    for (int i = 1; i < input.size() - 1; ++i) {
        double mag = std::abs(input[i]);
        if (mag >= std::abs(input[i-1]) && mag >= std::abs(input[i+1]) && mag >= peakThreshold) {
            for (int o = -1; o <= 1; ++o) {
                int idx = i + o;
                // 增大原信号保留比例 (从原来的 0.15 提高到 0.3)
                denoised[idx] = 0.3 * input[idx] + 0.7 * denoised[idx];
            }
        }
    }
}



// ---------- 分解层数确定 (不变) ----------
int resolveSwtDecompositionLevelsByLength(int inputLength)
{
    int levels = 0;
    while (levels < kRequestedLevels) {
        int dilation = 1 << levels;
        if (dilation * (kFilterLength - 1) + 1 > inputLength) break;
        ++levels;
    }
    return levels;
}

int resolveSwtDecompositionLevels(int inputLength, int sampleRate)
{
    int levelByLen = resolveSwtDecompositionLevelsByLength(inputLength);
    int levelByBand = 0;
    int bandUpperHz = sampleRate / 2;
    while (levelByBand < kRequestedLevels && bandUpperHz > 0) {
        ++levelByBand;
        bandUpperHz /= 2;
    }
    return std::min(levelByLen, levelByBand);
}

int resolveSwtBoundaryLength(int inputLength, int sampleRate)
{
    const int levels = resolveSwtDecompositionLevels(inputLength, sampleRate);
    if (levels <= 0) return 0;
    int radius = 0;
    for (int lv = 0; lv < levels; ++lv)
        radius += (kFilterLength - 1) * (1 << lv);
    return 2 * radius;
}

// ---------- 核心去噪流程 (优化后) ----------
QVector<double> denoiseStationary(const QVector<double>& inputSignal, int decompositionLevels)
{
    if (inputSignal.isEmpty() || decompositionLevels <= 0)
        return inputSignal;

    WaveletTransform::DecompositionResult decomp =
        WaveletTransform::decompose(inputSignal, decompositionLevels);
    if (decomp.details.isEmpty())
        return inputSignal;

    // 对每一层细节系数独立处理
    for (int lv = 0; lv < decomp.details.size(); ++lv) {
        QVector<double>& detail = decomp.details[lv];

        // 1. 稳健估计本层噪声标准差
        double sigma = estimateNoiseSigmaRobust(detail);

        // 2. 计算 BayesShrink 阈值 (若信号方差极低，直接无限大)
        double T = 0.0;
        if (sigma > kVarianceEpsilon) {
            double energy = 0.0;
            for (double v : detail) energy += v * v;
            double obsVar = energy / detail.size();
            double sigVar = std::max(obsVar - sigma * sigma, 0.0);
            if (sigVar <= kVarianceEpsilon)
                T = std::numeric_limits<double>::infinity();
            else
                T = (sigma * sigma) / std::sqrt(sigVar);
        }

        // 3. 乘以频带权重
        T *= levelThresholdWeight(lv);

        // 4. 小波域局部脉冲检测，生成保护掩膜
        QVector<bool> pulseMask = detectLocalPulses(detail);

        // 5. 应用 Garrote 阈值 (保护区域内不处理)
        applyGarroteShrinkage(detail, T, pulseMask);
    }

    // 重构
    QVector<double> approx = decomp.approximation;
    for (int lv = decomp.details.size() - 1; lv >= 0; --lv) {
        int dil = 1 << lv;
        approx = reconstructLevelStationary(approx, decomp.details[lv], dil);
        if (approx.isEmpty()) return inputSignal;
    }

    // 时域后处理
    // 注意：finestNoiseSigma 可在第一层重新估算一次用于峰值保护
    double finestSigma = estimateNoiseSigmaRobust(decomp.details.first());
    preserveTransientPeaks(inputSignal, approx, finestSigma);
    protectOutputRms(inputSignal, approx);

    return approx;
}
} // namespace

// ---------- 公有接口 (不变) ----------
WaveletTransform::DecompositionResult WaveletTransform::decompose(
    const QVector<double>& signal, int levels)
{
    DecompositionResult result;
    if (signal.isEmpty() || levels <= 0) {
        result.approximation = signal;
        return result;
    }
    const int reqLevels = std::min(levels, kRequestedLevels);
    const int decLevels = std::min(reqLevels, resolveSwtDecompositionLevelsByLength(signal.size()));
    if (decLevels <= 0) {
        result.approximation = signal;
        return result;
    }

    QVector<double> approx = signal, next;
    QVector<double> detail;
    for (int lv = 0; lv < decLevels; ++lv) {
        int dil = 1 << lv;
        decomposeLevelStationary(approx, dil, next, detail);
        result.details.append(std::move(detail));
        approx.swap(next);
    }
    result.approximation = std::move(approx);
    return result;
}

QVector<float> WaveletTransform::denoise(const QVector<float>& rawData, int sampleRate)
{
    if (rawData.isEmpty()) return {};

    const int effSR = normalizeSampleRate(sampleRate);
    int levels = resolveSwtDecompositionLevels(rawData.size(), effSR);
    if (levels == 0) return rawData;

    QVector<double> input(rawData.size());
    for (int i = 0; i < rawData.size(); ++i)
        input[i] = static_cast<double>(rawData[i]);

    QVector<double> clean = denoiseStationary(input, levels);

    // 尺寸对齐
    if (clean.size() > rawData.size())
        clean.resize(rawData.size());
    else if (clean.size() < rawData.size()) {
        int old = clean.size();
        clean.resize(rawData.size());
        for (int i = old; i < rawData.size(); ++i)
            clean[i] = static_cast<double>(rawData[i]);
    }

    QVector<float> denoised(clean.size());
    for (int i = 0; i < clean.size(); ++i)
        denoised[i] = static_cast<float>(clean[i]);

    if (g_debugLoggingEnabled.load(std::memory_order_relaxed)) {
        double sumInputSq = 0.0, sumOutputSq = 0.0, sumDiffSq = 0.0;
        double maxInput = 0.0, maxOutput = 0.0;
        const int N = rawData.size();

        for (int i = 0; i < N; ++i) {
            double inVal = static_cast<double>(rawData[i]);
            double outVal = static_cast<double>(denoised[i]);
            double diff = inVal - outVal;

            sumInputSq += inVal * inVal;
            sumOutputSq += outVal * outVal;
            sumDiffSq += diff * diff;

            double absIn = std::abs(inVal);
            double absOut = std::abs(outVal);
            if (absIn > maxInput) maxInput = absIn;
            if (absOut > maxOutput) maxOutput = absOut;
        }

        double rmsIn = std::sqrt(sumInputSq / N);
        double rmsOut = std::sqrt(sumOutputSq / N);

        double inputRmsDb = 20.0 * std::log10(std::max(rmsIn, 1e-12));
        double outputRmsDb = 20.0 * std::log10(std::max(rmsOut, 1e-12));
        double noiseReductionDb = (sumDiffSq > 1e-12) ?
                                      10.0 * std::log10(sumInputSq / sumDiffSq) : 99.0;
        double signalAttenuationDb = 10.0 * std::log10(std::max(sumOutputSq, 1e-12) / std::max(sumInputSq, 1e-12));
        double peakRetention = (maxInput > 1e-12) ? (maxOutput / maxInput) : 1.0;

        qDebug().nospace()
            << "[WaveletDenoise]"
            << " Input RMS:" << QString::number(inputRmsDb, 'f', 1) << "dB"
            << " Output RMS:" << QString::number(outputRmsDb, 'f', 1) << "dB"
            << " NR:" << QString::number(noiseReductionDb, 'f', 1) << "dB"
            << " Attenuation:" << QString::number(signalAttenuationDb, 'f', 1) << "dB"
            << " PeakRetention:" << QString::number(peakRetention, 'f', 3);
    }

    if (denoised.size() > rawData.size())
        denoised.resize(rawData.size());

    return denoised;
}

void WaveletTransform::resetStreamingState(StreamingState& state)
{
    state = StreamingState{};
}

void WaveletTransform::setDebugLoggingEnabled(bool enabled)
{
    g_debugLoggingEnabled.store(enabled, std::memory_order_relaxed);
}

bool WaveletTransform::debugLoggingEnabled()
{
    return g_debugLoggingEnabled.load(std::memory_order_relaxed);
}

QVector<float> WaveletTransform::denoiseStreaming(
    const QVector<float>& rawData,
    int sampleRate,
    StreamingState& state)
{
    if (rawData.isEmpty()) {
        return {};
    }

    const int effectiveSampleRate = normalizeSampleRate(sampleRate);
    if (!state.initialized || state.sampleRate != effectiveSampleRate) {
        state = StreamingState{};
        state.initialized = true;
        state.sampleRate = effectiveSampleRate;
    }

    const int boundaryLength = resolveSwtBoundaryLength(rawData.size(), effectiveSampleRate);
    const int maxOverlapLength =
        rawData.isEmpty() ? 0 : static_cast<int>(rawData.size()) - 1;
    state.overlapLength = std::min(boundaryLength, maxOverlapLength);
    state.historyLength = std::max({ state.overlapLength, boundaryLength * 2, 1024 });

    QVector<float> combined;
    combined.reserve(state.historyInput.size() + rawData.size());
    combined += state.historyInput;
    combined += rawData;

    const QVector<float> denoisedCombined = denoise(combined, effectiveSampleRate);
    QVector<float> current(rawData.size(), 0.0f);

    const int extractionStart = static_cast<int>(state.historyInput.size());
    if (extractionStart < denoisedCombined.size()) {
        const int available = std::min(
            static_cast<int>(rawData.size()),
            static_cast<int>(denoisedCombined.size()) - extractionStart);
        for (int i = 0; i < available; ++i) {
            current[i] = denoisedCombined[extractionStart + i];
        }
        for (int i = available; i < rawData.size(); ++i) {
            current[i] = rawData[i];
        }
    } else {
        current = rawData;
    }

    // Hold back the edge-sensitive tail and emit it after the next chunk arrives.
    QVector<float> output;
    output.reserve(rawData.size());

    const int overlap = std::min(
        state.overlapLength,
        static_cast<int>(state.historyInput.size()));
    if (overlap > 0) {
        const int correctedTailStart = static_cast<int>(state.historyInput.size()) - overlap;
        const int correctedTailAvailable = std::min(
            overlap,
            std::max(0, static_cast<int>(denoisedCombined.size()) - correctedTailStart));
        for (int i = 0; i < correctedTailAvailable; ++i) {
            output.append(denoisedCombined[correctedTailStart + i]);
        }
    }

    const int startupPadding = state.overlapLength - static_cast<int>(output.size());
    if (startupPadding > 0) {
        const float paddingValue = current.isEmpty() ? 0.0f : current.front();
        for (int i = 0; i < startupPadding; ++i) {
            output.append(paddingValue);
        }
    }

    const int stableCount = std::max(0, static_cast<int>(rawData.size()) - state.overlapLength);
    for (int i = 0; i < stableCount; ++i) {
        output.append(current[i]);
    }

    if (static_cast<int>(output.size()) < rawData.size()) {
        for (int i = static_cast<int>(output.size()); i < rawData.size(); ++i) {
            output.append(current[i]);
        }
    } else if (static_cast<int>(output.size()) > rawData.size()) {
        output.resize(rawData.size());
    }

    state.historyInput += rawData;
    if (state.historyInput.size() > state.historyLength) {
        const int removeCount = state.historyInput.size() - state.historyLength;
        state.historyInput.erase(
            state.historyInput.begin(),
            state.historyInput.begin() + removeCount);
    }

    return output;
}
