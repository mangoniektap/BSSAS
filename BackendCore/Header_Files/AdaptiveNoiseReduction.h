/** @file AdaptiveNoiseReduction.h
 *  @brief 自适应降噪 —— 基于谱减法与熵驱动噪声估计的单通道语音增强
 *
 *  本模块实现了基于短时傅里叶变换 (STFT) 的频域自适应降噪算法。
 *  核心流程包括：
 *  - 分帧加窗 (Hann 窗) 后计算功率谱
 *  - 基于谱熵的自适应噪声估计 (Minima-Controlled Recursive Averaging)
 *  - 先验/后验信噪比估计与最优增益计算 (Wiener 滤波风格)
 *  - 过零保护与频率/时间域平滑
 *  - 重叠相加 (Overlap-Add) 重建时域信号
 *
 *  支持两种调用模式：
 *  - 离线模式：一次性处理全部输入数据
 *  - 流式模式：逐帧处理，内部保持重叠缓冲区和噪声估计状态
 */

#ifndef ADAPTIVENOISEREDUCTION_H
#define ADAPTIVENOISEREDUCTION_H

#include <QVector>

/** @brief 自适应降噪处理类（纯静态工具类，不可实例化） */
class AdaptiveNoiseReduction
{
public:
    AdaptiveNoiseReduction() = delete;

    /** @brief 降噪算法可调参数 */
    struct Parameters
    {
        int frameLength = 0;                /**< STFT 帧长（样本数） */
        int hopLength = 0;                  /**< 帧移（样本数） */
        int minTrackingFrames = 0;          /**< 最小噪声跟踪帧数 */
        int noiseInitFrameCount = 0;        /**< 噪声初始化帧数 */

        double spectrumSmoothing = 0.82;             /**< 功率谱平滑系数 */
        double noiseUpdateAlphaNoiseOnly = 0.72;     /**< 纯噪声帧噪声更新速率 */
        double noiseUpdateAlphaSpeech = 0.98;        /**< 含语音帧噪声更新速率（接近 1 = 几乎不更新） */
        double entropyTrackingRate = 0.12;           /**< 熵跟踪速率 */
        double adaptiveAlphaMinimum = 0.35;          /**< 自适应平滑系数下限 */
        double adaptiveAlphaMaximum = 0.98;          /**< 自适应平滑系数上限 */
        double adaptiveAlphaZScoreRange = 2.5;       /**< 自适应平滑 Z 分数范围 */

        double ratioSpeechPresenceLower = 1.15;      /**< 语音存在概率比下限 */
        double ratioSpeechPresenceUpper = 2.8;       /**< 语音存在概率比上限 */
        double posterioriSnrSpeechUpper = 8.0;       /**< 后验信噪比语音上限 */

        double minGain = 0.15;                       /**< 最小增益 */
        double transientMinGain = 0.35;              /**< 瞬态信号最小增益 */
        double transientCrestFactor = 4.5;           /**< 瞬态检测峰值因子阈值 */
        double gainTemporalSmoothing = 0.62;         /**< 增益时间平滑系数 */
        double gainFrequencySmoothing = 0.16;        /**< 增益频率平滑系数 */
        double minimumOutputRmsRatio = 0.45;         /**< 输出 RMS 相对原始的最小比例 */
        double minPrioriSnr = 1e-3;                  /**< 最小先验信噪比 */
        double maxPrioriSnr = 100.0;                 /**< 最大先验信噪比 */
    };

    /** @brief 流式降噪处理的内部状态 */
    struct StreamingState
    {
        bool initialized = false;       /**< 是否已初始化 */
        int sampleRate = 0;             /**< 采样率 (Hz) */
        Parameters parameters;          /**< 当前参数 */

        QVector<double> window;                  /**< Hann 窗系数 */
        QVector<double> analysisBuffer;          /**< 分析帧输入缓冲 */
        QVector<double> synthesisOverlap;        /**< 合成重叠缓冲 */
        QVector<double> normalizationOverlap;    /**< 归一化重叠缓冲 */

        QVector<double> smoothedPsd;             /**< 平滑后的功率谱密度 */
        QVector<double> noisePsd;                /**< 估计的噪声功率谱密度 */
        QVector<double> minimaCurrent;           /**< 当前最小值跟踪 */
        QVector<double> minimaReference;         /**< 参考最小值 */
        QVector<double> previousGain;            /**< 上一帧增益 */
        QVector<double> previousPosterioriSnr;   /**< 上一帧后验信噪比 */
        QVector<double> previousPowerSpectrum;   /**< 上一帧功率谱 */
        QVector<double> entropyContributionMean;       /**< 熵贡献均值 */
        QVector<double> entropyContributionVariance;   /**< 熵贡献方差 */

        QVector<float> analysisFrame;            /**< 分析帧时域样本 */
        QVector<double> powerSpectrum;           /**< 当前帧功率谱 */

        int frameIndex = 0;                 /**< 当前帧序号 */
        int minimaFrameCounter = 0;         /**< 最小值跟踪帧计数器 */
        int noiseInitStableFrameCount = 0;  /**< 噪声初始化采纳的稳定帧数 */
        bool hasPreviousPowerSpectrum = false; /**< 是否已有上一帧功率谱 */
    };

    /** @brief 离线降噪处理（一次性处理全部数据）
     *  @param input 原始输入信号
     *  @param sampleRate 采样率 (Hz)
     *  @returns 降噪后的信号
     */
    static QVector<float> denoise(const QVector<float>& input, int sampleRate);

    /** @brief 流式降噪处理（逐帧处理）
     *  @param input 输入信号帧
     *  @param sampleRate 采样率 (Hz)
     *  @param state 流式状态（需在调用间保持）
     *  @returns 降噪后的信号帧（可能为空，等待更多帧）
     */
    static QVector<float> denoiseStreaming(
        const QVector<float>& input,
        int sampleRate,
        StreamingState& state);

    /** @brief 重置流式处理状态 */
    static void resetStreamingState(StreamingState& state);

    /** @brief 启用或禁用调试日志输出 */
    static void setDebugLoggingEnabled(bool enabled);
    /** @brief 查询调试日志是否已启用 */
    static bool debugLoggingEnabled();

private:
    /** @brief 根据采样率和样本数构造推荐参数 */
    static Parameters makeParameters(int sampleRate, int sampleCount);
};

#endif // ADAPTIVENOISEREDUCTION_H
