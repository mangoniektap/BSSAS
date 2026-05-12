/** @file MotionArtifactReduction.h
 *  @brief 运动伪影削减 —— 基于 EMD/IMF 分解与分形维度筛选的自适应伪影去除
 *
 *  本模块实现了针对生理信号（特别是肠鸣音）中运动伪影的自动检测
 *  和抑制算法。核心流程包括：
 *  - 对信号分帧后进行经验模态分解 (EMD)，提取固有模态函数 (IMF)
 *  - 计算各 IMF 的分形维度 (Fractal Dimension) 作为伪影特征
 *  - 基于分形维度阈值自动识别含伪影的 IMF 分量
 *  - 对伪影 IMF 进行自适应衰减 (soft decision)，保留真实信号成分
 *  - 通过重叠相加 (Overlap-Add) 重建干净的时域信号
 *
 *  算法对低频运动伪影 (0.5-25 Hz) 特别有效，同时尽可能保护
 *  高频生理信号的完整性。
 */

#ifndef MOTIONARTIFACTREDUCTION_H
#define MOTIONARTIFACTREDUCTION_H

#include <QVector>

/** @brief 运动伪影削减处理类（纯静态工具类，不可实例化） */
class MotionArtifactReduction
{
public:
    MotionArtifactReduction() = delete;

    /** @brief 伪影削减算法可调参数 */
    struct Parameters
    {
        int frameLength = 0;                /**< 分析帧长（样本数） */
        int hopLength = 0;                  /**< 帧移（样本数） */
        int maxImfCount = 6;                /**< EMD 最大 IMF 数量 */
        int maxSiftIterations = 8;          /**< EMD 筛选最大迭代次数 */
        int minimumExtremaCount = 2;        /**< EMD 最小极值点数 */
        int maxArtifactImfCount = 4;        /**< 最多判定为伪影的 IMF 数量 */
        int fdWindowSamples = 0;            /**< 分形维度计算窗口（样本数） */
        int fdHopSamples = 0;               /**< 分形维度计算步长（样本数） */

        double minimumImprovementRatio = 0.03;       /**< 最小改善比，低于此丢弃处理结果 */
        double lowerFrequencyHz = 0.5;               /**< 伪影分析频率下限 (Hz) */
        double upperFrequencyHz = 25.0;              /**< 伪影分析频率上限 (Hz) */
        double candidateBandEnergyRatio = 0.35;      /**< 候选伪影频带能量比例阈值 */
        double attenuationFactor = 0.08;             /**< 伪影 IMF 衰减因子 */
        double softDecisionWidth = 0.25;             /**< 软判决过渡带宽度 */
        double minimumOutputRmsRatio = 0.45;         /**< 输出 RMS 相对原始的最小比例 */
        double fdThresholdMinimum = 1.05;            /**< 分形维度判定阈值下限 */
        double fdThresholdMaximum = 1.40;            /**< 分形维度判定阈值上限 */
        double fdThresholdAbsoluteMin = 1.10;        /**< 分形维度绝对最小阈值 */
        double transientCrestThreshold = 10.0;       /**< 瞬态峰值因子阈值 */
    };

    /** @brief 使用默认参数执行伪影削减
     *  @param input 原始输入信号
     *  @param sampleRate 采样率 (Hz)
     *  @returns 去伪影后的信号
     */
    static QVector<float> reduce(const QVector<float>& input, int sampleRate);

    /** @brief 使用自定义参数执行伪影削减
     *  @param input 原始输入信号
     *  @param sampleRate 采样率 (Hz)
     *  @param parameters 自定义参数
     *  @returns 去伪影后的信号
     */
    static QVector<float> reduce(
        const QVector<float>& input,
        int sampleRate,
        const Parameters& parameters);

    /** @brief 启用或禁用调试日志输出 */
    static void setDebugLoggingEnabled(bool enabled);
    /** @brief 查询调试日志是否已启用 */
    static bool debugLoggingEnabled();

private:
    /** @brief 根据采样率和样本数构造推荐参数 */
    static Parameters makeParameters(int sampleRate, int sampleCount);
};

#endif // MOTIONARTIFACTREDUCTION_H
