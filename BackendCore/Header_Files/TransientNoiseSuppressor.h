/**
 * @file TransientNoiseSuppressor.h
 * @brief Offline transient noise suppression based on crest-factor detection and OLA synthesis.
 *
 * The suppressor processes a complete signal buffer at once. It detects short
 * high-energy transient frames, attenuates them, and protects low-frequency
 * bowel-sound components from excessive attenuation.
 */
#ifndef TRANSIENTNOISESUPPRESSOR_H
#define TRANSIENTNOISESUPPRESSOR_H

#include <QVector>

/**
 * @brief Offline transient noise suppressor.
 */
class TransientNoiseSuppressor
{
public:
    TransientNoiseSuppressor() = delete;

    /** @brief Tunable algorithm parameters. */
    struct Parameters
    {
        int     frameLength          = 256;   ///< Analysis frame length in samples.
        int     hopLength            = 128;   ///< Frame hop length in samples.
        double  transientThreshold   = 8.0;   ///< Crest-factor threshold for transient detection.
        double  minEnergyRatio       = 0.3;   ///< Minimum frame energy ratio relative to local mean.
        double  maxEnergyRatio       = 4.0;   ///< Maximum frame energy ratio relative to local mean.
        double  maxDurationMs        = 40.0;  ///< Maximum consecutive transient duration in milliseconds.
        double  attenuationGain      = 0.1;   ///< Gain applied to detected transient frames.
        double  bowelProtectFraction = 0.7;   ///< Reduce attenuation when low-frequency energy exceeds this fraction.
        double  lowFreqUpperHz       = 400.0; ///< Upper cutoff for low-frequency protection.
        double  rmsFloor             = 1e-6;  ///< Energy floor used to avoid silence-triggered false positives.
    };

    /**
     * @brief Suppress transient noise in a complete signal buffer.
     * @param input Input signal samples.
     * @param sampleRate Sampling rate in Hz.
     * @returns Suppressed signal samples with the same length as input.
     */
    static QVector<float> suppress(const QVector<float>& input, int sampleRate);

    /**
     * @brief Suppress transient noise in a complete signal buffer with explicit parameters.
     * @param input Input signal samples.
     * @param sampleRate Sampling rate in Hz.
     * @param params Algorithm parameters.
     * @returns Suppressed signal samples with the same length as input.
     */
    static QVector<float> suppress(const QVector<float>& input, int sampleRate,
                                   const Parameters& params);

    /** @brief Enable or disable debug metric logging. */
    static void setDebugLoggingEnabled(bool enabled);

    /** @brief Return whether debug metric logging is enabled. */
    static bool debugLoggingEnabled();

private:
    /** @brief Derive frame parameters from sampling rate and sample count. */
    static Parameters makeParameters(int sampleRate, int sampleCount = 0);
};

#endif // TRANSIENTNOISESUPPRESSOR_H
