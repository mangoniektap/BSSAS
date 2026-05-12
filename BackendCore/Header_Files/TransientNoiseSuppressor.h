/**
 * @file TransientNoiseSuppressor.h
 * @brief 离线瞬态噪声抑制模块，基于波峰因数检测与 OLA 合成实现。
 *
 * 该抑制器一次处理完整的信号缓冲区。它检测短时高能量瞬态帧，
 * 对其进行衰减，并保护低频肠鸣音成分免受过度衰减。
 */
#ifndef TRANSIENTNOISESUPPRESSOR_H
#define TRANSIENTNOISESUPPRESSOR_H

#include <QVector>

/** @brief 离线瞬态噪声抑制器，提供静态处理方法。 */
class TransientNoiseSuppressor
{
public:
    TransientNoiseSuppressor() = delete;

    /** @brief 可调算法参数。 */
    struct Parameters
    {
        int     frameLength          = 256;   /**< 分析帧长 (采样点数) */
        int     hopLength            = 128;   /**< 帧移步长 (采样点数) */
        double  transientThreshold   = 8.0;   /**< 波峰因数阈值，用于瞬态检测 */
        double  minEnergyRatio       = 0.3;   /**< 帧能量相对于局部均值的最小比值 */
        double  maxEnergyRatio       = 4.0;   /**< 帧能量相对于局部均值的最大比值 */
        double  maxDurationMs        = 40.0;  /**< 连续瞬态最大持续时间 (ms) */
        double  attenuationGain      = 0.1;   /**< 施加于检测到的瞬态帧的增益 */
        double  bowelProtectFraction = 0.7;   /**< 低频能量超过该比例时降低衰减力度 */
        double  lowFreqUpperHz       = 400.0; /**< 低频保护的上限截止频率 (Hz) */
        double  rmsFloor             = 1e-6;  /**< 能量底噪，避免静音段误触发 */
    };

    /**
     * @brief 对完整信号缓冲区执行瞬态噪声抑制。
     * @param input      输入信号采样
     * @param sampleRate 采样率 (Hz)
     * @returns 抑制后的信号采样，长度与输入相同
     */
    static QVector<float> suppress(const QVector<float>& input, int sampleRate);

    /**
     * @brief 使用指定参数对完整信号缓冲区执行瞬态噪声抑制。
     * @param input      输入信号采样
     * @param sampleRate 采样率 (Hz)
     * @param params     算法参数
     * @returns 抑制后的信号采样，长度与输入相同
     */
    static QVector<float> suppress(const QVector<float>& input, int sampleRate,
                                   const Parameters& params);

    /** @brief 启用或禁用调试指标日志。 @param enabled 是否启用 */
    static void setDebugLoggingEnabled(bool enabled);

    /** @brief 查询调试指标日志是否已启用。 @returns 启用状态 */
    static bool debugLoggingEnabled();

private:
    /** @brief 根据采样率和样本数推导帧参数。 @param sampleRate 采样率 @param sampleCount 样本数 */
    static Parameters makeParameters(int sampleRate, int sampleCount = 0);
};

#endif // TRANSIENTNOISESUPPRESSOR_H
