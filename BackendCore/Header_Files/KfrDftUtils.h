#ifndef KFRDFTUTILS_H
#define KFRDFTUTILS_H

#include <QVector>

#include <complex>

namespace KfrDftUtils {

enum class WindowFunction {
    Rectangular,
    Hann
};

struct RealDftSpectrumResult {
    int fftSize = 0;
    QVector<float> magnitudes;
};

RealDftSpectrumResult computeRealSpectrumMagnitudes(
    const QVector<float>& samples,
    WindowFunction windowFunction = WindowFunction::Hann,
    bool removeDcOffset = true);

QVector<std::complex<double>> computeComplexDft(const QVector<float>& signal);
QVector<double> computeInverseComplexDftReal(
    const QVector<std::complex<double>>& spectrum);

} // namespace KfrDftUtils

#endif // KFRDFTUTILS_H
