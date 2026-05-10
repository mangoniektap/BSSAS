#ifndef ACTIVENOISECANCELLATION_H
#define ACTIVENOISECANCELLATION_H

#include <QVector>

class ActiveNoiseCancellation
{
public:
    ActiveNoiseCancellation() = delete;

    struct Parameters
    {
        int filterLength = 0;
        int maxDelaySamples = 0;

        double minimumReferenceRms = 1e-7;
        double minimumReferenceValidRatio = 0.96;
        double minimumCorrelation = 0.10;
        double stepSize = 0.45;
        double normalizationEpsilon = 1e-6;
        double dcBlockerCutoffHz = 1.0;
    };

    struct Metrics
    {
        bool referenceValid = false;
        bool bypassed = false;
        bool adaptationFrozen = false;
        double referenceRms = 0.0;
        double targetRms = 0.0;
        double outputRms = 0.0;
        double correlation = 0.0;
        double stepSize = 0.0;
        int selectedDelaySamples = 0;
    };

    struct StreamingState
    {
        bool initialized = false;
        int sampleRate = 0;
        Parameters parameters;

        QVector<float> weights;
        QVector<float> referenceHistory;
        QVector<float> referenceAlignmentTail;
        QVector<float> pendingTarget;
        QVector<float> pendingReference;
        QVector<float> pendingReferenceValidFlags;

        int referenceHistoryWriteIndex = 0;
        double targetDcEstimate = 0.0;
        double referenceDcEstimate = 0.0;
        double lastCorrelation = 0.0;
        int selectedDelaySamples = 0;
        bool hasUsableWeights = false;
    };

    static Parameters makeParameters(int sampleRate);
    static QVector<float> cancel(
        const QVector<float>& targetSignal,
        const QVector<float>& referenceNoise,
        int sampleRate,
        StreamingState& state,
        Metrics* metrics = nullptr);
    static QVector<float> cancel(
        const QVector<float>& targetSignal,
        const QVector<float>& referenceNoise,
        int sampleRate,
        StreamingState& state,
        const Parameters& parameters,
        Metrics* metrics = nullptr);
    static QVector<float> flush(
        StreamingState& state,
        Metrics* metrics = nullptr);
    static void resetStreamingState(StreamingState& state);
    static void setDebugLoggingEnabled(bool enabled);
    static bool debugLoggingEnabled();
};

#endif // ACTIVENOISECANCELLATION_H
