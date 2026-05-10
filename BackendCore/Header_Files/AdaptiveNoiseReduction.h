#ifndef ADAPTIVENOISEREDUCTION_H
#define ADAPTIVENOISEREDUCTION_H

#include <QVector>

class AdaptiveNoiseReduction
{
public:
    AdaptiveNoiseReduction() = delete;

    struct Parameters
    {
        int frameLength = 0;
        int hopLength = 0;
        int minTrackingFrames = 0;
        int noiseInitFrameCount = 0;

        double spectrumSmoothing = 0.82;
        double noiseUpdateAlphaNoiseOnly = 0.72;
        double noiseUpdateAlphaSpeech = 0.98;
        double entropyTrackingRate = 0.12;
        double adaptiveAlphaMinimum = 0.35;
        double adaptiveAlphaMaximum = 0.98;
        double adaptiveAlphaZScoreRange = 2.5;

        double ratioSpeechPresenceLower = 1.15;
        double ratioSpeechPresenceUpper = 2.8;
        double posterioriSnrSpeechUpper = 8.0;

        double minGain = 0.15;
        double transientMinGain = 0.35;
        double transientCrestFactor = 4.5;
        double gainTemporalSmoothing = 0.62;
        double gainFrequencySmoothing = 0.16;
        double minimumOutputRmsRatio = 0.45;
        double minPrioriSnr = 1e-3;
        double maxPrioriSnr = 100.0;
    };

    struct StreamingState
    {
        bool initialized = false;
        int sampleRate = 0;
        Parameters parameters;

        QVector<double> window;
        QVector<double> analysisBuffer;
        QVector<double> synthesisOverlap;
        QVector<double> normalizationOverlap;

        QVector<double> smoothedPsd;
        QVector<double> noisePsd;
        QVector<double> minimaCurrent;
        QVector<double> minimaReference;
        QVector<double> previousGain;
        QVector<double> previousPosterioriSnr;
        QVector<double> entropyContributionMean;
        QVector<double> entropyContributionVariance;

        QVector<float> analysisFrame;
        QVector<double> powerSpectrum;

        int frameIndex = 0;
        int minimaFrameCounter = 0;
    };

    static QVector<float> denoise(const QVector<float>& input, int sampleRate);
    static QVector<float> denoiseStreaming(
        const QVector<float>& input,
        int sampleRate,
        StreamingState& state);
    static void resetStreamingState(StreamingState& state);
    static void setDebugLoggingEnabled(bool enabled);
    static bool debugLoggingEnabled();

private:
    static Parameters makeParameters(int sampleRate, int sampleCount);
};

#endif // ADAPTIVENOISEREDUCTION_H
