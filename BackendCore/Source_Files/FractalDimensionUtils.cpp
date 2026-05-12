/** @file FractalDimensionUtils.cpp
 *  @brief 分形维度工具函数实现。基于 Katz 算法计算波形帧的分形复杂度，
 *         用于肠鸣音信号的特征提取与分类辅助分析。
 */

#include "FractalDimensionUtils.h"

#include <algorithm>
#include <cmath>

namespace FractalDimensionUtils {

/** @brief 基于 Katz 算法计算波形帧的分形复杂度。
 *  @param frame 双精度浮点波形帧
 *  @returns 范围 [0, 1] 的分形复杂度，值越高表示波形越复杂
 */
double computeWaveformComplexity(const QVector<double>& frame)
{
    const int sampleCount = frame.size();
    if (sampleCount < 2) {
        return 0.0;
    }

    double length = 0.0;
    for (int index = 0; index < sampleCount - 1; ++index) {
        const double deltaY = frame[index + 1] - frame[index];
        length += std::sqrt(1.0 + deltaY * deltaY);
    }

    double d1 = 0.0;
    for (int index = 1; index < sampleCount; ++index) {
        const double deltaY = frame[index] - frame[0];
        d1 = std::max(
            d1,
            std::sqrt(static_cast<double>(index * index) + deltaY * deltaY));
    }

    const auto minmax = std::minmax_element(frame.cbegin(), frame.cend());
    const int minIndex =
        static_cast<int>(std::distance(frame.cbegin(), minmax.first));
    const int maxIndex =
        static_cast<int>(std::distance(frame.cbegin(), minmax.second));
    const double amplitudeDelta = *minmax.second - *minmax.first;
    const double d2 = std::sqrt(
        static_cast<double>((maxIndex - minIndex) * (maxIndex - minIndex)) +
        amplitudeDelta * amplitudeDelta);

    const double diameter = std::max(d1, d2);
    if (diameter <= 0.0 || length <= 0.0) {
        return 0.0;
    }

    const double sampleCountLog = std::log(static_cast<double>(sampleCount));
    return sampleCountLog /
        (sampleCountLog + std::log(diameter / length));
}

double computeWaveformComplexity(const QVector<float>& frame)
{
    QVector<double> frameAsDouble(frame.size(), 0.0);
    std::transform(
        frame.cbegin(),
        frame.cend(),
        frameAsDouble.begin(),
        [](float value) { return static_cast<double>(value); });
    return computeWaveformComplexity(frameAsDouble);
}

} // namespace FractalDimensionUtils
