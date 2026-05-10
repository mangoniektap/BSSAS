/**
 * @file TransientNoiseSuppressor.h
 * @brief 瞬态噪声抑制算法，基于峭度因子检测与重叠相加(OLA)帧处理。
 *
 * 检测信号中突发的高能量瞬态噪声并衰减，同时保护肠鸣音等低频生物声学信号。
 * 支持离线和实时流式两种处理模式。
 */
#ifndef TRANSIENTNOISESUPPRESSOR_H
#define TRANSIENTNOISESUPPRESSOR_H

#include <QVector>
#include <algorithm>

/**
 * @brief 瞬态噪声抑制器。
 *
 * 通过峭度因子、能量比率和过零率联合判决识别瞬态噪声帧，
 * 使用 sqrt-Hann 窗进行 OLA 处理，对检测到的突发帧施加可配置衰减。
 * 内置低频能量保护机制防止肠鸣音被误衰减。
 */
class TransientNoiseSuppressor
{
public:
    TransientNoiseSuppressor() = delete;

    /** @brief 算法可调参数。 */
    struct Parameters
    {
        int     frameLength         = 256;   ///< 分析帧长（样点数）
        int     hopLength           = 128;   ///< 帧移（样点数）
        double  transientThreshold  = 8.0;   ///< 峭度因子检测阈值
        double  minEnergyRatio      = 0.3;   ///< 候选帧相对局部均值的最小能量比
        double  maxEnergyRatio      = 4.0;   ///< 候选帧相对局部均值的最大能量比
        double  maxDurationMs       = 40.0;  ///< 连续突发最大持续时间（毫秒）
        double  attenuationGain     = 0.1;   ///< 衰减增益（0~1，越小衰减越强）
        double  bowelProtectFraction = 0.7;  ///< 低频能量占比高于此值时降低衰减
        double  lowFreqUpperHz      = 400.0; ///< 低频保护频带上限（Hz）
        double  rmsFloor           = 1e-6;   ///< RMS 底噪，防止静音段误判
    };

    /** @brief 流式处理状态。 */
    struct StreamingState
    {
        bool initialized = false;              ///< 是否已初始化
        int  sampleRate = 0;                   ///< 当前采样率
        Parameters parameters;                 ///< 当前参数
        QVector<double> window;                ///< sqrt-Hann 窗系数
        QVector<double> synthesisOverlap;      ///< 合成重叠区
        QVector<double> normalizationOverlap;  ///< 归一化重叠区
        QVector<double> analysisBuffer;        ///< 分析缓冲区
        QVector<float>  analysisFrame;         ///< 当前分析帧
        int   hopLength = 0;                   ///< 帧移
        int   overlapLength = 0;               ///< 重叠长度
    };

    /**
     * @brief 离线全量瞬态噪声抑制。
     * @param input 输入信号。
     * @param sampleRate 采样率（Hz），用于自适应帧长。
     * @returns 抑制后的信号。
     */
    static QVector<float> suppress(const QVector<float>& input, int sampleRate);

    /**
     * @brief 离线全量瞬态噪声抑制（指定参数）。
     * @param input 输入信号。
     * @param sampleRate 采样率（Hz）。
     * @param params 算法参数。
     * @returns 抑制后的信号。
     */
    static QVector<float> suppress(const QVector<float>& input, int sampleRate,
                                   const Parameters& params);

    /**
     * @brief 流式瞬态噪声抑制（自动推断参数）。
     * @param input 输入信号块。
     * @param sampleRate 采样率（Hz）。
     * @param state 流式状态，首次调用需为零初始化。
     * @returns 抑制后的信号块。
     */
    static QVector<float> suppressStreaming(const QVector<float>& input, int sampleRate,
                                            StreamingState& state);

    /**
     * @brief 流式瞬态噪声抑制（指定参数）。
     * @param input 输入信号块。
     * @param sampleRate 采样率（Hz）。
     * @param state 流式状态。
     * @param params 算法参数。
     * @returns 抑制后的信号块。
     */
    static QVector<float> suppressStreaming(const QVector<float>& input, int sampleRate,
                                            StreamingState& state, const Parameters& params);

    /** @brief 重置流式状态。 */
    static void resetStreamingState(StreamingState& state);

    /** @brief 启用/禁用调试日志。 */
    static void setDebugLoggingEnabled(bool enabled);

    /** @brief 查询调试日志是否启用。 */
    static bool debugLoggingEnabled();

private:
    /** @brief 根据采样率和数据长度自动推导帧参数。 */
    static Parameters makeParameters(int sampleRate, int sampleCount = 0);
};

#endif // TRANSIENTNOISESUPPRESSOR_H
