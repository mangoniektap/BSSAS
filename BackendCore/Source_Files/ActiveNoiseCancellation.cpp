/** @file ActiveNoiseCancellation.cpp
 *  @brief 自适应主动降噪 (ANC) 算法实现。基于 LMS 自适应滤波器，通过参考噪声信号消除目标信号中的噪声分量。
 *         包含直流阻断、时延对齐、互相关延迟估计、自适应权重更新及流式处理接口。
 */

#include "ActiveNoiseCancellation.h"

#include "DataManager.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace {
constexpr int kMinimumFilterLength = 16;
constexpr int kNormalMaximumFilterLength = 256;
constexpr int kHighSpeedMaximumFilterLength = 512;
constexpr int kMinimumCorrelationSamples = 16;
constexpr double kTwoPi = 6.28318530717958647692;

std::atomic_bool g_debugLoggingEnabled = true;

int normalizeSampleRate(int sampleRate)
{
    return sampleRate > 0 ? sampleRate : DataManager::DEFAULT_SAMPLE_RATE;
}

bool sameParameters(
    const ActiveNoiseCancellation::Parameters& left,
    const ActiveNoiseCancellation::Parameters& right)
{
    return left.filterLength == right.filterLength &&
        left.maxDelaySamples == right.maxDelaySamples &&
        left.minimumReferenceRms == right.minimumReferenceRms &&
        left.minimumReferenceValidRatio == right.minimumReferenceValidRatio &&
        left.minimumCorrelation == right.minimumCorrelation &&
        left.stepSize == right.stepSize &&
        left.normalizationEpsilon == right.normalizationEpsilon &&
        left.dcBlockerCutoffHz == right.dcBlockerCutoffHz;
}

void initializeState(
    ActiveNoiseCancellation::StreamingState& state,
    int sampleRate,
    const ActiveNoiseCancellation::Parameters& parameters)
{
    state = ActiveNoiseCancellation::StreamingState{};
    state.sampleRate = normalizeSampleRate(sampleRate);
    state.parameters = parameters;
    state.weights.fill(0.0f, parameters.filterLength);
    state.referenceHistory.fill(0.0f, parameters.filterLength);
    state.referenceAlignmentTail.fill(0.0f, parameters.maxDelaySamples);
    state.initialized = parameters.filterLength > 0;
}

float sanitizeSample(float value)
{
    return std::isfinite(value) ? value : 0.0f;
}

double computeRms(const QVector<float>& values)
{
    if (values.isEmpty()) {
        return 0.0;
    }

    double energy = 0.0;
    for (float value : values) {
        const double sample = static_cast<double>(sanitizeSample(value));
        energy += sample * sample;
    }

    return std::sqrt(energy / static_cast<double>(values.size()));
}

double computeRmsPrefix(const QVector<float>& values, int count)
{
    const int usedCount = std::min(count, static_cast<int>(values.size()));
    if (usedCount <= 0) {
        return 0.0;
    }

    double energy = 0.0;
    for (int index = 0; index < usedCount; ++index) {
        const double sample = static_cast<double>(sanitizeSample(values[index]));
        energy += sample * sample;
    }

    return std::sqrt(energy / static_cast<double>(usedCount));
}

double computeValidRatioAtLag(
    const QVector<float>& referenceValidFlags,
    int targetCount,
    int lag)
{
    if (targetCount <= 0) {
        return 0.0;
    }

    double validCount = 0.0;
    int usedCount = 0;
    for (int targetIndex = 0; targetIndex < targetCount; ++targetIndex) {
        const int referenceIndex = targetIndex + lag;
        if (referenceIndex < 0) {
            continue;
        }

        ++usedCount;
        if (referenceIndex < referenceValidFlags.size()) {
            validCount += referenceValidFlags[referenceIndex] > 0.5f ? 1.0 : 0.0;
        }
    }

    if (usedCount <= 0) {
        return 0.0;
    }

    return validCount / static_cast<double>(usedCount);
}

double dcBlockerAlpha(double cutoffHz, int sampleRate)
{
    const int effectiveSampleRate = normalizeSampleRate(sampleRate);
    if (cutoffHz <= 0.0 || !std::isfinite(cutoffHz)) {
        return 0.0;
    }

    return std::clamp(
        1.0 - std::exp(-kTwoPi * cutoffHz / static_cast<double>(effectiveSampleRate)),
        0.0,
        1.0);
}

void appendDcBlockedSamples(
    const QVector<float>& source,
    int outputSize,
    double alpha,
    double& dcEstimate,
    QVector<float>& destination,
    QVector<float>* validFlags = nullptr)
{
    if (outputSize <= 0) {
        return;
    }

    destination.reserve(destination.size() + outputSize);
    if (validFlags != nullptr) {
        validFlags->reserve(validFlags->size() + outputSize);
    }

    for (int index = 0; index < outputSize; ++index) {
        const bool hasSourceSample = index < source.size();
        const float rawValue = hasSourceSample ? source[index] : 0.0f;
        const bool valid = hasSourceSample && std::isfinite(rawValue);
        const double sample = static_cast<double>(sanitizeSample(rawValue));
        dcEstimate += alpha * (sample - dcEstimate);
        destination.append(static_cast<float>(sample - dcEstimate));
        if (validFlags != nullptr) {
            validFlags->append(valid ? 1.0f : 0.0f);
        }
    }
}

float referenceAt(
    const QVector<float>& reference,
    const QVector<float>& tail,
    int targetIndex,
    int lag)
{
    const int sourceIndex = targetIndex + lag;
    if (sourceIndex >= 0) {
        return sourceIndex < reference.size() ? reference[sourceIndex] : 0.0f;
    }

    const int tailIndex = tail.size() + sourceIndex;
    if (tailIndex >= 0 && tailIndex < tail.size()) {
        return tail[tailIndex];
    }

    return 0.0f;
}

double normalizedCorrelationAtLag(
    const QVector<float>& target,
    const QVector<float>& reference,
    const QVector<float>& tail,
    int targetCount,
    int lag)
{
    double targetSum = 0.0;
    double referenceSum = 0.0;
    int usedCount = 0;

    for (int targetIndex = 0; targetIndex < targetCount; ++targetIndex) {
        const int referenceIndex = targetIndex + lag;
        if (referenceIndex < -tail.size() || referenceIndex >= reference.size()) {
            continue;
        }

        targetSum += static_cast<double>(sanitizeSample(target[targetIndex]));
        referenceSum += static_cast<double>(
            referenceAt(reference, tail, targetIndex, lag));
        ++usedCount;
    }

    if (usedCount < kMinimumCorrelationSamples) {
        return 0.0;
    }

    const double targetMean = targetSum / static_cast<double>(usedCount);
    const double referenceMean = referenceSum / static_cast<double>(usedCount);
    double cross = 0.0;
    double targetEnergy = 0.0;
    double referenceEnergy = 0.0;

    for (int targetIndex = 0; targetIndex < targetCount; ++targetIndex) {
        const int referenceIndex = targetIndex + lag;
        if (referenceIndex < -tail.size() || referenceIndex >= reference.size()) {
            continue;
        }

        const double targetSample =
            static_cast<double>(sanitizeSample(target[targetIndex])) - targetMean;
        const double referenceSample =
            static_cast<double>(referenceAt(reference, tail, targetIndex, lag)) -
            referenceMean;
        cross += targetSample * referenceSample;
        targetEnergy += targetSample * targetSample;
        referenceEnergy += referenceSample * referenceSample;
    }

    const double denominator = std::sqrt(targetEnergy * referenceEnergy);
    if (denominator <= std::numeric_limits<double>::epsilon()) {
        return 0.0;
    }

    return cross / denominator;
}

/** @brief 在延迟范围内搜索目标信号与参考信号之间的最佳互相关延迟。
 *  @param target 目标信号（待降噪）
 *  @param reference 参考噪声信号
 *  @param tail 上一批参考信号的尾部（用于负延迟对齐）
 *  @param parameters ANC 参数配置
 *  @param targetCount 参与计算的目标样本数
 *  @param bestCorrelation 输出最佳相关系数
 *  @returns 最佳延迟样本数（正=参考滞后，负=参考超前）
 */
int chooseReferenceLag(
    const QVector<float>& target,
    const QVector<float>& reference,
    const QVector<float>& tail,
    const ActiveNoiseCancellation::Parameters& parameters,
    int targetCount,
    double& bestCorrelation)
{
    const int maxLag = std::min(
        parameters.maxDelaySamples,
        std::max(0, targetCount - kMinimumCorrelationSamples));
    int bestLag = 0;
    bestCorrelation = 0.0;

    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        const double correlation =
            normalizedCorrelationAtLag(target, reference, tail, targetCount, lag);
        if (std::abs(correlation) > std::abs(bestCorrelation)) {
            bestCorrelation = correlation;
            bestLag = lag;
        }
    }

    return bestLag;
}

void updateReferenceAlignmentTail(
    ActiveNoiseCancellation::StreamingState& state,
    int consumedReferenceSamples)
{
    const int tailSize = state.parameters.maxDelaySamples;
    if (tailSize <= 0) {
        state.referenceAlignmentTail.clear();
        return;
    }

    for (int index = 0; index < consumedReferenceSamples; ++index) {
        state.referenceAlignmentTail.append(state.pendingReference[index]);
    }

    const int excess = state.referenceAlignmentTail.size() - tailSize;
    if (excess > 0) {
        state.referenceAlignmentTail.remove(0, excess);
    }
}

void discardProcessedSamples(
    ActiveNoiseCancellation::StreamingState& state,
    int processedTargetSamples)
{
    const int consumedReferenceSamples =
        std::min(processedTargetSamples, static_cast<int>(state.pendingReference.size()));
    updateReferenceAlignmentTail(state, consumedReferenceSamples);

    if (processedTargetSamples > 0) {
        state.pendingTarget.remove(
            0,
            std::min(processedTargetSamples, static_cast<int>(state.pendingTarget.size())));
    }
    if (consumedReferenceSamples > 0) {
        state.pendingReference.remove(0, consumedReferenceSamples);
        state.pendingReferenceValidFlags.remove(0, consumedReferenceSamples);
    }
}

void clearAdaptiveWeights(ActiveNoiseCancellation::StreamingState& state)
{
    state.weights.fill(0.0f, state.parameters.filterLength);
    state.referenceHistory.fill(0.0f, state.parameters.filterLength);
    state.referenceHistoryWriteIndex = 0;
    state.hasUsableWeights = false;
}

/** @brief 对流式缓存中的待处理样本执行 LMS 自适应滤波降噪。
 *         包含延迟对齐、LMS 权重更新、直流阻断后的参考/目标信号的逐样本滤波。
 *  @param state 流式处理状态
 *  @param flushing 是否为刷新模式（处理所有缓存样本，不等待更多参考数据）
 *  @param metrics 输出降噪指标（可选）
 *  @returns 降噪后的输出信号
 */
QVector<float> processPending(
    ActiveNoiseCancellation::StreamingState& state,
    bool flushing,
    ActiveNoiseCancellation::Metrics* metrics)
{
    if (metrics != nullptr) {
        *metrics = ActiveNoiseCancellation::Metrics{};
    }

    if (!state.initialized || state.pendingTarget.isEmpty()) {
        return {};
    }

    const ActiveNoiseCancellation::Parameters& parameters = state.parameters;
    const int availableReferenceSamples =
        static_cast<int>(state.pendingReference.size());
    const int processableCount = flushing
        ? static_cast<int>(state.pendingTarget.size())
        : std::min(
              static_cast<int>(state.pendingTarget.size()),
              std::max(0, availableReferenceSamples - parameters.maxDelaySamples));
    if (processableCount <= 0) {
        return {};
    }

    double bestCorrelation = 0.0;
    const int selectedLag = chooseReferenceLag(
        state.pendingTarget,
        state.pendingReference,
        state.referenceAlignmentTail,
        parameters,
        processableCount,
        bestCorrelation);
    state.selectedDelaySamples = selectedLag;
    state.lastCorrelation = bestCorrelation;

    const double targetRms = computeRmsPrefix(state.pendingTarget, processableCount);
    const double referenceRms = computeRmsPrefix(state.pendingReference, processableCount);
    const double referenceValidRatio = computeValidRatioAtLag(
        state.pendingReferenceValidFlags,
        processableCount,
        selectedLag);
    const bool referenceValid =
        referenceRms >= parameters.minimumReferenceRms &&
        referenceValidRatio >= parameters.minimumReferenceValidRatio &&
        std::abs(bestCorrelation) >= parameters.minimumCorrelation;
    const bool adaptationFrozen = !referenceValid;

    QVector<float> output(processableCount, 0.0f);
    double outputEnergy = 0.0;
    bool updatedWeights = false;

    for (int sampleIndex = 0; sampleIndex < processableCount; ++sampleIndex) {
        const float referenceSample = referenceAt(
            state.pendingReference,
            state.referenceAlignmentTail,
            sampleIndex,
            selectedLag);
        state.referenceHistory[state.referenceHistoryWriteIndex] = referenceSample;

        double estimatedNoise = 0.0;
        double referenceEnergy = 0.0;
        int tapHistoryIndex = state.referenceHistoryWriteIndex;
        for (int tapIndex = 0; tapIndex < parameters.filterLength; ++tapIndex) {
            const double historySample =
                static_cast<double>(state.referenceHistory[tapHistoryIndex]);
            estimatedNoise += static_cast<double>(state.weights[tapIndex]) * historySample;
            referenceEnergy += historySample * historySample;

            --tapHistoryIndex;
            if (tapHistoryIndex < 0) {
                tapHistoryIndex = parameters.filterLength - 1;
            }
        }

        const double targetSample =
            static_cast<double>(sanitizeSample(state.pendingTarget[sampleIndex]));
        double errorSignal = targetSample - estimatedNoise;
        bool finiteSample =
            std::isfinite(estimatedNoise) &&
            std::isfinite(errorSignal) &&
            std::isfinite(referenceEnergy);
        if (!finiteSample) {
            clearAdaptiveWeights(state);
            estimatedNoise = 0.0;
            errorSignal = targetSample;
        }

        output[sampleIndex] = static_cast<float>(
            std::isfinite(errorSignal) ? errorSignal : targetSample);
        outputEnergy += static_cast<double>(output[sampleIndex]) *
            static_cast<double>(output[sampleIndex]);

        const bool updateAllowed =
            finiteSample &&
            !adaptationFrozen &&
            referenceEnergy > parameters.normalizationEpsilon;
        if (updateAllowed) {
            const double stepFactor =
                parameters.stepSize / (referenceEnergy + parameters.normalizationEpsilon);
            tapHistoryIndex = state.referenceHistoryWriteIndex;
            for (int tapIndex = 0; tapIndex < parameters.filterLength; ++tapIndex) {
                const double historySample =
                    static_cast<double>(state.referenceHistory[tapHistoryIndex]);
                const double nextWeight =
                    static_cast<double>(state.weights[tapIndex]) +
                    stepFactor * errorSignal * historySample;
                if (std::isfinite(nextWeight)) {
                    state.weights[tapIndex] = static_cast<float>(nextWeight);
                } else {
                    clearAdaptiveWeights(state);
                    finiteSample = false;
                    break;
                }

                --tapHistoryIndex;
                if (tapHistoryIndex < 0) {
                    tapHistoryIndex = parameters.filterLength - 1;
                }
            }
            updatedWeights = updatedWeights || finiteSample;
        }

        ++state.referenceHistoryWriteIndex;
        if (state.referenceHistoryWriteIndex >= parameters.filterLength) {
            state.referenceHistoryWriteIndex = 0;
        }
    }

    if (updatedWeights) {
        state.hasUsableWeights = true;
    }

    discardProcessedSamples(state, processableCount);

    if (metrics != nullptr) {
        metrics->referenceValid = referenceValid;
        metrics->bypassed = !referenceValid && !state.hasUsableWeights;
        metrics->adaptationFrozen = adaptationFrozen;
        metrics->referenceRms = referenceRms;
        metrics->targetRms = targetRms;
        metrics->outputRms = output.isEmpty()
            ? 0.0
            : std::sqrt(outputEnergy / static_cast<double>(output.size()));
        metrics->correlation = bestCorrelation;
        metrics->stepSize = adaptationFrozen ? 0.0 : parameters.stepSize;
        metrics->selectedDelaySamples = selectedLag;
    }

    return output;
}
} // namespace

/** @brief 根据采样率生成默认的 ANC 算法参数。
 *  @param sampleRate 信号采样率 (Hz)
 *  @returns 包含滤波器长度、延迟范围、步长等参数的默认配置
 */
ActiveNoiseCancellation::Parameters ActiveNoiseCancellation::makeParameters(int sampleRate)
{
    const int effectiveSampleRate = normalizeSampleRate(sampleRate);
    Parameters parameters;

    const int maximumFilterLength =
        effectiveSampleRate >= 100000 ? kHighSpeedMaximumFilterLength : kNormalMaximumFilterLength;
    parameters.filterLength = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(effectiveSampleRate) * 0.010)),
        kMinimumFilterLength,
        maximumFilterLength);
    parameters.maxDelaySamples = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(effectiveSampleRate) * 0.006)),
        0,
        256);

    if (effectiveSampleRate >= 100000) {
        parameters.stepSize = 0.24;
    }

    return parameters;
}

/** @brief 使用默认参数对目标信号进行自适应主动降噪处理（便捷重载）。
 *  @param targetSignal 待降噪的目标信号
 *  @param referenceNoise 参考噪声信号
 *  @param sampleRate 信号采样率 (Hz)
 *  @param state 流式处理状态（保持跨帧上下文）
 *  @param metrics 输出降噪指标（可选）
 *  @returns 降噪后的输出信号
 */
QVector<float> ActiveNoiseCancellation::cancel(
    const QVector<float>& targetSignal,
    const QVector<float>& referenceNoise,
    int sampleRate,
    StreamingState& state,
    Metrics* metrics)
{
    return cancel(
        targetSignal,
        referenceNoise,
        sampleRate,
        state,
        makeParameters(sampleRate),
        metrics);
}

/** @brief 使用自定义参数对目标信号进行自适应主动降噪处理。
 *  @param targetSignal 待降噪的目标信号
 *  @param referenceNoise 参考噪声信号
 *  @param sampleRate 信号采样率 (Hz)
 *  @param state 流式处理状态（保持跨帧上下文）
 *  @param parameters 自定义 ANC 参数
 *  @param metrics 输出降噪指标（可选）
 *  @returns 降噪后的输出信号
 */
QVector<float> ActiveNoiseCancellation::cancel(
    const QVector<float>& targetSignal,
    const QVector<float>& referenceNoise,
    int sampleRate,
    StreamingState& state,
    const Parameters& parameters,
    Metrics* metrics)
{
    if (metrics != nullptr) {
        *metrics = Metrics{};
    }

    if (targetSignal.isEmpty()) {
        return {};
    }

    const int effectiveSampleRate = normalizeSampleRate(sampleRate);
    if (!state.initialized ||
        state.sampleRate != effectiveSampleRate ||
        !sameParameters(state.parameters, parameters)) {
        initializeState(state, effectiveSampleRate, parameters);
    }

    if (!state.initialized) {
        if (metrics != nullptr) {
            metrics->bypassed = true;
        }
        return targetSignal;
    }

    const double alpha = dcBlockerAlpha(parameters.dcBlockerCutoffHz, effectiveSampleRate);
    appendDcBlockedSamples(
        targetSignal,
        targetSignal.size(),
        alpha,
        state.targetDcEstimate,
        state.pendingTarget);
    appendDcBlockedSamples(
        referenceNoise,
        targetSignal.size(),
        alpha,
        state.referenceDcEstimate,
        state.pendingReference,
        &state.pendingReferenceValidFlags);

    return processPending(state, false, metrics);
}

/** @brief 强制刷新流式状态中的所有缓存数据，输出最后一批降噪结果。
 *  @param state 流式处理状态
 *  @param metrics 输出降噪指标（可选）
 *  @returns 缓存中所有可处理样本的降噪输出
 */
QVector<float> ActiveNoiseCancellation::flush(
    StreamingState& state,
    Metrics* metrics)
{
    return processPending(state, true, metrics);
}

void ActiveNoiseCancellation::resetStreamingState(StreamingState& state)
{
    state = StreamingState{};
}

void ActiveNoiseCancellation::setDebugLoggingEnabled(bool enabled)
{
    g_debugLoggingEnabled.store(enabled, std::memory_order_relaxed);
}

bool ActiveNoiseCancellation::debugLoggingEnabled()
{
    return g_debugLoggingEnabled.load(std::memory_order_relaxed);
}
