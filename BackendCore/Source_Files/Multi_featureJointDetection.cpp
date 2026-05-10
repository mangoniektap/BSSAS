#include "Multi_featureJointDetection.h"

#include "DataManager.h"
#include "FractalDimensionUtils.h"
#include "KfrDftUtils.h"
#include "GenerateManager.h"
#include "WAVHandle.h"
#include "WaveletTransform.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace {
using Segment = QPair<int, int>;

struct ImportedSignalData {
    int sampleRate = 0;
    QVector<float> samples;
};

struct SignalSummary {
    int sampleCount = 0;
    double minAmplitude = 0.0;
    double maxAmplitude = 0.0;
    double peakAmplitude = 0.0;
    double peakToPeakAmplitude = 0.0;
    double meanAmplitude = 0.0;
    double meanAbsoluteAmplitude = 0.0;
    double rms = 0.0;
};

struct FrameFeatureSummary {
    int frameCount = 0;
    double steMean = 0.0;
    double steMax = 0.0;
    double zcrMean = 0.0;
    double zcrMax = 0.0;
    double fdMean = 0.0;
    double fdMax = 0.0;
};

struct EventFeatureSummary {
    int frameCount = 0;
    int candidateFrameCount = 0;
    double peakLogEnergy = 0.0;
    double energyIntegral = 0.0;
    double envelopePeak = 0.0;
    double envelopeSlopeMean = 0.0;
    double envelopeShape = 0.0;
    double transientPeak = 0.0;
    double transientMean = 0.0;
    double kurtosisMean = 0.0;
    double spectralEntropyMean = 0.0;
    double spectralEntropyStd = 0.0;
    double spectralCentroidMean = 0.0;
    double bandwidthMean = 0.0;
    double rolloffMean = 0.0;
    double dominantFreqMean = 0.0;
    double subbandLowEnergy = 0.0;
    double subbandMidEnergy = 0.0;
    double subbandHighEnergy = 0.0;
    double subbandEntropy = 0.0;
    double multiscaleEnergy1 = 0.0;
    double multiscaleEnergy2 = 0.0;
    double multiscaleEnergy3 = 0.0;
    double waveletEntropyMean = 0.0;
    double logMel1Mean = 0.0;
    double logMel2Mean = 0.0;
    double logMel3Mean = 0.0;
    double fdWeakMean = 0.0;
    double candidateScoreMean = 0.0;
    double maxCandidateScore = 0.0;
    double preSilenceLogEnergy = 0.0;
    double postSilenceLogEnergy = 0.0;
    double silenceContrast = 0.0;
};

struct EventDecisionResult {
    bool passedRuleLayer = false;
    bool accepted = false;
    double linearScore = 0.0;
    double quadraticPenalty = 0.0;
    double svmLikeMargin = 0.0;
    double randomForestLikeVote = 0.0;
    double finalScore = 0.0;
};

struct FrequencySubbandEnergies {
    double low = 0.0;
    double mid = 0.0;
    double high = 0.0;
};

constexpr double kFeatureEpsilon = 1e-12;
constexpr double kPi = 3.14159265358979323846;
constexpr double kSubbandLowUpperHz = 150.0;
constexpr double kSubbandMidUpperHz = 500.0;
constexpr double kSubbandHighUpperHz = 1200.0;

double safeRatio(double numerator, double denominator)
{
    return numerator / std::max(denominator, kFeatureEpsilon);
}

double safeMean(const QVector<double>& values)
{
    if (values.isEmpty()) {
        return 0.0;
    }

    const double sum = std::accumulate(values.cbegin(), values.cend(), 0.0);
    return sum / static_cast<double>(values.size());
}

double safeStd(const QVector<double>& values, double meanValue)
{
    if (values.size() < 2) {
        return 0.0;
    }

    double sumSquares = 0.0;
    for (double value : values) {
        const double centered = value - meanValue;
        sumSquares += centered * centered;
    }

    return std::sqrt(sumSquares / static_cast<double>(values.size() - 1));
}

double normalizedEntropy(const QVector<double>& nonNegativeValues)
{
    if (nonNegativeValues.isEmpty()) {
        return 0.0;
    }

    double total = 0.0;
    for (double value : nonNegativeValues) {
        total += std::max(0.0, value);
    }
    if (total <= kFeatureEpsilon) {
        return 0.0;
    }

    double entropy = 0.0;
    int nonZeroCount = 0;
    for (double value : nonNegativeValues) {
        const double probability = std::max(0.0, value) / total;
        if (probability <= 0.0) {
            continue;
        }

        entropy -= probability * std::log(probability);
        ++nonZeroCount;
    }

    if (nonZeroCount < 2) {
        return 0.0;
    }

    return entropy / std::log(static_cast<double>(nonZeroCount));
}

double logisticScore(double value)
{
    return 1.0 / (1.0 + std::exp(-value));
}

double elapsedMilliseconds(const QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
}

double cappedZScore(double value, double meanValue, double stdValue)
{
    if (stdValue <= kFeatureEpsilon) {
        return 0.0;
    }

    return std::clamp((value - meanValue) / stdValue, -4.0, 6.0);
}

FrequencySubbandEnergies computeFrequencySubbandEnergies(
    const QVector<float>& magnitudes,
    double frequencyResolution)
{
    FrequencySubbandEnergies energies;
    if (magnitudes.isEmpty() || frequencyResolution <= 0.0) {
        return energies;
    }

    for (int binIndex = 0; binIndex < magnitudes.size(); ++binIndex) {
        const double frequency = static_cast<double>(binIndex) * frequencyResolution;
        if (frequency > kSubbandHighUpperHz) {
            break;
        }

        const double magnitude = std::max(0.0, static_cast<double>(magnitudes[binIndex]));
        if (frequency < kSubbandLowUpperHz) {
            energies.low += magnitude;
        } else if (frequency < kSubbandMidUpperHz) {
            energies.mid += magnitude;
        } else {
            energies.high += magnitude;
        }
    }

    return energies;
}

// Standard log-Mel filterbank statistics.
// Converts linear-scale FFT magnitudes to log-Mel energies and aggregates them
// into numStats statistics across low/mid/high Mel bands.
QVector<double> computeLogMelStatistics(
    const QVector<float>& magnitudes,
    int fftSize,
    int sampleRate,
    int numFilters,
    int numStats)
{
    QVector<double> result(numStats, 0.0);
    if (magnitudes.isEmpty() || fftSize <= 0 || sampleRate <= 0 ||
        numFilters <= 0 || numStats <= 0) {
        return result;
    }

    constexpr double kMelLowHz = 80.0;
    const double fHigh = static_cast<double>(sampleRate) * 0.5;
    const double mLow = 2595.0 * std::log10(1.0 + kMelLowHz / 700.0);
    const double mHigh = 2595.0 * std::log10(1.0 + fHigh / 700.0);

    // Build numFilters+2 equally-spaced Mel points -> FFT bin indices.
    const int numPoints = numFilters + 2;
    QVector<int> bins(numPoints);
    const int maxBin = static_cast<int>(magnitudes.size()) - 1;
    for (int i = 0; i < numPoints; ++i) {
        const double mel = mLow + static_cast<double>(i) * (mHigh - mLow) /
            static_cast<double>(numPoints - 1);
        const double hz = 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0);
        const int bin = static_cast<int>(
            std::round(hz * static_cast<double>(fftSize) / static_cast<double>(sampleRate)));
        bins[i] = std::clamp(bin, 0, maxBin);
    }

    // Compute log Mel filter energies using triangular windows.
    QVector<double> logMelEnergies(numFilters, 0.0);
    for (int m = 0; m < numFilters; ++m) {
        const int lo     = bins[m];
        const int center = bins[m + 1];
        const int hi     = bins[m + 2];
        double energy = 0.0;
        for (int k = lo; k <= hi && k <= maxBin; ++k) {
            const double mag = std::max(0.0, static_cast<double>(magnitudes[k]));
            double weight = 0.0;
            if (center > lo && k >= lo && k <= center) {
                weight = static_cast<double>(k - lo) / static_cast<double>(center - lo);
            } else if (hi > center && k > center && k <= hi) {
                weight = static_cast<double>(hi - k) / static_cast<double>(hi - center);
            }
            energy += weight * mag * mag;
        }
        logMelEnergies[m] = std::log(std::max(energy, kFeatureEpsilon));
    }

    // Aggregate contiguous Mel regions (e.g. low/mid/high) as simple statistics.
    for (int statIndex = 0; statIndex < numStats; ++statIndex) {
        const int startFilter =
            (statIndex * numFilters) / numStats;
        const int endFilterExclusive =
            ((statIndex + 1) * numFilters) / numStats;
        if (endFilterExclusive <= startFilter) {
            continue;
        }

        double sum = 0.0;
        for (int filterIndex = startFilter; filterIndex < endFilterExclusive; ++filterIndex) {
            sum += logMelEnergies[filterIndex];
        }
        const double count = static_cast<double>(endFilterExclusive - startFilter);
        result[statIndex] = sum / count;
    }

    return result;
}

double stftEntropyAtTimeSeconds(double centerSeconds)
{
    DataManager* dataManager = DataManager::instance();
    if (dataManager == nullptr) {
        return 0.0;
    }

    const double boundaryHz = dataManager->importedSpectrumBoundary();
    if (boundaryHz <= 0.0) {
        return 0.0;
    }

    const QVariantList points = dataManager->importedSpectrumPointsAtTime(
        0.0,
        boundaryHz,
        std::max(0.0, centerSeconds));
    if (points.isEmpty()) {
        return 0.0;
    }

    QVector<double> magnitudes;
    magnitudes.reserve(points.size());
    for (const QVariant& pointValue : points) {
        const QPointF point = pointValue.toPointF();
        magnitudes.append(std::max(0.0, point.y()));
    }
    return normalizedEntropy(magnitudes);
}

ImportedSignalData readImportedSignal(const QString& temporaryFilePath)
{
    ImportedSignalData result;
    QFile file(temporaryFilePath);
    if (temporaryFilePath.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return result;
    }

    const QByteArray headerBytes = file.read(static_cast<qint64>(sizeof(float)));
    if (headerBytes.size() != sizeof(float)) {
        return result;
    }

    float sampleRateValue = 0.0f;
    std::memcpy(&sampleRateValue, headerBytes.constData(), sizeof(float));
    result.sampleRate = static_cast<int>(std::lround(sampleRateValue));

    const QByteArray sampleBytes = file.readAll();
    const int sampleCount = sampleBytes.size() / static_cast<int>(sizeof(float));
    if (result.sampleRate <= 0 || sampleCount <= 0) {
        result.sampleRate = 0;
        return result;
    }

    result.samples.resize(sampleCount);
    std::memcpy(
        result.samples.data(),
        sampleBytes.constData(),
        static_cast<size_t>(sampleCount * sizeof(float)));
    return result;
}

QVariantList toVariantList(const QVector<double>& values)
{
    QVariantList list;
    list.reserve(values.size());
    for (double value : values) {
        list.append(value);
    }
    return list;
}

SignalSummary computeSignalSummary(
    const QVector<double>& signal,
    int startSample,
    int endSample)
{
    SignalSummary summary;
    if (signal.isEmpty()) {
        return summary;
    }

    const int signalSize = static_cast<int>(signal.size());
    const int clampedStart = std::clamp(startSample, 0, signalSize);
    const int clampedEnd = std::clamp(endSample, clampedStart, signalSize);
    const int sampleCount = clampedEnd - clampedStart;
    if (sampleCount <= 0) {
        return summary;
    }

    summary.sampleCount = sampleCount;
    summary.minAmplitude = signal[clampedStart];
    summary.maxAmplitude = signal[clampedStart];

    double sum = 0.0;
    double sumAbsolute = 0.0;
    double sumSquares = 0.0;
    for (int sampleIndex = clampedStart; sampleIndex < clampedEnd; ++sampleIndex) {
        const double sample = signal[sampleIndex];
        summary.minAmplitude = std::min(summary.minAmplitude, sample);
        summary.maxAmplitude = std::max(summary.maxAmplitude, sample);
        sum += sample;
        sumAbsolute += std::abs(sample);
        sumSquares += sample * sample;
    }

    summary.peakAmplitude = std::max(
        std::abs(summary.minAmplitude),
        std::abs(summary.maxAmplitude));
    summary.peakToPeakAmplitude = summary.maxAmplitude - summary.minAmplitude;
    summary.meanAmplitude = sum / static_cast<double>(sampleCount);
    summary.meanAbsoluteAmplitude = sumAbsolute / static_cast<double>(sampleCount);
    summary.rms = std::sqrt(sumSquares / static_cast<double>(sampleCount));
    return summary;
}

FrameFeatureSummary computeFrameFeatureSummary(
    const QVector<double>& steValues,
    const QVector<double>& zcrValues,
    const QVector<double>& fdValues,
    int startSample,
    int endSample,
    int frameLengthSamples,
    int frameShiftSamples)
{
    FrameFeatureSummary summary;
    const int frameCount = static_cast<int>(
        std::min({ steValues.size(), zcrValues.size(), fdValues.size() }));
    if (frameCount <= 0 || frameLengthSamples <= 0 || frameShiftSamples <= 0) {
        return summary;
    }

    double steSum = 0.0;
    double zcrSum = 0.0;
    double fdSum = 0.0;

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const int frameStartSample = frameIndex * frameShiftSamples;
        const int frameEndSample = frameStartSample + frameLengthSamples;
        if (frameEndSample <= startSample || frameStartSample >= endSample) {
            continue;
        }

        const double steValue = steValues[frameIndex];
        const double zcrValue = zcrValues[frameIndex];
        const double fdValue = fdValues[frameIndex];

        if (summary.frameCount == 0) {
            summary.steMax = steValue;
            summary.zcrMax = zcrValue;
            summary.fdMax = fdValue;
        } else {
            summary.steMax = std::max(summary.steMax, steValue);
            summary.zcrMax = std::max(summary.zcrMax, zcrValue);
            summary.fdMax = std::max(summary.fdMax, fdValue);
        }

        steSum += steValue;
        zcrSum += zcrValue;
        fdSum += fdValue;
        ++summary.frameCount;
    }

    if (summary.frameCount > 0) {
        const double frameCountDouble = static_cast<double>(summary.frameCount);
        summary.steMean = steSum / frameCountDouble;
        summary.zcrMean = zcrSum / frameCountDouble;
        summary.fdMean = fdSum / frameCountDouble;
    }

    return summary;
}

QVariantMap buildRecognizedEvent(
    const Segment& segment,
    int eventIndex,
    int sampleRate,
    const QVector<double>& rawSignal,
    const QVector<double>& steValues,
    const QVector<double>& zcrValues,
    const QVector<double>& fdValues,
    const QVector<double>& logSteValues,
    const QVector<double>& envPeakValues,
    const QVector<double>& envSlopeValues,
    const QVector<double>& kurtosisValues,
    const QVector<double>& subbandLowValues,
    const QVector<double>& subbandMidValues,
    const QVector<double>& subbandHighValues,
    const QVector<double>& dominantFreqValues,
    const QVector<double>& spectralCentroidValues,
    const QVector<double>& spectralBandwidthValues,
    const QVector<double>& spectralRolloffValues,
    const QVector<double>& spectralEntropyValues,
    const QVector<double>& multiscaleEnergy1Values,
    const QVector<double>& multiscaleEnergy2Values,
    const QVector<double>& multiscaleEnergy3Values,
    const QVector<double>& waveletEntropyValues,
    const QVector<double>& transientStrengthValues,
    const QVector<double>& logMel1Values,
    const QVector<double>& logMel2Values,
    const QVector<double>& logMel3Values,
    const QVector<double>& candidateScoreValues,
    const QVector<bool>& candidateMask,
    double logEnergyBackgroundMean,
    double logEnergyThreshold,
    double entropyThreshold,
    int frameLengthSamples,
    int frameShiftSamples)
{
    const int startSample = std::max(0, segment.first);
    const int endSample = std::max(startSample, segment.second);
    const SignalSummary signalSummary =
        computeSignalSummary(rawSignal, startSample, endSample);
    const FrameFeatureSummary frameSummary =
        computeFrameFeatureSummary(
            steValues,
            zcrValues,
            fdValues,
            startSample,
            endSample,
            frameLengthSamples,
            frameShiftSamples);

    EventFeatureSummary eventSummary;
    const int firstFrame = std::max(0, startSample / std::max(1, frameShiftSamples));
    const int lastFrameExclusive = std::max(
        firstFrame,
        static_cast<int>(std::ceil(
            static_cast<double>(endSample) / static_cast<double>(std::max(1, frameShiftSamples)))));
    const int totalFrameCount = static_cast<int>(
        std::min({
            logSteValues.size(),
            envPeakValues.size(),
            envSlopeValues.size(),
            kurtosisValues.size(),
            subbandLowValues.size(),
            subbandMidValues.size(),
            subbandHighValues.size(),
            dominantFreqValues.size(),
            spectralCentroidValues.size(),
            spectralBandwidthValues.size(),
            spectralRolloffValues.size(),
            spectralEntropyValues.size(),
            multiscaleEnergy1Values.size(),
            multiscaleEnergy2Values.size(),
            multiscaleEnergy3Values.size(),
            waveletEntropyValues.size(),
            transientStrengthValues.size(),
            logMel1Values.size(),
            logMel2Values.size(),
            logMel3Values.size(),
            candidateScoreValues.size(),
            fdValues.size()
        }));
    if (totalFrameCount > 0 && lastFrameExclusive > firstFrame) {
        const int clampedLastFrame = std::min(lastFrameExclusive, totalFrameCount);

        double sumEnvelopeSlope = 0.0;
        double sumTransient = 0.0;
        double sumKurtosis = 0.0;
        double sumEntropy = 0.0;
        double sumEntropySquared = 0.0;
        double sumCentroid = 0.0;
        double sumBandwidth = 0.0;
        double sumRolloff = 0.0;
        double sumDominantFreq = 0.0;
        double sumWaveletEntropy = 0.0;
        double sumCep1 = 0.0;
        double sumCep2 = 0.0;
        double sumCep3 = 0.0;
        double sumFdWeak = 0.0;
        double sumCandidateScore = 0.0;

        for (int frameIndex = firstFrame; frameIndex < clampedLastFrame; ++frameIndex) {
            const double logEnergy = logSteValues[frameIndex];
            const double envelopePeak = envPeakValues[frameIndex];
            const double envelopeSlope = envSlopeValues[frameIndex];
            const double transientStrength = transientStrengthValues[frameIndex];
            const double candidateScore = candidateScoreValues[frameIndex];

            if (eventSummary.frameCount == 0) {
                eventSummary.peakLogEnergy = logEnergy;
                eventSummary.envelopePeak = envelopePeak;
                eventSummary.transientPeak = transientStrength;
                eventSummary.maxCandidateScore = candidateScore;
            } else {
                eventSummary.peakLogEnergy = std::max(eventSummary.peakLogEnergy, logEnergy);
                eventSummary.envelopePeak = std::max(eventSummary.envelopePeak, envelopePeak);
                eventSummary.transientPeak = std::max(eventSummary.transientPeak, transientStrength);
                eventSummary.maxCandidateScore = std::max(eventSummary.maxCandidateScore, candidateScore);
            }

            eventSummary.energyIntegral += std::exp(logEnergy) - 1.0;
            eventSummary.subbandLowEnergy += subbandLowValues[frameIndex];
            eventSummary.subbandMidEnergy += subbandMidValues[frameIndex];
            eventSummary.subbandHighEnergy += subbandHighValues[frameIndex];
            eventSummary.multiscaleEnergy1 += multiscaleEnergy1Values[frameIndex];
            eventSummary.multiscaleEnergy2 += multiscaleEnergy2Values[frameIndex];
            eventSummary.multiscaleEnergy3 += multiscaleEnergy3Values[frameIndex];

            sumEnvelopeSlope += envelopeSlope;
            sumTransient += transientStrength;
            sumKurtosis += kurtosisValues[frameIndex];
            sumEntropy += spectralEntropyValues[frameIndex];
            sumEntropySquared += spectralEntropyValues[frameIndex] * spectralEntropyValues[frameIndex];
            sumCentroid += spectralCentroidValues[frameIndex];
            sumBandwidth += spectralBandwidthValues[frameIndex];
            sumRolloff += spectralRolloffValues[frameIndex];
            sumDominantFreq += dominantFreqValues[frameIndex];
            sumWaveletEntropy += waveletEntropyValues[frameIndex];
            sumCep1 += logMel1Values[frameIndex];
            sumCep2 += logMel2Values[frameIndex];
            sumCep3 += logMel3Values[frameIndex];
            sumFdWeak += fdValues[frameIndex];
            sumCandidateScore += candidateScore;

            if (frameIndex < candidateMask.size() && candidateMask[frameIndex]) {
                ++eventSummary.candidateFrameCount;
            }
            ++eventSummary.frameCount;
        }

        if (eventSummary.frameCount > 0) {
            const double frameCountDouble = static_cast<double>(eventSummary.frameCount);
            eventSummary.envelopeSlopeMean = sumEnvelopeSlope / frameCountDouble;
            eventSummary.transientMean = sumTransient / frameCountDouble;
            eventSummary.kurtosisMean = sumKurtosis / frameCountDouble;
            eventSummary.spectralEntropyMean = sumEntropy / frameCountDouble;
            eventSummary.spectralEntropyStd = std::sqrt(std::max(
                0.0,
                sumEntropySquared / frameCountDouble -
                    eventSummary.spectralEntropyMean * eventSummary.spectralEntropyMean));
            eventSummary.spectralCentroidMean = sumCentroid / frameCountDouble;
            eventSummary.bandwidthMean = sumBandwidth / frameCountDouble;
            eventSummary.rolloffMean = sumRolloff / frameCountDouble;
            eventSummary.dominantFreqMean = sumDominantFreq / frameCountDouble;
            eventSummary.waveletEntropyMean = sumWaveletEntropy / frameCountDouble;
            eventSummary.logMel1Mean = sumCep1 / frameCountDouble;
            eventSummary.logMel2Mean = sumCep2 / frameCountDouble;
            eventSummary.logMel3Mean = sumCep3 / frameCountDouble;
            eventSummary.fdWeakMean = sumFdWeak / frameCountDouble;
            eventSummary.candidateScoreMean = sumCandidateScore / frameCountDouble;
            eventSummary.energyIntegral *=
                static_cast<double>(frameShiftSamples) / static_cast<double>(std::max(1, sampleRate));

            const QVector<double> subbandDistribution {
                std::max(0.0, eventSummary.subbandLowEnergy),
                std::max(0.0, eventSummary.subbandMidEnergy),
                std::max(0.0, eventSummary.subbandHighEnergy)
            };
            eventSummary.subbandEntropy = normalizedEntropy(subbandDistribution);

            int peakFrame = firstFrame;
            double peakValue = logSteValues[firstFrame];
            for (int frameIndex = firstFrame + 1; frameIndex < clampedLastFrame; ++frameIndex) {
                if (logSteValues[frameIndex] > peakValue) {
                    peakValue = logSteValues[frameIndex];
                    peakFrame = frameIndex;
                }
            }
            const int durationFrames = std::max(1, clampedLastFrame - firstFrame);
            eventSummary.envelopeShape = static_cast<double>(peakFrame - firstFrame) /
                static_cast<double>(durationFrames);

            const int silenceFrames = std::max(1, static_cast<int>(
                std::lround(0.2 * static_cast<double>(sampleRate) /
                            static_cast<double>(std::max(1, frameShiftSamples)))));
            double preSilenceSum = 0.0;
            int preSilenceCount = 0;
            for (int frameIndex = std::max(0, firstFrame - silenceFrames);
                 frameIndex < firstFrame && frameIndex < logSteValues.size();
                 ++frameIndex) {
                preSilenceSum += logSteValues[frameIndex];
                ++preSilenceCount;
            }
            double postSilenceSum = 0.0;
            int postSilenceCount = 0;
            for (int frameIndex = clampedLastFrame;
                 frameIndex < std::min(
                     clampedLastFrame + silenceFrames,
                     static_cast<int>(logSteValues.size()));
                 ++frameIndex) {
                postSilenceSum += logSteValues[frameIndex];
                ++postSilenceCount;
            }
            eventSummary.preSilenceLogEnergy =
                preSilenceCount > 0 ? preSilenceSum / static_cast<double>(preSilenceCount)
                                    : logEnergyBackgroundMean;
            eventSummary.postSilenceLogEnergy =
                postSilenceCount > 0 ? postSilenceSum / static_cast<double>(postSilenceCount)
                                     : logEnergyBackgroundMean;
            const double silenceBaseline = 0.5 *
                (eventSummary.preSilenceLogEnergy + eventSummary.postSilenceLogEnergy);
            eventSummary.silenceContrast = eventSummary.peakLogEnergy - silenceBaseline;
        }
    }

    EventDecisionResult decision;
    const double durationMs =
        sampleRate > 0
            ? static_cast<double>(endSample - startSample) * 1000.0 /
                static_cast<double>(sampleRate)
            : 0.0;
    const bool durationRule = durationMs >= 20.0 && durationMs <= 2200.0;
    const bool contrastRule = eventSummary.silenceContrast > 0.25;
    const bool entropyRule = eventSummary.spectralEntropyMean < (entropyThreshold + 0.08);
    const bool transientRule = eventSummary.transientPeak > 0.0;
    const bool energyRule = eventSummary.peakLogEnergy > logEnergyThreshold;
    const double subbandMidHighRatio = safeRatio(
        eventSummary.subbandMidEnergy,
        eventSummary.subbandHighEnergy);
    const bool spectralShapeRule =
        subbandMidHighRatio > 0.35 || eventSummary.spectralCentroidMean < 1200.0;
    decision.passedRuleLayer =
        durationRule && contrastRule && entropyRule && transientRule && energyRule &&
        spectralShapeRule;

    const double durationNorm = std::clamp(durationMs / 600.0, 0.0, 3.0);
    const double energyNorm = std::clamp(
        eventSummary.peakLogEnergy - logEnergyBackgroundMean,
        -1.0,
        6.0);
    const double transientNorm = std::clamp(eventSummary.transientPeak, 0.0, 6.0);
    const double entropyNorm = std::clamp(eventSummary.spectralEntropyMean, 0.0, 1.5);
    const double contrastNorm = std::clamp(eventSummary.silenceContrast, -1.0, 6.0);
    const double kurtosisNorm = std::clamp(eventSummary.kurtosisMean / 6.0, 0.0, 2.0);
    const double waveletNorm = std::clamp(1.0 - eventSummary.waveletEntropyMean, -0.5, 1.5);
    const double multiscaleBalance = std::clamp(
        safeRatio(
            eventSummary.multiscaleEnergy2,
            eventSummary.multiscaleEnergy1 + eventSummary.multiscaleEnergy3),
        0.0,
        3.0);
    const double frequencyFocus = std::clamp(
        1.0 - std::abs(eventSummary.dominantFreqMean - 450.0) / 1200.0,
        -1.0,
        1.0);
    const double logMelNorm = std::clamp(
        0.22 * std::abs(eventSummary.logMel1Mean) +
            0.18 * std::abs(eventSummary.logMel2Mean) +
            0.10 * std::abs(eventSummary.logMel3Mean),
        0.0,
        2.0);
    const double spectralShapeNorm = std::clamp(
        0.28 * subbandMidHighRatio +
            0.18 * safeRatio(eventSummary.subbandLowEnergy, eventSummary.subbandMidEnergy) +
            0.10 * std::clamp(1.0 - eventSummary.bandwidthMean / 1500.0, -1.0, 1.0) +
            0.08 * frequencyFocus +
            0.06 * std::clamp(1.0 - eventSummary.rolloffMean / 1800.0, -1.0, 1.0),
        -1.0,
        2.5);
    const double candidateDensity =
        eventSummary.frameCount > 0
            ? static_cast<double>(eventSummary.candidateFrameCount) /
                static_cast<double>(eventSummary.frameCount)
            : 0.0;

    decision.linearScore =
        0.62 * energyNorm +
        0.48 * contrastNorm +
        0.30 * transientNorm +
        0.22 * (1.0 - entropyNorm) +
        0.20 * eventSummary.candidateScoreMean +
        0.16 * waveletNorm +
        0.13 * multiscaleBalance +
        0.14 * spectralShapeNorm +
        0.12 * logMelNorm +
        0.10 * kurtosisNorm +
        0.15 * eventSummary.fdWeakMean +
        0.10 * candidateDensity;
    decision.quadraticPenalty =
        -0.28 * std::pow(durationNorm - 0.55, 2.0) -
        0.12 * std::pow(eventSummary.envelopeShape - 0.5, 2.0);
    decision.svmLikeMargin = logisticScore(
        0.75 * energyNorm +
        0.55 * contrastNorm +
        0.35 * transientNorm -
        0.65 * entropyNorm -
        0.22 * std::clamp(eventSummary.waveletEntropyMean - 0.55, -1.0, 1.0) +
        0.15 * std::clamp(multiscaleBalance - 0.8, -1.0, 2.0) +
        0.14 * std::clamp(subbandMidHighRatio - 0.5, -1.0, 2.0) -
        0.22 * std::abs(durationNorm - 0.6));

    int voteCount = 0;
    voteCount += (eventSummary.silenceContrast > 0.30) ? 1 : 0;
    voteCount += (eventSummary.subbandMidEnergy > eventSummary.subbandHighEnergy * 0.45) ? 1 : 0;
    voteCount += (eventSummary.spectralEntropyStd > 0.02) ? 1 : 0;
    voteCount += (candidateDensity > 0.35) ? 1 : 0;
    voteCount += (durationMs > 35.0 && durationMs < 1400.0) ? 1 : 0;
    voteCount += (eventSummary.waveletEntropyMean < 0.85) ? 1 : 0;
    voteCount += (std::abs(eventSummary.logMel1Mean) > 0.06) ? 1 : 0;
    decision.randomForestLikeVote = static_cast<double>(voteCount) / 7.0;

    decision.finalScore =
        decision.linearScore +
        decision.quadraticPenalty +
        0.65 * decision.svmLikeMargin +
        0.45 * decision.randomForestLikeVote;

    const double eventCenterSeconds =
        sampleRate > 0
            ? 0.5 *
                (static_cast<double>(startSample) + static_cast<double>(endSample)) /
                static_cast<double>(sampleRate)
            : 0.0;
    const double cachedStftEntropy = stftEntropyAtTimeSeconds(eventCenterSeconds);
    const double effectiveStftEntropy =
        cachedStftEntropy > 0.0 ? cachedStftEntropy : eventSummary.spectralEntropyMean;
    decision.finalScore += 0.08 * std::max(0.0, entropyThreshold - effectiveStftEntropy);
    decision.accepted = decision.passedRuleLayer && decision.finalScore > 1.15;

    // FD stands for fractal dimension here, not frequency, so the high-frequency
    // label must be driven only by Hz-domain spectral indicators.
    const bool isHighFrequencyEvent =
        eventSummary.dominantFreqMean >= 300.0 ||
        eventSummary.spectralCentroidMean >= 300.0;
    const QString acousticEventType = isHighFrequencyEvent
        ? QStringLiteral("高频/亢进音")
        : QStringLiteral("典型肠鸣音");

    QVariantMap item;
    item["eventIndex"] = eventIndex + 1;
    item["startSample"] = startSample;
    item["endSample"] = endSample;
    item["startSeconds"] =
        sampleRate > 0
            ? static_cast<double>(startSample) / static_cast<double>(sampleRate)
            : 0.0;
    item["endSeconds"] =
        sampleRate > 0
            ? static_cast<double>(endSample) / static_cast<double>(sampleRate)
            : 0.0;
    item["durationMs"] =
        sampleRate > 0
            ? static_cast<double>(endSample - startSample) * 1000.0 /
                static_cast<double>(sampleRate)
            : 0.0;
    item["sampleCount"] = signalSummary.sampleCount;
    item["minAmplitude"] = signalSummary.minAmplitude;
    item["maxAmplitude"] = signalSummary.maxAmplitude;
    item["peakAmplitude"] = signalSummary.peakAmplitude;
    item["peakToPeakAmplitude"] = signalSummary.peakToPeakAmplitude;
    item["meanAmplitude"] = signalSummary.meanAmplitude;
    item["meanAbsoluteAmplitude"] = signalSummary.meanAbsoluteAmplitude;
    item["rms"] = signalSummary.rms;
    item["frameCount"] = frameSummary.frameCount;
    item["steMean"] = frameSummary.steMean;
    item["steMax"] = frameSummary.steMax;
    item["zcrMean"] = frameSummary.zcrMean;
    item["zcrMax"] = frameSummary.zcrMax;
    item["fdMean"] = frameSummary.fdMean;
    item["fdMax"] = frameSummary.fdMax;
    item["peakLogEnergy"] = eventSummary.peakLogEnergy;
    item["energyIntegral"] = eventSummary.energyIntegral;
    item["envelopePeak"] = eventSummary.envelopePeak;
    item["envelopeSlopeMean"] = eventSummary.envelopeSlopeMean;
    item["envelopeShape"] = eventSummary.envelopeShape;
    item["transientPeak"] = eventSummary.transientPeak;
    item["transientMean"] = eventSummary.transientMean;
    item["kurtosisMean"] = eventSummary.kurtosisMean;
    item["spectralEntropyMean"] = eventSummary.spectralEntropyMean;
    item["spectralEntropyStd"] = eventSummary.spectralEntropyStd;
    item["spectralCentroidMean"] = eventSummary.spectralCentroidMean;
    item["bandwidthMean"] = eventSummary.bandwidthMean;
    item["rolloffMean"] = eventSummary.rolloffMean;
    item["dominantFreqMean"] = eventSummary.dominantFreqMean;
    item["subbandLowEnergy"] = eventSummary.subbandLowEnergy;
    item["subbandMidEnergy"] = eventSummary.subbandMidEnergy;
    item["subbandHighEnergy"] = eventSummary.subbandHighEnergy;
    item["subbandEntropy"] = eventSummary.subbandEntropy;
    item["multiscaleEnergy1"] = eventSummary.multiscaleEnergy1;
    item["multiscaleEnergy2"] = eventSummary.multiscaleEnergy2;
    item["multiscaleEnergy3"] = eventSummary.multiscaleEnergy3;
    item["waveletEntropyMean"] = eventSummary.waveletEntropyMean;
    item["logMel1Mean"] = eventSummary.logMel1Mean;
    item["logMel2Mean"] = eventSummary.logMel2Mean;
    item["logMel3Mean"] = eventSummary.logMel3Mean;
    item["fdWeakMean"] = eventSummary.fdWeakMean;
    item["candidateFrameCount"] = eventSummary.candidateFrameCount;
    item["candidateDensity"] =
        eventSummary.frameCount > 0
            ? static_cast<double>(eventSummary.candidateFrameCount) /
                static_cast<double>(eventSummary.frameCount)
            : 0.0;
    item["candidateScoreMean"] = eventSummary.candidateScoreMean;
    item["maxCandidateScore"] = eventSummary.maxCandidateScore;
    item["preSilenceLogEnergy"] = eventSummary.preSilenceLogEnergy;
    item["postSilenceLogEnergy"] = eventSummary.postSilenceLogEnergy;
    item["silenceContrast"] = eventSummary.silenceContrast;
    item["passedRuleLayer"] = decision.passedRuleLayer;
    item["linearScore"] = decision.linearScore;
    item["quadraticPenalty"] = decision.quadraticPenalty;
    item["svmLikeMargin"] = decision.svmLikeMargin;
    item["randomForestLikeVote"] = decision.randomForestLikeVote;
    item["eventScore"] = decision.finalScore;
    item["stftEntropy200ms"] = cachedStftEntropy;
    item["eventType"] = acousticEventType;
    item["acousticEventType"] = acousticEventType;
    item["isRecognized"] = decision.accepted;
    return item;
}

QVariantList toSegmentList(
    const QVector<Segment>& segments,
    int sampleRate,
    const QVector<double>& rawSignal,
    const QVector<double>& steValues,
    const QVector<double>& zcrValues,
    const QVector<double>& fdValues,
    const QVector<double>& logSteValues,
    const QVector<double>& envPeakValues,
    const QVector<double>& envSlopeValues,
    const QVector<double>& kurtosisValues,
    const QVector<double>& subbandLowValues,
    const QVector<double>& subbandMidValues,
    const QVector<double>& subbandHighValues,
    const QVector<double>& dominantFreqValues,
    const QVector<double>& spectralCentroidValues,
    const QVector<double>& spectralBandwidthValues,
    const QVector<double>& spectralRolloffValues,
    const QVector<double>& spectralEntropyValues,
    const QVector<double>& multiscaleEnergy1Values,
    const QVector<double>& multiscaleEnergy2Values,
    const QVector<double>& multiscaleEnergy3Values,
    const QVector<double>& waveletEntropyValues,
    const QVector<double>& transientStrengthValues,
    const QVector<double>& logMel1Values,
    const QVector<double>& logMel2Values,
    const QVector<double>& logMel3Values,
    const QVector<double>& candidateScoreValues,
    const QVector<bool>& candidateMask,
    double logEnergyBackgroundMean,
    double logEnergyThreshold,
    double entropyThreshold,
    int frameLengthSamples,
    int frameShiftSamples)
{
    QVariantList list;
    if (sampleRate <= 0) {
        return list;
    }

    list.reserve(segments.size());
    for (int eventIndex = 0; eventIndex < segments.size(); ++eventIndex) {
        list.append(
            buildRecognizedEvent(
                segments[eventIndex],
                eventIndex,
                sampleRate,
                rawSignal,
                steValues,
                zcrValues,
                fdValues,
                logSteValues,
                envPeakValues,
                envSlopeValues,
                kurtosisValues,
                subbandLowValues,
                subbandMidValues,
                subbandHighValues,
                dominantFreqValues,
                spectralCentroidValues,
                spectralBandwidthValues,
                spectralRolloffValues,
                spectralEntropyValues,
                multiscaleEnergy1Values,
                multiscaleEnergy2Values,
                multiscaleEnergy3Values,
                waveletEntropyValues,
                transientStrengthValues,
                logMel1Values,
                logMel2Values,
                logMel3Values,
                candidateScoreValues,
                candidateMask,
                logEnergyBackgroundMean,
                logEnergyThreshold,
                entropyThreshold,
                frameLengthSamples,
                frameShiftSamples));
    }

    return list;
}

double totalDurationMs(const QVariantList& events)
{
    double total = 0.0;
    for (const QVariant& eventValue : events) {
        total += eventValue.toMap().value(QStringLiteral("durationMs")).toDouble();
    }
    return total;
}

QVariantMap buildDefaultFeatureValues()
{
    QVariantMap map;
    map["sampleRate"] = 0;
    map["sampleCount"] = 0;
    map["frameLengthMs"] = 0.0;
    map["frameShiftMs"] = 0.0;
    map["maxSilenceMs"] = 0.0;
    map["thresholdT"] = 0.0;
    map["steThreshold"] = 0.0;
    map["zcrThreshold"] = 0.0;
    map["fdThreshold"] = 0.0;
    map["logEnergyThreshold"] = 0.0;
    map["envelopePeakThreshold"] = 0.0;
    map["transientThreshold"] = 0.0;
    map["spectralEntropyUpperBound"] = 0.0;
    map["steValues"] = QVariantList {};
    map["zcrValues"] = QVariantList {};
    map["fdValues"] = QVariantList {};
    map["logSteValues"] = QVariantList {};
    map["envelopePeakValues"] = QVariantList {};
    map["envelopeSlopeValues"] = QVariantList {};
    map["kurtosisValues"] = QVariantList {};
    map["subbandLowValues"] = QVariantList {};
    map["subbandMidValues"] = QVariantList {};
    map["subbandHighValues"] = QVariantList {};
    map["dominantFreqValues"] = QVariantList {};
    map["spectralCentroidValues"] = QVariantList {};
    map["spectralBandwidthValues"] = QVariantList {};
    map["spectralRolloffValues"] = QVariantList {};
    map["spectralEntropyValues"] = QVariantList {};
    map["multiscaleEnergy1Values"] = QVariantList {};
    map["multiscaleEnergy2Values"] = QVariantList {};
    map["multiscaleEnergy3Values"] = QVariantList {};
    map["waveletEntropyValues"] = QVariantList {};
    map["transientStrengthValues"] = QVariantList {};
    map["logMel1Values"] = QVariantList {};
    map["logMel2Values"] = QVariantList {};
    map["logMel3Values"] = QVariantList {};
    map["candidateScoreValues"] = QVariantList {};
    map["candidateFrameMask"] = QVariantList {};
    map["candidateSegments"] = QVariantList {};
    map["candidateEventCount"] = 0;
    map["recognizedSegments"] = QVariantList {};
    map["recognizedEventCount"] = 0;
    map["bowelSoundOccurrenceCount"] = 0;
    map["recognizedTotalDurationMs"] = 0.0;
    map["hasBowelSound"] = false;
    return map;
}

double percentileValue(const QVector<double>& values, double percentile)
{
    if (values.isEmpty()) {
        return 0.0;
    }

    QVector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    const double clampedPercentile = std::clamp(percentile, 0.0, 1.0);
    const qsizetype lastIndex = sorted.size() - 1;
    const double index =
        clampedPercentile * static_cast<double>(lastIndex);
    const qsizetype lowerIndex = static_cast<qsizetype>(std::floor(index));
    const qsizetype upperIndex = static_cast<qsizetype>(std::ceil(index));
    if (lowerIndex == upperIndex) {
        return sorted[lowerIndex];
    }

    const double weight = index - static_cast<double>(lowerIndex);
    return sorted[lowerIndex] * (1.0 - weight) + sorted[upperIndex] * weight;
}

double adaptiveThreshold(
    const QVector<double>& values,
    double lowPercentile,
    double highPercentile,
    double blend)
{
    if (values.isEmpty()) {
        return 0.0;
    }

    const double low = percentileValue(values, lowPercentile);
    const double high = percentileValue(values, highPercentile);
    if (high <= low) {
        return std::max(0.0, low);
    }

    const double threshold = low + (high - low) * std::clamp(blend, 0.0, 1.0);
    return std::max(0.0, threshold);
}

QVariantList boolVectorToVariantList(const QVector<bool>& values)
{
    QVariantList list;
    list.reserve(values.size());
    for (bool value : values) {
        list.append(value);
    }
    return list;
}

QVariantList segmentPreviewList(const QVector<Segment>& segments, int sampleRate)
{
    QVariantList list;
    list.reserve(segments.size());
    for (int index = 0; index < segments.size(); ++index) {
        QVariantMap item;
        const int startSample = std::max(0, segments[index].first);
        const int endSample = std::max(startSample, segments[index].second);
        item["candidateIndex"] = index + 1;
        item["startSample"] = startSample;
        item["endSample"] = endSample;
        item["startSeconds"] =
            sampleRate > 0
                ? static_cast<double>(startSample) / static_cast<double>(sampleRate)
                : 0.0;
        item["endSeconds"] =
            sampleRate > 0
                ? static_cast<double>(endSample) / static_cast<double>(sampleRate)
                : 0.0;
        item["durationMs"] =
            sampleRate > 0
                ? static_cast<double>(endSample - startSample) * 1000.0 /
                    static_cast<double>(sampleRate)
                : 0.0;
        list.append(item);
    }
    return list;
}
} // namespace

Multi_featureJointDetection* Multi_featureJointDetection::m_instance = nullptr;

FeatureDetectionAlgorithm::FeatureDetectionAlgorithm(
    int sampleRate,
    double frameLengthMs,
    double frameShiftMs,
    double maxSilenceMs,
    double thresholdT)
    : m_sampleRate(sampleRate)
    , m_frameLengthMs(frameLengthMs)
    , m_frameShiftMs(frameShiftMs)
    , m_maxSilenceMs(maxSilenceMs)
    , m_thresholdT(thresholdT)
{
    m_frameLengthSamples = std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<double>(m_sampleRate) * m_frameLengthMs / 1000.0)));
    m_frameShiftSamples = std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<double>(m_sampleRate) * m_frameShiftMs / 1000.0)));
    m_maxSilenceFrames = std::max(
        1,
        static_cast<int>(std::lround(m_maxSilenceMs / m_frameShiftMs)));
}

void FeatureDetectionAlgorithm::setThresholds(
    double steTh,
    double zcrTh,
    double fdTh)
{
    m_steThresh = steTh;
    m_zcrThresh = zcrTh;
    m_fdThresh = fdTh;
    m_logEnergyThresh = std::log1p(std::max(0.0, steTh));
    m_envPeakThresh = std::sqrt(std::max(0.0, steTh));
    m_transientThresh = std::sqrt(std::max(0.0, steTh));
    m_entropyUpperBound = std::max(0.35, 0.9 - 0.05 * fdTh);
    m_thresholdsSet = true;
}

QVector<QPair<int, int>> FeatureDetectionAlgorithm::detect(
    const QVector<double>& rawSignal)
{
    const QVector<QVector<double>> frames = frameSignal(rawSignal);
    QVector<double> steVec;
    QVector<double> fdVec;
    QVector<double> zcrVec;
    extractFeatures(frames, steVec, fdVec, zcrVec);

    if (!m_thresholdsSet) {
        estimateAdaptiveThresholds(steVec, zcrVec, fdVec);
    }

    return detectByThreeStage(steVec, zcrVec, fdVec, m_frameShiftSamples);
}

QVariantMap FeatureDetectionAlgorithm::analyze(const QVector<double>& rawSignal)
{
    QVariantMap featureValues = buildDefaultFeatureValues();
    featureValues["sampleRate"] = m_sampleRate;
    featureValues["sampleCount"] = rawSignal.size();
    featureValues["frameLengthMs"] = m_frameLengthMs;
    featureValues["frameShiftMs"] = m_frameShiftMs;
    featureValues["maxSilenceMs"] = m_maxSilenceMs;
    featureValues["thresholdT"] = m_thresholdT;

    const QVector<QVector<double>> frames = frameSignal(rawSignal);
    QVector<double> steVec;
    QVector<double> fdVec;
    QVector<double> zcrVec;
    extractFeatures(frames, steVec, fdVec, zcrVec);

    if (!m_thresholdsSet) {
        estimateAdaptiveThresholds(steVec, zcrVec, fdVec);
    }

    const QVector<Segment> candidateSegments =
        detectByThreeStage(steVec, zcrVec, fdVec, m_frameShiftSamples);

    featureValues["steThreshold"] = m_steThresh;
    featureValues["zcrThreshold"] = m_zcrThresh;
    featureValues["fdThreshold"] = m_fdThresh;
    featureValues["logEnergyThreshold"] = m_logEnergyThresh;
    featureValues["envelopePeakThreshold"] = m_envPeakThresh;
    featureValues["transientThreshold"] = m_transientThresh;
    featureValues["spectralEntropyUpperBound"] = m_entropyUpperBound;
    featureValues["steValues"] = toVariantList(steVec);
    featureValues["zcrValues"] = toVariantList(zcrVec);
    featureValues["fdValues"] = toVariantList(fdVec);
    featureValues["logSteValues"] = toVariantList(m_logSteVec);
    featureValues["envelopePeakValues"] = toVariantList(m_envPeakVec);
    featureValues["envelopeSlopeValues"] = toVariantList(m_envSlopeVec);
    featureValues["kurtosisValues"] = toVariantList(m_kurtosisVec);
    featureValues["subbandLowValues"] = toVariantList(m_subbandLowVec);
    featureValues["subbandMidValues"] = toVariantList(m_subbandMidVec);
    featureValues["subbandHighValues"] = toVariantList(m_subbandHighVec);
    featureValues["dominantFreqValues"] = toVariantList(m_dominantFreqVec);
    featureValues["spectralCentroidValues"] = toVariantList(m_spectralCentroidVec);
    featureValues["spectralBandwidthValues"] = toVariantList(m_spectralBandwidthVec);
    featureValues["spectralRolloffValues"] = toVariantList(m_spectralRolloffVec);
    featureValues["spectralEntropyValues"] = toVariantList(m_spectralEntropyVec);
    featureValues["multiscaleEnergy1Values"] = toVariantList(m_multiscaleEnergy1Vec);
    featureValues["multiscaleEnergy2Values"] = toVariantList(m_multiscaleEnergy2Vec);
    featureValues["multiscaleEnergy3Values"] = toVariantList(m_multiscaleEnergy3Vec);
    featureValues["waveletEntropyValues"] = toVariantList(m_waveletEntropyVec);
    featureValues["transientStrengthValues"] = toVariantList(m_transientStrengthVec);
    featureValues["logMel1Values"] = toVariantList(m_logMel1Vec);
    featureValues["logMel2Values"] = toVariantList(m_logMel2Vec);
    featureValues["logMel3Values"] = toVariantList(m_logMel3Vec);
    featureValues["candidateScoreValues"] = toVariantList(m_candidateScoreVec);
    featureValues["candidateFrameMask"] = boolVectorToVariantList(m_candidateMask);

    const double logEnergyBackgroundMean =
        m_logSteVec.isEmpty() ? 0.0 : percentileValue(m_logSteVec, 0.20);

    const QVariantList candidateEvents =
        toSegmentList(
            candidateSegments,
            m_sampleRate,
            rawSignal,
            steVec,
            zcrVec,
            fdVec,
            m_logSteVec,
            m_envPeakVec,
            m_envSlopeVec,
            m_kurtosisVec,
            m_subbandLowVec,
            m_subbandMidVec,
            m_subbandHighVec,
            m_dominantFreqVec,
            m_spectralCentroidVec,
            m_spectralBandwidthVec,
            m_spectralRolloffVec,
            m_spectralEntropyVec,
            m_multiscaleEnergy1Vec,
            m_multiscaleEnergy2Vec,
            m_multiscaleEnergy3Vec,
            m_waveletEntropyVec,
            m_transientStrengthVec,
            m_logMel1Vec,
            m_logMel2Vec,
            m_logMel3Vec,
            m_candidateScoreVec,
            m_candidateMask,
            logEnergyBackgroundMean,
            m_logEnergyThresh,
            m_entropyUpperBound,
            m_frameLengthSamples,
            m_frameShiftSamples);

    QVariantList recognizedEvents;
    QVector<Segment> recognizedSegments;
    for (int index = 0; index < candidateEvents.size(); ++index) {
        const QVariantMap event = candidateEvents[index].toMap();
        if (!event.value(QStringLiteral("isRecognized")).toBool()) {
            continue;
        }

        recognizedEvents.append(event);
        recognizedSegments.append(qMakePair(
            event.value(QStringLiteral("startSample")).toInt(),
            event.value(QStringLiteral("endSample")).toInt()));
    }

    featureValues["candidateSegments"] = segmentPreviewList(candidateSegments, m_sampleRate);
    featureValues["candidateEventCount"] = candidateSegments.size();
    featureValues["recognizedSegments"] = recognizedEvents;
    featureValues["recognizedEventCount"] = recognizedEvents.size();
    featureValues["bowelSoundOccurrenceCount"] = recognizedEvents.size();
    featureValues["recognizedTotalDurationMs"] = totalDurationMs(recognizedEvents);
    featureValues["hasBowelSound"] = !recognizedSegments.isEmpty();
    return featureValues;
}

// Use the original rectangular-window framing path.
QVector<QVector<double>> FeatureDetectionAlgorithm::frameSignal(
    const QVector<double>& signal)
{
    QVector<QVector<double>> frames;
    const int totalSamples = signal.size();
    int start = 0;
    while (start + m_frameLengthSamples <= totalSamples) {
        QVector<double> frame(m_frameLengthSamples);
        std::copy(
            signal.begin() + start,
            signal.begin() + start + m_frameLengthSamples,
            frame.begin());
        frames.append(frame);
        start += m_frameShiftSamples;
    }
    return frames;
}

void FeatureDetectionAlgorithm::extractFeatures(
    const QVector<QVector<double>>& frames,
    QVector<double>& steVec,
    QVector<double>& fdVec,
    QVector<double>& zcrVec)
{
    steVec.clear();
    fdVec.clear();
    zcrVec.clear();
    m_logSteVec.clear();
    m_envPeakVec.clear();
    m_envSlopeVec.clear();
    m_kurtosisVec.clear();
    m_subbandLowVec.clear();
    m_subbandMidVec.clear();
    m_subbandHighVec.clear();
    m_dominantFreqVec.clear();
    m_spectralCentroidVec.clear();
    m_spectralBandwidthVec.clear();
    m_spectralRolloffVec.clear();
    m_spectralEntropyVec.clear();
    m_multiscaleEnergy1Vec.clear();
    m_multiscaleEnergy2Vec.clear();
    m_multiscaleEnergy3Vec.clear();
    m_waveletEntropyVec.clear();
    m_transientStrengthVec.clear();
    m_logMel1Vec.clear();
    m_logMel2Vec.clear();
    m_logMel3Vec.clear();
    m_candidateScoreVec.clear();
    m_frameRmsVec.clear();
    m_frameDurationMsVec.clear();
    m_candidateMask.clear();

    steVec.reserve(frames.size());
    fdVec.reserve(frames.size());
    zcrVec.reserve(frames.size());
    m_logSteVec.reserve(frames.size());
    m_envPeakVec.reserve(frames.size());
    m_envSlopeVec.reserve(frames.size());
    m_kurtosisVec.reserve(frames.size());
    m_subbandLowVec.reserve(frames.size());
    m_subbandMidVec.reserve(frames.size());
    m_subbandHighVec.reserve(frames.size());
    m_dominantFreqVec.reserve(frames.size());
    m_spectralCentroidVec.reserve(frames.size());
    m_spectralBandwidthVec.reserve(frames.size());
    m_spectralRolloffVec.reserve(frames.size());
    m_spectralEntropyVec.reserve(frames.size());
    m_multiscaleEnergy1Vec.reserve(frames.size());
    m_multiscaleEnergy2Vec.reserve(frames.size());
    m_multiscaleEnergy3Vec.reserve(frames.size());
    m_waveletEntropyVec.reserve(frames.size());
    m_transientStrengthVec.reserve(frames.size());
    m_logMel1Vec.reserve(frames.size());
    m_logMel2Vec.reserve(frames.size());
    m_logMel3Vec.reserve(frames.size());
    m_candidateScoreVec.reserve(frames.size());
    m_frameRmsVec.reserve(frames.size());
    m_frameDurationMsVec.reserve(frames.size());

    double previousEnvelopePeak = 0.0;

    for (const QVector<double>& frame : frames) {
        const double ste = computeSTE(frame);
        const double fd = computeFD(frame);
        const double zcr = computeZCR(frame, m_thresholdT);

        const int sampleCount = frame.size();
        QVector<float> frameFloat(sampleCount, 0.0f);
        for (int index = 0; index < sampleCount; ++index) {
            frameFloat[index] = static_cast<float>(frame[index]);
        }

        QVector<float> magnitudes;
        int fftSize = sampleCount;
        try {
            const KfrDftUtils::RealDftSpectrumResult spectrum =
                KfrDftUtils::computeRealSpectrumMagnitudes(frameFloat);
            magnitudes = spectrum.magnitudes;
            fftSize = std::max(1, spectrum.fftSize);
        } catch (const std::exception& exception) {
            qWarning() << "Multi_featureJointDetection: FFT frame analysis failed" << exception.what();
            magnitudes.fill(0.0f, std::max(2, sampleCount / 2 + 1));
        }

        double spectrumSum = 0.0;
        double weightedFrequencySum = 0.0;
        double weightedFrequencySquareSum = 0.0;
        double maxMagnitude = 0.0;
        int maxMagnitudeIndex = 0;
        double cumulativeMagnitude = 0.0;
        const double frequencyResolution =
            fftSize > 0 ? static_cast<double>(m_sampleRate) / static_cast<double>(fftSize) : 0.0;

        QVector<double> spectralForEntropy;
        spectralForEntropy.reserve(magnitudes.size());
        for (int binIndex = 0; binIndex < magnitudes.size(); ++binIndex) {
            const double magnitude = std::max(0.0, static_cast<double>(magnitudes[binIndex]));
            const double frequency = static_cast<double>(binIndex) * frequencyResolution;
            spectralForEntropy.append(magnitude);
            spectrumSum += magnitude;
            weightedFrequencySum += magnitude * frequency;
            weightedFrequencySquareSum += magnitude * frequency * frequency;
            if (magnitude > maxMagnitude) {
                maxMagnitude = magnitude;
                maxMagnitudeIndex = binIndex;
            }
        }

        const double spectralCentroid =
            spectrumSum > kFeatureEpsilon ? weightedFrequencySum / spectrumSum : 0.0;
        const double spectralBandwidth =
            spectrumSum > kFeatureEpsilon
                ? std::sqrt(std::max(
                      0.0,
                      weightedFrequencySquareSum / spectrumSum -
                          spectralCentroid * spectralCentroid))
                : 0.0;

        double rolloffFrequency = 0.0;
        const double rolloffTarget = spectrumSum * 0.85;
        for (int binIndex = 0; binIndex < magnitudes.size(); ++binIndex) {
            cumulativeMagnitude += std::max(0.0, static_cast<double>(magnitudes[binIndex]));
            if (cumulativeMagnitude >= rolloffTarget) {
                rolloffFrequency = static_cast<double>(binIndex) * frequencyResolution;
                break;
            }
        }

        // Use explicit bowel-sound frequency windows instead of splitting FFT bins
        // into three equal parts, which makes low/mid/high energy more interpretable.
        const FrequencySubbandEnergies subbandEnergies =
            computeFrequencySubbandEnergies(magnitudes, frequencyResolution);
        const double subbandLow = subbandEnergies.low;
        const double subbandMid = subbandEnergies.mid;
        const double subbandHigh = subbandEnergies.high;

        double frameMean = 0.0;
        for (double sample : frame) {
            frameMean += sample;
        }
        frameMean /= static_cast<double>(std::max(1, sampleCount));

        double secondMoment = 0.0;
        double fourthMoment = 0.0;
        double envelopePeak = 0.0;
        double transientStrength = 0.0;
        double rmsAccumulator = 0.0;
        for (int index = 0; index < sampleCount; ++index) {
            const double sample = frame[index];
            const double absoluteSample = std::abs(sample);
            envelopePeak = std::max(envelopePeak, absoluteSample);
            rmsAccumulator += sample * sample;
            const double centered = sample - frameMean;
            secondMoment += centered * centered;
            fourthMoment += centered * centered * centered * centered;
            if (index > 0) {
                transientStrength += std::abs(frame[index] - frame[index - 1]);
            }
        }

        const double rms = std::sqrt(rmsAccumulator / static_cast<double>(std::max(1, sampleCount)));
        transientStrength /= static_cast<double>(std::max(1, sampleCount - 1));
        const double kurtosis =
            secondMoment > kFeatureEpsilon
                ? (fourthMoment / static_cast<double>(sampleCount)) /
                    std::pow(secondMoment / static_cast<double>(sampleCount), 2.0)
                : 0.0;

        const double envelopeSlope = envelopePeak - previousEnvelopePeak;
        previousEnvelopePeak = envelopePeak;

        // Translation-invariant sym6 SWT decomposition: 3 levels (no decimation).
        // scaleEnergies still tracks fine/mid/coarse detail activity.
        const WaveletTransform::DecompositionResult wavDecomp =
            WaveletTransform::decompose(frame, 3);

        QVector<double> scaleEnergies(3, 0.0);
        for (int level = 0; level < std::min(3, static_cast<int>(wavDecomp.details.size())); ++level) {
            const QVector<double>& detail = wavDecomp.details[level];
            if (detail.isEmpty()) {
                continue;
            }
            double levelEnergy = 0.0;
            for (double coefficient : detail) {
                levelEnergy += coefficient * coefficient;
            }
            scaleEnergies[level] = levelEnergy / static_cast<double>(detail.size());
        }

        // SWT is redundant; compensate each level by its redundancy factor 2^j
        // to keep inter-scale energy contributions balanced.
        QVector<double> waveletEnergyDistribution;
        waveletEnergyDistribution.reserve(wavDecomp.details.size() + 1);
        for (int level = 0; level < wavDecomp.details.size(); ++level) {
            const QVector<double>& detail = wavDecomp.details[level];
            double levelEnergy = 0.0;
            for (double coefficient : detail) {
                levelEnergy += coefficient * coefficient;
            }

            const int compensatedLevel = std::min(level + 1, 20);
            const double redundancyCompensation =
                static_cast<double>(1 << compensatedLevel);
            waveletEnergyDistribution.append(levelEnergy / redundancyCompensation);
        }
        {
            double approxEnergy = 0.0;
            for (double coefficient : wavDecomp.approximation) {
                approxEnergy += coefficient * coefficient;
            }

            const int approxLevel = std::min(static_cast<int>(wavDecomp.details.size()), 20);
            const double approxRedundancyCompensation =
                static_cast<double>(1 << approxLevel);
            waveletEnergyDistribution.append(approxEnergy / approxRedundancyCompensation);
        }

        // Log-Mel statistics: split 26 Mel filters into 3 contiguous regions.
        const QVector<double> logMelStats =
            computeLogMelStatistics(magnitudes, fftSize, m_sampleRate, 26, 3);
        const double logMel1 = logMelStats.size() > 0 ? logMelStats[0] : 0.0;
        const double logMel2 = logMelStats.size() > 1 ? logMelStats[1] : 0.0;
        const double logMel3 = logMelStats.size() > 2 ? logMelStats[2] : 0.0;

        steVec.append(ste);
        fdVec.append(fd);
        zcrVec.append(zcr);
        m_logSteVec.append(std::log1p(std::max(0.0, ste)));
        m_envPeakVec.append(envelopePeak);
        m_envSlopeVec.append(envelopeSlope);
        m_kurtosisVec.append(kurtosis);
        m_subbandLowVec.append(subbandLow);
        m_subbandMidVec.append(subbandMid);
        m_subbandHighVec.append(subbandHigh);
        m_dominantFreqVec.append(static_cast<double>(maxMagnitudeIndex) * frequencyResolution);
        m_spectralCentroidVec.append(spectralCentroid);
        m_spectralBandwidthVec.append(spectralBandwidth);
        m_spectralRolloffVec.append(rolloffFrequency);
        m_spectralEntropyVec.append(normalizedEntropy(spectralForEntropy));
        m_multiscaleEnergy1Vec.append(scaleEnergies[0]);
        m_multiscaleEnergy2Vec.append(scaleEnergies[1]);
        m_multiscaleEnergy3Vec.append(scaleEnergies[2]);
        m_waveletEntropyVec.append(normalizedEntropy(waveletEnergyDistribution));
        m_transientStrengthVec.append(transientStrength);
        m_logMel1Vec.append(logMel1);
        m_logMel2Vec.append(logMel2);
        m_logMel3Vec.append(logMel3);
        m_candidateScoreVec.append(0.0);
        m_frameRmsVec.append(rms);
        m_frameDurationMsVec.append(
            static_cast<double>(sampleCount) * 1000.0 /
            static_cast<double>(std::max(1, m_sampleRate)));
        m_candidateMask.append(false);
    }
}

double FeatureDetectionAlgorithm::computeSTE(const QVector<double>& frame)
{
    double energy = 0.0;
    for (double sample : frame) {
        energy += sample * sample;
    }
    return energy;
}

double FeatureDetectionAlgorithm::computeFD(const QVector<double>& frame)
{
    return FractalDimensionUtils::computeWaveformComplexity(frame);
}

double FeatureDetectionAlgorithm::computeZCR(
    const QVector<double>& frame,
    double thresholdT)
{
    double zcr = 0.0;
    for (int i = 1; i < frame.size(); ++i) {
        const bool crossUpper =
            (frame[i - 1] - thresholdT) * (frame[i] - thresholdT) < 0.0;
        const bool crossLower =
            (frame[i - 1] + thresholdT) * (frame[i] + thresholdT) < 0.0;
        if (crossUpper) {
            zcr += 0.5;
        }
        if (crossLower) {
            zcr += 0.5;
        }
    }
    return zcr;
}

void FeatureDetectionAlgorithm::estimateAdaptiveThresholds(
    const QVector<double>& ste,
    const QVector<double>& zcr,
    const QVector<double>& fd)
{
    m_steThresh = adaptiveThreshold(ste, 0.20, 0.85, 0.35);
    m_zcrThresh = adaptiveThreshold(zcr, 0.25, 0.85, 0.40);
    m_fdThresh = adaptiveThreshold(fd, 0.25, 0.80, 0.35);

    QVector<double> backgroundLogEnergy;
    QVector<double> backgroundEnvelopePeak;
    QVector<double> backgroundTransient;
    QVector<double> backgroundEntropy;
    const double logEnergyFloor = percentileValue(m_logSteVec, 0.30);
    for (int index = 0; index < m_logSteVec.size(); ++index) {
        if (m_logSteVec[index] <= logEnergyFloor) {
            backgroundLogEnergy.append(m_logSteVec[index]);
            if (index < m_envPeakVec.size()) {
                backgroundEnvelopePeak.append(m_envPeakVec[index]);
            }
            if (index < m_transientStrengthVec.size()) {
                backgroundTransient.append(m_transientStrengthVec[index]);
            }
            if (index < m_spectralEntropyVec.size()) {
                backgroundEntropy.append(m_spectralEntropyVec[index]);
            }
        }
    }

    const double logMean = safeMean(backgroundLogEnergy);
    const double logStd = safeStd(backgroundLogEnergy, logMean);
    const double envMean = safeMean(backgroundEnvelopePeak);
    const double envStd = safeStd(backgroundEnvelopePeak, envMean);
    const double transientMean = safeMean(backgroundTransient);
    const double transientStd = safeStd(backgroundTransient, transientMean);
    const double entropyMean = safeMean(backgroundEntropy);
    const double entropyStd = safeStd(backgroundEntropy, entropyMean);

    m_logEnergyThresh = std::max(logMean + 2.4 * logStd, percentileValue(m_logSteVec, 0.62));
    m_envPeakThresh = std::max(envMean + 1.8 * envStd, percentileValue(m_envPeakVec, 0.60));
    m_transientThresh =
        std::max(transientMean + 1.6 * transientStd, percentileValue(m_transientStrengthVec, 0.60));
    m_entropyUpperBound = std::min(
        1.2,
        std::max(0.35, entropyMean + 0.8 * entropyStd));

    m_thresholdsSet = true;

    qInfo() << "Multi_featureJointDetection: adaptive thresholds applied"
            << "STE:" << m_steThresh
            << "ZCR:" << m_zcrThresh
            << "FD:" << m_fdThresh
            << "logE:" << m_logEnergyThresh
            << "envPeak:" << m_envPeakThresh
            << "transient:" << m_transientThresh
            << "entropyUpper:" << m_entropyUpperBound;
}

QVector<QPair<int, int>> FeatureDetectionAlgorithm::detectByThreeStage(
    const QVector<double>& ste,
    const QVector<double>& zcr,
    const QVector<double>& fd,
    int frameShiftSamples)
{
    QVector<QPair<int, int>> segments;
    const int numFrames = std::min({
        ste.size(),
        zcr.size(),
        fd.size(),
        m_logSteVec.size(),
        m_envPeakVec.size(),
        m_transientStrengthVec.size(),
        m_spectralEntropyVec.size(),
        m_kurtosisVec.size(),
        m_subbandLowVec.size(),
        m_subbandMidVec.size(),
        m_subbandHighVec.size(),
        m_multiscaleEnergy1Vec.size(),
        m_multiscaleEnergy2Vec.size(),
        m_multiscaleEnergy3Vec.size(),
        m_waveletEntropyVec.size(),
        m_logMel1Vec.size(),
        m_logMel2Vec.size(),
        m_logMel3Vec.size(),
        m_candidateMask.size(),
        m_candidateScoreVec.size()
    });
    if (numFrames == 0) {
        return segments;
    }

    std::fill(m_candidateMask.begin(), m_candidateMask.end(), false);
    std::fill(m_candidateScoreVec.begin(), m_candidateScoreVec.end(), 0.0);

    const double kurtosisThreshold = adaptiveThreshold(m_kurtosisVec, 0.45, 0.85, 0.35);
    const double waveletEntropyUpper = adaptiveThreshold(m_waveletEntropyVec, 0.15, 0.65, 0.55);

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex) {
        const double logEnergy = m_logSteVec[frameIndex];
        const double envPeak = m_envPeakVec[frameIndex];
        const double transient = m_transientStrengthVec[frameIndex];
        const double entropy = m_spectralEntropyVec[frameIndex];
        const double kurtosis = m_kurtosisVec[frameIndex];
        const double waveletEntropy = m_waveletEntropyVec[frameIndex];
        const double logMelFeature =
            0.20 * std::abs(m_logMel1Vec[frameIndex]) +
            0.15 * std::abs(m_logMel2Vec[frameIndex]) +
            0.10 * std::abs(m_logMel3Vec[frameIndex]);
        const double midHighRatio = safeRatio(
            m_subbandMidVec[frameIndex],
            m_subbandHighVec[frameIndex]);
        const double lowMidRatio = safeRatio(
            m_subbandLowVec[frameIndex],
            m_subbandMidVec[frameIndex]);
        const double multiscaleMidRatio = safeRatio(
            m_multiscaleEnergy2Vec[frameIndex],
            m_multiscaleEnergy1Vec[frameIndex] + m_multiscaleEnergy3Vec[frameIndex]);
        const double fdWeak = fd[frameIndex];

        const double energyScore = std::max(0.0, logEnergy - m_logEnergyThresh);
        const double envScore = std::max(0.0, envPeak - m_envPeakThresh);
        const double transientScore = std::max(0.0, transient - m_transientThresh);
        const double entropyScore = std::max(0.0, m_entropyUpperBound - entropy);
        const double kurtosisScore = std::max(0.0, kurtosis - kurtosisThreshold);
        const double waveletScore = std::max(0.0, waveletEntropyUpper - waveletEntropy);
        const double subbandScore = std::max(0.0, midHighRatio - 0.35) +
            0.5 * std::max(0.0, lowMidRatio - 0.20);
        const double multiscaleScore = std::max(0.0, multiscaleMidRatio - 0.75);
        const double zcrScore = std::max(0.0, zcr[frameIndex] - m_zcrThresh * 0.6);
        const double fdScore = std::max(0.0, fdWeak - m_fdThresh * 0.8);

        const double candidateScore =
            0.30 * energyScore +
            0.18 * envScore +
            0.16 * transientScore +
            0.11 * entropyScore +
            0.08 * kurtosisScore +
            0.07 * waveletScore +
            0.05 * subbandScore +
            0.04 * multiscaleScore +
            0.03 * zcrScore +
            0.01 * fdScore +
            0.01 * std::clamp(logMelFeature, 0.0, 3.0);

        const int voteCount =
            (logEnergy > m_logEnergyThresh ? 1 : 0) +
            (envPeak > m_envPeakThresh ? 1 : 0) +
            (transient > m_transientThresh ? 1 : 0) +
            (entropy < m_entropyUpperBound ? 1 : 0) +
            (kurtosis > kurtosisThreshold ? 1 : 0) +
            (waveletEntropy < waveletEntropyUpper ? 1 : 0) +
            (midHighRatio > 0.35 ? 1 : 0) +
            (multiscaleMidRatio > 0.75 ? 1 : 0) +
            (zcr[frameIndex] > m_zcrThresh * 0.6 ? 1 : 0);

        const bool isCandidate =
            (logEnergy > m_logEnergyThresh) &&
            (voteCount >= 4 || candidateScore > 0.85);
        m_candidateMask[frameIndex] = isCandidate;
        m_candidateScoreVec[frameIndex] = candidateScore;
    }

    enum State { IDLE, IN_SEGMENT };
    State state = IDLE;
    int segmentStartFrame = -1;
    int silentFrameCount = 0;

    for (int i = 0; i < numFrames; ++i) {
        const bool candidateActive = m_candidateMask[i];

        switch (state) {
        case IDLE:
            if (candidateActive) {
                segmentStartFrame = i;
                silentFrameCount = 0;
                state = IN_SEGMENT;
            }
            break;

        case IN_SEGMENT:
            if (!candidateActive) {
                ++silentFrameCount;
                if (silentFrameCount >= m_maxSilenceFrames) {
                    const int startSample =
                        segmentStartFrame * frameShiftSamples;
                    const int endSample =
                        (i - silentFrameCount + 1) * frameShiftSamples +
                        m_frameLengthSamples;
                    const double durationMs =
                        static_cast<double>(endSample - startSample) * 1000.0 /
                        static_cast<double>(std::max(1, m_sampleRate));
                    if (endSample > startSample && durationMs >= 20.0) {
                        segments.append(qMakePair(startSample, endSample));
                    }
                    state = IDLE;
                }
            } else {
                silentFrameCount = 0;
            }
            break;
        }
    }

    if (state == IN_SEGMENT) {
        const int startSample = segmentStartFrame * frameShiftSamples;
        const int endSample = numFrames * frameShiftSamples + m_frameLengthSamples;
        const double durationMs =
            static_cast<double>(endSample - startSample) * 1000.0 /
            static_cast<double>(std::max(1, m_sampleRate));
        if (endSample > startSample && durationMs >= 20.0) {
            segments.append(qMakePair(startSample, endSample));
        }
    }

    return segments;
}

MultiFeatureJointDetectionWorker::MultiFeatureJointDetectionWorker(
    bool thresholdsConfigured,
    double steThreshold,
    double zcrThreshold,
    double fdThreshold,
    QObject* parent)
    : QObject(parent)
    , m_thresholdsConfigured(thresholdsConfigured)
    , m_steThreshold(steThreshold)
    , m_zcrThreshold(zcrThreshold)
    , m_fdThreshold(fdThreshold)
{
}

MultiFeatureJointDetectionWorker::~MultiFeatureJointDetectionWorker() = default;

void MultiFeatureJointDetectionWorker::analyzeImportedTemporaryFile(
    const QString& temporaryFilePath)
{
    // This worker is single-shot: read temp data, extract features, store, exit.
    const ImportedSignalData signalData = readImportedSignal(temporaryFilePath);
    if (signalData.sampleRate <= 0 || signalData.samples.isEmpty()) {
        emit analysisFailed(
            "Multi_featureJointDetection: imported temporary file is not ready");
        emit finished();
        return;
    }

    QVector<double> rawSignal(signalData.samples.size(), 0.0);
    std::transform(
        signalData.samples.cbegin(),
        signalData.samples.cend(),
        rawSignal.begin(),
        [](float value) { return static_cast<double>(value); });

    FeatureDetectionAlgorithm detector(signalData.sampleRate);
    if (m_thresholdsConfigured) {
        detector.setThresholds(m_steThreshold, m_zcrThreshold, m_fdThreshold);
    }

    QElapsedTimer analysisTimer;
    analysisTimer.start();
    const QVariantMap featureValues = detector.analyze(rawSignal);
    DataManager::instance()->updateImportedAnalysisSummary({
        {QStringLiteral("timings"), QVariantMap{
             {QStringLiteral("multiFeatureAnalysisMs"), elapsedMilliseconds(analysisTimer)}
         }}
    });
    DataManager::instance()->storeImportedFeatureValues(featureValues);
    QString persistErrorMessage;
    if (!GenerateManager::persistImportedAnalysisTemporaryJson(
            featureValues,
            &persistErrorMessage)) {
        qWarning() << "Multi_featureJointDetection:" << persistErrorMessage;
    }

    emit analysisCompleted(featureValues);
    emit finished();
}

Multi_featureJointDetection::Multi_featureJointDetection(QObject* parent)
    : QObject(parent)
{
    connect(
        WAVHandle::instance(),
        &WAVHandle::importDataReady,
        this,
        &Multi_featureJointDetection::handleImportedDataReady);
}

Multi_featureJointDetection::~Multi_featureJointDetection()
{
    if (m_workerThread != nullptr && m_workerThread->isRunning()) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void Multi_featureJointDetection::startImportedAnalysis()
{
    {
        QMutexLocker locker(&m_stateMutex);
        m_analysisRequested = true;
    }

    tryStartImportedAnalysis();
}

void Multi_featureJointDetection::handleImportedDataReady()
{
    tryStartImportedAnalysis();
}

void Multi_featureJointDetection::setThresholds(
    double steTh,
    double zcrTh,
    double fdTh)
{
    QMutexLocker locker(&m_stateMutex);
    m_thresholdsConfigured = true;
    m_steThreshold = steTh;
    m_zcrThreshold = zcrTh;
    m_fdThreshold = fdTh;
}

bool Multi_featureJointDetection::busy() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_busy;
}

QVariantMap Multi_featureJointDetection::importedFeatureValues() const
{
    return DataManager::instance()->importedFeatureValues();
}

void Multi_featureJointDetection::handleWorkerCompleted(
    const QVariantMap& featureValues)
{
    DataManager::instance()->updateImportedAnalysisSummary({
        {QStringLiteral("completed"), true},
        {QStringLiteral("success"), true},
        {QStringLiteral("errorMessage"), QString()}
    });
    emit importedFeatureValuesChanged();
    emit importedAnalysisCompleted(featureValues);
}

void Multi_featureJointDetection::handleWorkerFailed(
    const QString& errorMessage)
{
    DataManager::instance()->updateImportedAnalysisSummary({
        {QStringLiteral("completed"), true},
        {QStringLiteral("success"), false},
        {QStringLiteral("errorMessage"), errorMessage}
    });
    DataManager::instance()->storeImportedFeatureValues({});
    emit importedFeatureValuesChanged();
    emit importedAnalysisFailed(errorMessage);
}

void Multi_featureJointDetection::handleWorkerFinished()
{
    m_workerThread = nullptr;
    setBusy(false);
    tryStartImportedAnalysis();
}

void Multi_featureJointDetection::tryStartImportedAnalysis()
{
    const QString temporaryFilePath =
        DataManager::instance()->importedRawTemporaryFilePath();
    if (temporaryFilePath.isEmpty() || !QFile::exists(temporaryFilePath)) {
        qInfo() << "Multi_featureJointDetection: imported data is not ready yet,"
                << "waiting for WAV import";
        return;
    }

    bool thresholdsConfigured = false;
    double steThreshold = 0.0;
    double zcrThreshold = 0.0;
    double fdThreshold = 0.0;
    {
        QMutexLocker locker(&m_stateMutex);
        if (!m_analysisRequested || m_busy) {
            return;
        }
        m_analysisRequested = false;
        m_busy = true;
        thresholdsConfigured = m_thresholdsConfigured;
        steThreshold = m_steThreshold;
        zcrThreshold = m_zcrThreshold;
        fdThreshold = m_fdThreshold;
    }

    emit busyChanged();

    // Keep the QML-facing singleton alive and run a fresh worker per request.
    m_workerThread = new QThread(this);
    auto* worker = new MultiFeatureJointDetectionWorker(
        thresholdsConfigured,
        steThreshold,
        zcrThreshold,
        fdThreshold);
    worker->moveToThread(m_workerThread);

    connect(
        m_workerThread,
        &QThread::started,
        worker,
        [worker, temporaryFilePath]() {
            worker->analyzeImportedTemporaryFile(temporaryFilePath);
        });
    connect(
        worker,
        &MultiFeatureJointDetectionWorker::analysisCompleted,
        this,
        &Multi_featureJointDetection::handleWorkerCompleted);
    connect(
        worker,
        &MultiFeatureJointDetectionWorker::analysisFailed,
        this,
        &Multi_featureJointDetection::handleWorkerFailed);
    connect(
        worker,
        &MultiFeatureJointDetectionWorker::finished,
        m_workerThread,
        &QThread::quit);
    connect(
        m_workerThread,
        &QThread::finished,
        worker,
        &QObject::deleteLater);
    connect(
        m_workerThread,
        &QThread::finished,
        this,
        &Multi_featureJointDetection::handleWorkerFinished);
    connect(
        m_workerThread,
        &QThread::finished,
        m_workerThread,
        &QObject::deleteLater);

    m_workerThread->start();
}

void Multi_featureJointDetection::setBusy(bool busy)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_stateMutex);
        if (m_busy == busy) {
            return;
        }
        m_busy = busy;
        changed = true;
    }
    if (changed) {
        emit busyChanged();
    }
}
