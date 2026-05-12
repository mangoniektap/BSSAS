/** @file FractalDimensionUtils.h
 *  @brief 分形维度工具 —— 基于 Higuchi 算法的信号复杂度度量
 *
 *  本模块实现了 Higuchi 分形维度 (Fractal Dimension) 的计算。
 *  分形维度可用于量化生理信号（如肠鸣音）的波形复杂程度，
 *  作为特征提取的辅助指标。提供 double 和 float 两种数据类型的重载。
 */

#ifndef FRACTALDIMENSIONUTILS_H
#define FRACTALDIMENSIONUTILS_H

#include <QVector>

/** @brief 分形维度工具命名空间 */
namespace FractalDimensionUtils {

/** @brief 计算波形复杂度（Higuchi 分形维度）
 *  @param frame 输入信号帧 (double 精度)
 *  @returns Higuchi 分形维度值，范围通常为 1.0 ~ 2.0
 */
double computeWaveformComplexity(const QVector<double>& frame);

/** @brief 计算波形复杂度（Higuchi 分形维度）
 *  @param frame 输入信号帧 (float 精度)
 *  @returns Higuchi 分形维度值，范围通常为 1.0 ~ 2.0
 */
double computeWaveformComplexity(const QVector<float>& frame);

} // namespace FractalDimensionUtils

#endif // FRACTALDIMENSIONUTILS_H
