/** @file AdaptiveNoiseReduction.h
 *  @brief WebRTC Audio Processing based adaptive noise reduction facade.
 */

#ifndef ADAPTIVENOISEREDUCTION_H
#define ADAPTIVENOISEREDUCTION_H

#include <QVector>

#include <api/audio/audio_processing.h>

/** @brief Adaptive noise reduction entry point used by the preprocessing pipeline. */
class AdaptiveNoiseReduction
{
public:
    AdaptiveNoiseReduction() = delete;

    enum NoiseSuppressionLevel
    {
        NoiseSuppressionLow = 0,
        NoiseSuppressionModerate = 1,
        NoiseSuppressionHigh = 2,
        NoiseSuppressionVeryHigh = 3
    };

    /** @brief User-facing WebRTC denoise parameters. */
    struct Parameters
    {
        int noiseSuppressionLevel = NoiseSuppressionModerate;
        bool highPassFilterEnabled = false;
        bool automaticGainControlEnabled = false;
        bool transientSuppressionEnabled = false;
    };

    /** @brief Streaming state retained across realtime chunks. */
    struct StreamingState
    {
        bool initialized = false;
        int sampleRate = 0;
        Parameters parameters;
        webrtc::scoped_refptr<webrtc::AudioProcessing> audioProcessing;
        QVector<float> pendingInput;
        QVector<float> pendingOutput;
    };

    static Parameters defaultParameters();

    static QVector<float> denoise(const QVector<float>& input, int sampleRate);
    static QVector<float> denoise(
        const QVector<float>& input,
        int sampleRate,
        const Parameters& parameters);

    static QVector<float> denoiseStreaming(
        const QVector<float>& input,
        int sampleRate,
        StreamingState& state);
    static QVector<float> denoiseStreaming(
        const QVector<float>& input,
        int sampleRate,
        StreamingState& state,
        const Parameters& parameters);

    static QVector<float> flushStreaming(int sampleRate, StreamingState& state);
    static QVector<float> flushStreaming(
        int sampleRate,
        StreamingState& state,
        const Parameters& parameters);

    static void resetStreamingState(StreamingState& state);

    static void setDebugLoggingEnabled(bool enabled);
    static bool debugLoggingEnabled();
};

#endif // ADAPTIVENOISEREDUCTION_H
