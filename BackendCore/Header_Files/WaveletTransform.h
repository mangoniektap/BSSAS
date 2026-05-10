#ifndef WAVELETTRANSFORM_H
#define WAVELETTRANSFORM_H

#include <QVector>

class WaveletTransform
{
public:
    WaveletTransform() = delete;

    struct StreamingState
    {
        bool initialized = false;
        int sampleRate = 0;
        int historyLength = 0;
        int overlapLength = 0;
        QVector<float> historyInput;
    };

    // Result of a multi-level translation-invariant wavelet decomposition
    // (sym6 SWT / undecimated wavelet transform).
    // details[0] = finest scale (level-1) detail coefficients, etc.
    // approximation = coarsest low-pass residue.
    struct DecompositionResult
    {
        QVector<QVector<double>> details;
        QVector<double> approximation;
    };

    // Perform up to `levels` levels of translation-invariant sym6 SWT on
    // `signal`. Actual levels may be fewer if the signal is too short.
    static DecompositionResult decompose(const QVector<double>& signal, int levels = 3);

    // Translation-invariant sym6 SWT denoising with level-aware firm shrinkage
    // and RMS protection.
    static QVector<float> denoise(const QVector<float>& rawData, int sampleRate);
    static QVector<float> denoiseStreaming(
        const QVector<float>& rawData,
        int sampleRate,
        StreamingState& state);
    static void resetStreamingState(StreamingState& state);
    static void setDebugLoggingEnabled(bool enabled);
    static bool debugLoggingEnabled();
};

#endif // WAVELETTRANSFORM_H
