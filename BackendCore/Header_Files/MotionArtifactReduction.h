#ifndef MOTIONARTIFACTREDUCTION_H
#define MOTIONARTIFACTREDUCTION_H

#include <QVector>

class MotionArtifactReduction
{
public:
    MotionArtifactReduction() = delete;

    struct Parameters
    {
        int frameLength = 0;
        int hopLength = 0;
        int maxImfCount = 6;
        int maxSiftIterations = 8;
        int minimumExtremaCount = 2;
        int maxArtifactImfCount = 4;
        int fdWindowSamples = 0;
        int fdHopSamples = 0;

        double minimumImprovementRatio = 0.03;
        double lowerFrequencyHz = 0.5;
        double upperFrequencyHz = 25.0;
        double candidateBandEnergyRatio = 0.35;
        double attenuationFactor = 0.08;
        double softDecisionWidth = 0.25;
        double minimumOutputRmsRatio = 0.45;
        double fdThresholdMinimum = 1.05;
        double fdThresholdMaximum = 1.40;
        double fdThresholdAbsoluteMin = 1.10;
        double transientCrestThreshold = 10.0;
    };

    static QVector<float> reduce(const QVector<float>& input, int sampleRate);
    static QVector<float> reduce(
        const QVector<float>& input,
        int sampleRate,
        const Parameters& parameters);
    static void setDebugLoggingEnabled(bool enabled);
    static bool debugLoggingEnabled();

private:
    static Parameters makeParameters(int sampleRate, int sampleCount);
};

#endif // MOTIONARTIFACTREDUCTION_H
