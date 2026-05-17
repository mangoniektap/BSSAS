/** @file ActiveNoiseCancellation.h
 *  @brief 主动降噪 (ANC) —— 基于 LMS 自适应滤波的实时噪声抵消
 *
 *  本模块实现了基于归一化 LMS（Normalized Least Mean Square）自适应
 *  滤波的单通道主动降噪算法。输入包含目标信号与参考噪声信号，通过
 *  在线学习的 FIR 滤波器估计噪声传输路径并从目标信号中减去估计噪声，
 *  同时提供多种运行时指标（相关性、RMS 能量等）用于质量监控。
 *
 *  支持流式处理：传入帧片段，内部自动管理参考历史缓存和滤波器权重。
 */

#ifndef ACTIVENOISECANCELLATION_H
#define ACTIVENOISECANCELLATION_H

#include <QVector>

/** @brief 主动降噪处理类（纯静态工具类，不可实例化） */
class ActiveNoiseCancellation
{
public:
    ActiveNoiseCancellation() = delete;

    /** @brief ANC 算法可调参数 */
    struct Parameters
    {
        int filterLength = 0;           /**< FIR 滤波器阶数 */
        int maxDelaySamples = 0;        /**< 最大时延搜索范围（样本数） */

        double minimumReferenceRms = 1e-7;          /**< 参考信号最小 RMS 阈值，低于此认为无信号 */
        double minimumReferenceValidRatio = 0.96;   /**< 参考信号有效帧比例下限 */
        double minimumCorrelation = 0.20;           /**< 目标和参考之间的最小相关系数 */
        double minimumCancellationCorrelation = 0.08;   /**< 对消硬旁路相关性下限，低于此立即关闭对消 */
        int cancellationRampUpFrames = 1;               /**< 连续高相关帧数达到后才增强对消 */
        double cancellationAttackStep = 1.0;           /**< 对消门控每帧增强步进 */
        double cancellationReleaseStep = 0.5;          /**< 对消门控每帧衰减步进 */
        double stepSize = 0.30;                     /**< LMS 步长 */
        double normalizationEpsilon = 1e-6;         /**< 归一化因子防止除零 */
        double dcBlockerCutoffHz = 1.0;             /**< 直流阻断滤波器截止频率 (Hz) */
    };

    /** @brief ANC 处理后的实时运行指标 */
    struct Metrics
    {
        bool referenceValid = false;        /**< 参考信号是否有效 */
        bool bypassed = false;              /**< 是否因信号质量不足而旁路 */
        bool adaptationFrozen = false;      /**< 自适应是否被冻结 */
        bool cancellationActive = false;    /**< 对消门控是否处于开启状态 */
        double referenceRms = 0.0;          /**< 参考信号 RMS 值 */
        double targetRms = 0.0;             /**< 目标信号 RMS 值 */
        double outputRms = 0.0;             /**< 输出信号 RMS 值 */
        double correlation = 0.0;           /**< 目标与参考的相关系数 */
        double cancellationGain = 0.0;      /**< 当前对消门控增益 */
        double stepSize = 0.0;              /**< 实际使用的步长 */
        int selectedDelaySamples = 0;       /**< 选定的最优时延（样本数） */
        int highCorrelationFrameCount = 0;  /**< 连续高相关帧计数 */
    };

    /** @brief ANC 流式处理的内部状态 */
    struct StreamingState
    {
        bool initialized = false;           /**< 是否已初始化 */
        int sampleRate = 0;                 /**< 采样率 (Hz) */
        Parameters parameters;              /**< 当前参数 */

        QVector<float> weights;                     /**< FIR 滤波器权重 */
        QVector<float> referenceHistory;            /**< 参考信号历史环形缓冲区 */
        QVector<float> referenceAlignmentTail;      /**< 参考对齐尾部缓冲 */
        QVector<float> pendingTarget;               /**< 待处理目标帧缓存 */
        QVector<float> pendingReference;            /**< 待处理参考帧缓存 */
        QVector<float> pendingReferenceValidFlags;  /**< 参考有效性标志缓存 */

        int referenceHistoryWriteIndex = 0;     /**< 参考历史写入指针 */
        double targetDcEstimate = 0.0;          /**< 目标信号直流分量估计 */
        double referenceDcEstimate = 0.0;       /**< 参考信号直流分量估计 */
        double lastCorrelation = 0.0;           /**< 上一帧相关系数 */
        double cancellationGain = 0.0;          /**< 对消门控平滑增益 */
        int selectedDelaySamples = 0;           /**< 选定的时延（样本数） */
        int highCorrelationFrameCount = 0;      /**< 连续高相关帧计数 */
        bool hasUsableWeights = false;          /**< 滤波器权重是否已收敛可用 */
    };

    /** @brief 根据采样率构造推荐参数 */
    static Parameters makeParameters(int sampleRate);

    /** @brief 执行流式 ANC 处理
     *  @param targetSignal 目标信号帧
     *  @param referenceNoise 参考噪声信号帧
     *  @param sampleRate 采样率 (Hz)
     *  @param state 流式状态（需在调用间保持）
     *  @param metrics 可选输出指标
     *  @returns 降噪后的输出信号
     */
    static QVector<float> cancel(
        const QVector<float>& targetSignal,
        const QVector<float>& referenceNoise,
        int sampleRate,
        StreamingState& state,
        Metrics* metrics = nullptr);

    /** @brief 带自定义参数的流式 ANC
     *  @param targetSignal 目标信号帧
     *  @param referenceNoise 参考噪声信号帧
     *  @param sampleRate 采样率 (Hz)
     *  @param state 流式状态
     *  @param parameters 自定义参数
     *  @param metrics 可选输出指标
     *  @returns 降噪后的输出信号
     */
    static QVector<float> cancel(
        const QVector<float>& targetSignal,
        const QVector<float>& referenceNoise,
        int sampleRate,
        StreamingState& state,
        const Parameters& parameters,
        Metrics* metrics = nullptr);

    /** @brief 冲刷流式缓存，输出缓冲区中剩余的降噪后信号 */
    static QVector<float> flush(
        StreamingState& state,
        Metrics* metrics = nullptr);

    /** @brief 重置流式处理状态 */
    static void resetStreamingState(StreamingState& state);

    /** @brief 启用或禁用调试日志输出 */
    static void setDebugLoggingEnabled(bool enabled);
    /** @brief 查询调试日志是否已启用 */
    static bool debugLoggingEnabled();
};

#endif // ACTIVENOISECANCELLATION_H
