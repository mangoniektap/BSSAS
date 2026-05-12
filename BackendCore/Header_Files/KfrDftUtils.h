/** @file KfrDftUtils.h
 *  @brief KFR DFT 工具 —— 基于 KFR 框架的频谱计算与逆变换封装
 *
 *  本模块封装了 KFR (KFR Framework) 的 DFT/FFT 运算，
 *  提供实信号幅度谱计算、复数 DFT 及其逆变换等工具函数。
 *  支持矩形窗和 Hann 窗两种窗函数，可选择性去除直流偏置。
 */

#ifndef KFRDFTUTILS_H
#define KFRDFTUTILS_H

#include <QVector>

#include <complex>

/** @brief KFR DFT 工具命名空间 */
namespace KfrDftUtils {

/** @brief 窗函数类型枚举 */
enum class WindowFunction {
    Rectangular,    /**< 矩形窗（无窗） */
    Hann            /**< Hann 窗 */
};

/** @brief 实信号 DFT 频谱结果 */
struct RealDftSpectrumResult {
    int fftSize = 0;                 /**< FFT 点数 */
    QVector<float> magnitudes;       /**< 幅度谱（仅保留非负频率分量） */
};

/** @brief 计算实信号的幅度谱
 *  @param samples 输入实信号样本
 *  @param windowFunction 窗函数类型（默认 Hann）
 *  @param removeDcOffset 是否去除直流偏置（默认 true）
 *  @returns 包含 FFT 点数和幅度谱的结果结构体
 */
RealDftSpectrumResult computeRealSpectrumMagnitudes(
    const QVector<float>& samples,
    WindowFunction windowFunction = WindowFunction::Hann,
    bool removeDcOffset = true);

/** @brief 计算信号的复数 DFT
 *  @param signal 输入时域信号
 *  @returns 复数频谱向量
 */
QVector<std::complex<double>> computeComplexDft(const QVector<float>& signal);

/** @brief 计算复数频谱的逆 DFT（仅取实部）
 *  @param spectrum 输入复数频谱
 *  @returns 重建的实信号
 */
QVector<double> computeInverseComplexDftReal(
    const QVector<std::complex<double>>& spectrum);

} // namespace KfrDftUtils

#endif // KFRDFTUTILS_H
