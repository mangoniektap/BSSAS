#ifndef FRACTALDIMENSIONUTILS_H
#define FRACTALDIMENSIONUTILS_H

#include <QVector>

namespace FractalDimensionUtils {

double computeWaveformComplexity(const QVector<double>& frame);
double computeWaveformComplexity(const QVector<float>& frame);

} // namespace FractalDimensionUtils

#endif // FRACTALDIMENSIONUTILS_H
