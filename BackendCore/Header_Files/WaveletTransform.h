/** @file WaveletTransform.h
 *  @brief 小波变换模块，提供基于 sym6 平移不变 SWT 的多级分解与去噪功能。
 */

#ifndef WAVELETTRANSFORM_H
#define WAVELETTRANSFORM_H

#include <QVector>

/** @brief 小波变换工具类，提供静态小波分解与去噪方法。 */
class WaveletTransform
{
public:
    WaveletTransform() = delete;

    /** @brief 流式去噪的状态保持结构体，存储历史输入与重叠信息。 */
    struct StreamingState
    {
        bool initialized = false;          /**< 是否已初始化 */
        int sampleRate = 0;                /**< 采样率 (Hz) */
        int historyLength = 0;             /**< 历史缓冲区长度 */
        int overlapLength = 0;             /**< 重叠长度 */
        QVector<float> historyInput;       /**< 历史输入数据 */
    };

    /**
     * @brief 多级平移不变小波分解结果 (sym6 SWT / 非抽取小波变换)。
     * @details details[0] 为最精细尺度 (Level-1) 的细节系数，依此类推；
     *          approximation 为最粗糙尺度的低频残余。
     */
    struct DecompositionResult
    {
        QVector<QVector<double>> details;  /**< 各级细节系数 */
        QVector<double> approximation;     /**< 近似系数（低频残余） */
    };

    /**
     * @brief 对信号执行至多 levels 级的平移不变 sym6 SWT 分解。
     * @param signal 输入信号
     * @param levels 分解层数，默认 3（信号过短时实际层数可能更少）
     * @returns 分解结果，包含各层细节系数与近似系数
     */
    static DecompositionResult decompose(const QVector<double>& signal, int levels = 3);

    /**
     * @brief 基于平移不变 sym6 SWT 的去噪，使用分层感知 firm 收缩与 RMS 保护。
     * @param rawData    原始信号数据
     * @param sampleRate 采样率 (Hz)
     * @returns 去噪后的信号数据
     */
    static QVector<float> denoise(const QVector<float>& rawData, int sampleRate);

    /**
     * @brief 流式平移不变 sym6 SWT 去噪，支持分块连续处理。
     * @param rawData    原始信号数据块
     * @param sampleRate 采样率 (Hz)
     * @param state      流式状态（输入/输出，维持历史与重叠信息）
     * @returns 去噪后的信号数据
     */
    static QVector<float> denoiseStreaming(
        const QVector<float>& rawData,
        int sampleRate,
        StreamingState& state);

    /** @brief 重置流式去噪状态。 @param state 待重置的流式状态 */
    static void resetStreamingState(StreamingState& state);

    /** @brief 启用或禁用调试日志。 @param enabled 是否启用 */
    static void setDebugLoggingEnabled(bool enabled);

    /** @brief 查询调试日志是否已启用。 @returns 启用状态 */
    static bool debugLoggingEnabled();
};

#endif // WAVELETTRANSFORM_H
