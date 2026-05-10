#include "LocalizationIntestinalSound.h"
#include "KfrDftUtils.h"

#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace {
using ComplexD = std::complex<double>;

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

struct PairFeature {
    int i = 0;
    int j = 0;
    QVector<double> gcc;
    int maxLag = 0;
    double weight = 0.0;
    double snrWeight = 0.0;
    double sharpnessWeight = 0.0;
};

struct CandidateScore {
    Point2D point;
    double stRaw = 0.0;
    double saRaw = 0.0;
    double logPrior = 0.0;
    double fusedScore = 0.0;
};

constexpr int kExpectedChannelCount = 7;
constexpr int kRightMiddleChannelIndex = 3;
constexpr int kCenterMiddleChannelIndex = 4;
constexpr int kLeftMiddleChannelIndex = 5;
constexpr double kPlaceholderEventThreshold = 0.08;
constexpr int kGccFrameSize = 256;
constexpr double kPhatEpsilon = 1e-12;
constexpr double kMapEpsilon = 1e-9;
constexpr double kEffectivePropagationSpeed = 1540.0; // m/s in soft tissue
constexpr double kMapPriorLambda = 0.25;
constexpr double kGridXMin = -0.12;
constexpr double kGridXMax = 0.12;
constexpr double kGridYMin = -0.08;
constexpr double kGridYMax = 0.08;
constexpr double kGridStep = 0.01;

const QVector<Point2D> kSensorPositions = {
    { 0.10, -0.05},
    { 0.00, -0.06},
    {-0.10, -0.05},
    { 0.10,  0.00},
    { 0.00,  0.00},
    {-0.10,  0.00},
    { 0.10,  0.06}
};

const QVector<double> kChannelGainBias = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

QVector<Point2D> buildSearchGrid()
{
    QVector<Point2D> grid;
    for (double y = kGridYMin; y <= kGridYMax + 1e-9; y += kGridStep) {
        for (double x = kGridXMin; x <= kGridXMax + 1e-9; x += kGridStep) {
            grid.append(Point2D{x, y});
        }
    }
    return grid;
}

QVector<Point2D> searchGrid()
{
    static const QVector<Point2D> kGrid = buildSearchGrid();
    return kGrid;
}

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double squared(double value)
{
    return value * value;
}

double safeLog(double value)
{
    return std::log(std::max(value, kMapEpsilon));
}

double averageAbsoluteValue(const QVector<float>& samples)
{
    if (samples.isEmpty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (float value : samples) {
        sum += std::abs(static_cast<double>(value));
    }

    return sum / static_cast<double>(samples.size());
}

double rmsValue(const QVector<float>& samples)
{
    if (samples.isEmpty()) {
        return 0.0;
    }

    double energy = 0.0;
    for (float value : samples) {
        const double v = static_cast<double>(value);
        energy += v * v;
    }
    return std::sqrt(energy / static_cast<double>(samples.size()));
}

QVector<float> centeredPaddedWindow(const QVector<float>& samples, int targetSize)
{
    QVector<float> out(targetSize, 0.0f);
    if (samples.isEmpty() || targetSize <= 0) {
        return out;
    }

    const int copyCount = std::min(targetSize, static_cast<int>(samples.size()));
    const int srcStart = std::max(0, (static_cast<int>(samples.size()) - copyCount) / 2);
    const int dstStart = (targetSize - copyCount) / 2;
    for (int idx = 0; idx < copyCount; ++idx) {
        out[dstStart + idx] = samples[srcStart + idx];
    }

    // Remove DC to stabilize GCC-PHAT peak shape.
    double mean = 0.0;
    for (float v : out) {
        mean += static_cast<double>(v);
    }
    mean /= static_cast<double>(targetSize);
    for (int idx = 0; idx < targetSize; ++idx) {
        out[idx] = static_cast<float>(static_cast<double>(out[idx]) - mean);
    }

    return out;
}

QVector<double> computeGccPhatCorrelation(const QVector<float>& x, const QVector<float>& y)
{
    if (x.size() != y.size() || x.isEmpty()) {
        return {};
    }

    const QVector<ComplexD> xSpec = KfrDftUtils::computeComplexDft(x);
    const QVector<ComplexD> ySpec = KfrDftUtils::computeComplexDft(y);
    const int n = x.size();

    QVector<ComplexD> phatSpec(n, ComplexD(0.0, 0.0));
    for (int k = 0; k < n; ++k) {
        const ComplexD cross = xSpec[k] * std::conj(ySpec[k]);
        const double denom = std::max(std::abs(cross), kPhatEpsilon);
        phatSpec[k] = cross / denom;
    }

    QVector<double> circular = KfrDftUtils::computeInverseComplexDftReal(phatSpec);

    // Reorder to lag axis [-N/2, ..., N/2-1].
    QVector<double> lagCorrelation(n, 0.0);
    const int half = n / 2;
    for (int idx = 0; idx < n; ++idx) {
        const int lagIndex = idx - half;
        const int circularIndex = (lagIndex + n) % n;
        lagCorrelation[idx] = circular[circularIndex];
    }
    return lagCorrelation;
}

double estimatePeakSharpness(const QVector<double>& gcc)
{
    if (gcc.isEmpty()) {
        return 0.0;
    }

    const int n = gcc.size();
    const int peakIndex = static_cast<int>(std::distance(
        gcc.cbegin(),
        std::max_element(gcc.cbegin(), gcc.cend())));
    const double peak = gcc[peakIndex];

    double sideMean = 0.0;
    int sideCount = 0;
    for (int idx = 0; idx < n; ++idx) {
        if (std::abs(idx - peakIndex) <= 2) {
            continue;
        }
        sideMean += std::abs(gcc[idx]);
        ++sideCount;
    }

    const double baseline = sideCount > 0 ? (sideMean / static_cast<double>(sideCount)) : 0.0;
    return peak / std::max(baseline, 1e-6);
}

double channelDistance(int channelIndexA, int channelIndexB)
{
    const Point2D& a = kSensorPositions[channelIndexA];
    const Point2D& b = kSensorPositions[channelIndexB];
    return std::sqrt(squared(a.x - b.x) + squared(a.y - b.y));
}

double distanceToPoint(const Point2D& point, int sensorIndex)
{
    const Point2D& s = kSensorPositions[sensorIndex];
    return std::sqrt(squared(point.x - s.x) + squared(point.y - s.y));
}

double sampleGccAtLag(const QVector<double>& gcc, double lag)
{
    if (gcc.isEmpty()) {
        return 0.0;
    }

    const int n = gcc.size();
    const int half = n / 2;
    const double boundedLag = std::clamp(lag, -static_cast<double>(half), static_cast<double>(half - 1));
    const double floatIndex = boundedLag + static_cast<double>(half);
    const int left = static_cast<int>(std::floor(floatIndex));
    const int right = std::min(left + 1, n - 1);
    const double w = floatIndex - static_cast<double>(left);
    return (1.0 - w) * gcc[left] + w * gcc[right];
}

double weightedMean(const QVector<double>& values, const QVector<double>& weights)
{
    if (values.isEmpty() || values.size() != weights.size()) {
        return 0.0;
    }

    double num = 0.0;
    double den = 0.0;
    for (int i = 0; i < values.size(); ++i) {
        num += weights[i] * values[i];
        den += weights[i];
    }
    return den > kMapEpsilon ? (num / den) : 0.0;
}

double weightedVariance(const QVector<double>& values, const QVector<double>& weights, double mean)
{
    if (values.isEmpty() || values.size() != weights.size()) {
        return 0.0;
    }

    double num = 0.0;
    double den = 0.0;
    for (int i = 0; i < values.size(); ++i) {
        const double diff = values[i] - mean;
        num += weights[i] * diff * diff;
        den += weights[i];
    }
    return den > kMapEpsilon ? (num / den) : 0.0;
}

QVector<double> zScore(const QVector<double>& values)
{
    QVector<double> out(values.size(), 0.0);
    if (values.isEmpty()) {
        return out;
    }

    const double mean = std::accumulate(values.cbegin(), values.cend(), 0.0) /
        static_cast<double>(values.size());
    double variance = 0.0;
    for (double value : values) {
        const double centered = value - mean;
        variance += centered * centered;
    }
    variance /= static_cast<double>(values.size());
    const double stdDev = std::sqrt(std::max(variance, 1e-12));

    for (int i = 0; i < values.size(); ++i) {
        out[i] = (values[i] - mean) / stdDev;
    }
    return out;
}

QString regionLabelFromPoint(const Point2D& point)
{
    const QString horizontal =
        point.x < -0.03 ? QStringLiteral("左") :
        (point.x > 0.03 ? QStringLiteral("右") : QStringLiteral("中"));
    const QString vertical =
        point.y > 0.03 ? QStringLiteral("上") :
        (point.y < -0.03 ? QStringLiteral("下") : QStringLiteral("中"));
    return horizontal + vertical;
}

QString lowConfidenceText(const Point2D& bestPoint, const Point2D& secondPoint)
{
    const QString best = regionLabelFromPoint(bestPoint);
    const QString second = regionLabelFromPoint(secondPoint);
    if (best == second) {
        return best + QStringLiteral("低置信");
    }

    return best + QStringLiteral("/") + second + QStringLiteral("之间");
}
}

class RealtimeIntestinalSoundEventDetector
{
public:
    bool detect(const QVector<float>& fusedMiddleSamples, double* eventScore) const
    {
        const double score = averageAbsoluteValue(fusedMiddleSamples);
        if (eventScore != nullptr) {
            *eventScore = score;
        }
        return score >= kPlaceholderEventThreshold;
    }
};

class RealtimeIntestinalSoundLocalizer
{
public:
    bool localize(
        const QVector<QVector<float>>& channelSamples,
        int sampleRate,
        int* bestChannelIndex,
        double* confidence,
        QVector<double>* channelScores,
        QString* decisionText) const
    {
        if (channelSamples.size() != kExpectedChannelCount || sampleRate <= 0) {
            return false;
        }

        QVector<double> energyByChannel(kExpectedChannelCount, 0.0);
        QVector<double> amplitudeLogByChannel(kExpectedChannelCount, 0.0);
        QVector<double> reliabilityByChannel(kExpectedChannelCount, 0.0);
        double maxEnergy = 0.0;

        for (int ch = 0; ch < kExpectedChannelCount; ++ch) {
            const double rms = rmsValue(channelSamples[ch]);
            const double energy = rms * rms;
            energyByChannel[ch] = energy;
            amplitudeLogByChannel[ch] = safeLog(std::max(energy, kMapEpsilon));
            maxEnergy = std::max(maxEnergy, energy);
        }
        for (int ch = 0; ch < kExpectedChannelCount; ++ch) {
            reliabilityByChannel[ch] = clamp01(energyByChannel[ch] / std::max(maxEnergy, kMapEpsilon));
        }

        const double maxPairDistance = [&]() {
            double d = 0.0;
            for (int i = 0; i < kExpectedChannelCount; ++i) {
                for (int j = i + 1; j < kExpectedChannelCount; ++j) {
                    d = std::max(d, channelDistance(i, j));
                }
            }
            return std::max(d, 1e-6);
        }();

        QVector<PairFeature> pairFeatures;
        pairFeatures.reserve((kExpectedChannelCount * (kExpectedChannelCount - 1)) / 2);

        for (int i = 0; i < kExpectedChannelCount; ++i) {
            for (int j = i + 1; j < kExpectedChannelCount; ++j) {
                const QVector<float> xi = centeredPaddedWindow(channelSamples[i], kGccFrameSize);
                const QVector<float> xj = centeredPaddedWindow(channelSamples[j], kGccFrameSize);
                const QVector<double> gcc = computeGccPhatCorrelation(xi, xj);
                if (gcc.isEmpty()) {
                    continue;
                }

                const double snrPair = clamp01(
                    std::min(energyByChannel[i], energyByChannel[j]) /
                    std::max(std::max(energyByChannel[i], energyByChannel[j]), kMapEpsilon));
                const double sharpnessRaw = estimatePeakSharpness(gcc);
                const double sharpness = clamp01(sharpnessRaw / 8.0);
                const double contact = clamp01(0.5 * (reliabilityByChannel[i] + reliabilityByChannel[j]));
                const double geometry = clamp01(channelDistance(i, j) / maxPairDistance);

                PairFeature pair;
                pair.i = i;
                pair.j = j;
                pair.gcc = gcc;
                pair.maxLag = static_cast<int>(gcc.size() / 2);
                pair.snrWeight = snrPair;
                pair.sharpnessWeight = sharpness;
                pair.weight = 0.30 * snrPair + 0.30 * sharpness + 0.20 * contact + 0.20 * geometry;
                pairFeatures.append(pair);
            }
        }

        if (pairFeatures.isEmpty()) {
            return false;
        }

        const QVector<Point2D> grid = searchGrid();
        QVector<CandidateScore> candidates;
        candidates.reserve(grid.size());

        QVector<double> stAll;
        QVector<double> saAll;
        QVector<double> priorAll;
        stAll.reserve(grid.size());
        saAll.reserve(grid.size());
        priorAll.reserve(grid.size());

        for (const Point2D& point : grid) {
            CandidateScore candidate;
            candidate.point = point;

            // 1) TDOA main score from GCC-PHAT pairwise delay consistency.
            double st = 0.0;
            double weightSum = 0.0;
            for (const PairFeature& pair : pairFeatures) {
                const double di = distanceToPoint(point, pair.i);
                const double dj = distanceToPoint(point, pair.j);
                const double tau = (di - dj) / kEffectivePropagationSpeed;
                const double lag = tau * static_cast<double>(sampleRate);
                const double value = sampleGccAtLag(pair.gcc, lag);
                st += pair.weight * value;
                weightSum += pair.weight;
            }
            candidate.stRaw = weightSum > kMapEpsilon ? (st / weightSum) : 0.0;

            // 2) Amplitude likelihood using Ai ~= beta0 - beta1*di + gi.
            QVector<double> x;
            QVector<double> y;
            QVector<double> v;
            x.reserve(kExpectedChannelCount);
            y.reserve(kExpectedChannelCount);
            v.reserve(kExpectedChannelCount);
            for (int ch = 0; ch < kExpectedChannelCount; ++ch) {
                x.append(-distanceToPoint(point, ch));
                y.append(amplitudeLogByChannel[ch] - kChannelGainBias[ch]);
                v.append(std::max(reliabilityByChannel[ch], 0.05));
            }

            const double xMean = weightedMean(x, v);
            const double yMean = weightedMean(y, v);
            const double varX = weightedVariance(x, v, xMean);
            double covXY = 0.0;
            double wSum = 0.0;
            for (int idx = 0; idx < x.size(); ++idx) {
                covXY += v[idx] * (x[idx] - xMean) * (y[idx] - yMean);
                wSum += v[idx];
            }
            covXY = wSum > kMapEpsilon ? (covXY / wSum) : 0.0;

            const double beta1 = covXY / std::max(varX, kMapEpsilon);
            const double beta0 = yMean - beta1 * xMean;

            double residual = 0.0;
            for (int idx = 0; idx < x.size(); ++idx) {
                const double yHat = beta0 + beta1 * x[idx];
                const double err = y[idx] - yHat;
                residual += v[idx] * err * err;
            }
            candidate.saRaw = -residual;

            // 3) Anatomical prior in abdomen region.
            constexpr double kPriorSigmaX = 0.09;
            constexpr double kPriorSigmaY = 0.06;
            candidate.logPrior = -0.5 * (
                squared(point.x / kPriorSigmaX) + squared(point.y / kPriorSigmaY));

            candidates.append(candidate);
            stAll.append(candidate.stRaw);
            saAll.append(candidate.saRaw);
            priorAll.append(candidate.logPrior);
        }

        if (candidates.size() < 2) {
            return false;
        }

        const QVector<double> stNorm = zScore(stAll);
        const QVector<double> saNorm = zScore(saAll);
        const QVector<double> priorNorm = zScore(priorAll);

        // Adaptive alpha: sharp GCC and better cross-channel consistency -> larger alpha.
        double meanSharpness = 0.0;
        double meanSnr = 0.0;
        for (const PairFeature& pair : pairFeatures) {
            meanSharpness += pair.sharpnessWeight;
            meanSnr += pair.snrWeight;
        }
        meanSharpness /= static_cast<double>(pairFeatures.size());
        meanSnr /= static_cast<double>(pairFeatures.size());
        const double quality = 0.6 * meanSharpness + 0.4 * meanSnr;
        const double alpha = std::clamp(0.5 + 0.4 * quality, 0.5, 0.9);

        for (int idx = 0; idx < candidates.size(); ++idx) {
            candidates[idx].fusedScore =
                alpha * stNorm[idx] +
                (1.0 - alpha) * saNorm[idx] +
                kMapPriorLambda * priorNorm[idx];
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const CandidateScore& lhs, const CandidateScore& rhs) {
                return lhs.fusedScore > rhs.fusedScore;
            });

        const CandidateScore& best = candidates[0];
        const CandidateScore& second = candidates[1];

        // Convert best location to nearest channel index for existing signal compatibility.
        int nearestChannel = 0;
        double nearestDistance = std::numeric_limits<double>::infinity();
        for (int ch = 0; ch < kExpectedChannelCount; ++ch) {
            const double d = distanceToPoint(best.point, ch);
            if (d < nearestDistance) {
                nearestDistance = d;
                nearestChannel = ch;
            }
        }

        // Recommended confidence from top-2 MAP score gap.
        const double conf = clamp01(
            (best.fusedScore - second.fusedScore) /
            (std::abs(best.fusedScore) + kMapEpsilon));

        QVector<double> outScores(kExpectedChannelCount, 0.0);
        for (int ch = 0; ch < kExpectedChannelCount; ++ch) {
            const double d = distanceToPoint(best.point, ch);
            outScores[ch] = 1.0 / (d + 1e-3);
        }

        QString decision;
        if (conf < 0.08) {
            decision = QStringLiteral("多源重叠，放弃定位");
        } else if (conf < 0.18) {
            decision = lowConfidenceText(best.point, second.point);
        } else {
            decision = regionLabelFromPoint(best.point) + QStringLiteral("高置信");
        }

        if (bestChannelIndex != nullptr) {
            *bestChannelIndex = nearestChannel;
        }
        if (confidence != nullptr) {
            *confidence = conf;
        }
        if (channelScores != nullptr) {
            *channelScores = outScores;
        }
        if (decisionText != nullptr) {
            *decisionText = decision;
        }

        return true;
    }
};

LocalizationIntestinalSound* LocalizationIntestinalSound::m_instance = nullptr;

LocalizationIntestinalSound::LocalizationIntestinalSound(QObject* parent)
    : QObject(parent)
    , m_eventDetector(std::make_unique<RealtimeIntestinalSoundEventDetector>())
{
}

LocalizationIntestinalSound::~LocalizationIntestinalSound() = default;

void LocalizationIntestinalSound::startRealtimePipeline(int sampleRate)
{
    bool runningChangedNeeded = false;
    {
        QMutexLocker locker(&m_stateMutex);
        if (m_running) {
            return;
        }

        m_sampleRate = std::max(sampleRate, 1);
        m_processedSamples = 0;
        m_lastEventSampleIndex = -1;
        resetLocalizer();
        m_running = true;
        runningChangedNeeded = true;
    }

    if (runningChangedNeeded) {
        emit runningChanged();
    }
}

void LocalizationIntestinalSound::stopRealtimePipeline()
{
    bool runningChangedNeeded = false;
    bool localizerChangedNeeded = false;

    {
        QMutexLocker locker(&m_stateMutex);
        if (!m_running) {
            return;
        }

        m_running = false;
        m_sampleRate = 0;
        m_processedSamples = 0;
        m_lastEventSampleIndex = -1;
        runningChangedNeeded = true;

        if (m_localizerActive) {
            localizerChangedNeeded = true;
        }
        resetLocalizer();
    }

    if (runningChangedNeeded) {
        emit runningChanged();
    }
    if (localizerChangedNeeded) {
        emit localizerActiveChanged();
    }
}

void LocalizationIntestinalSound::processRealtimeFrame(const QVector<QVector<float>>& channelSamples)
{
    if (channelSamples.size() != kExpectedChannelCount) {
        return;
    }

    bool shouldEmitEvent = false;
    bool shouldEmitLocalizerChanged = false;
    bool shouldEmitLocalization = false;
    double eventTimeSeconds = 0.0;
    double eventScore = 0.0;
    int bestChannelIndex = 0;
    double confidence = 0.0;
    QVector<double> channelScores;
    QString decisionText;

    {
        QMutexLocker locker(&m_stateMutex);
        if (!m_running || m_sampleRate <= 0) {
            return;
        }

        const qint64 frameSampleCount = static_cast<qint64>(channelSamples[0].size());
        if (frameSampleCount <= 0) {
            return;
        }

        const QVector<float> fusedMiddleSamples = fuseMiddleChannels(channelSamples);
        if (!detectEvent(fusedMiddleSamples, &eventScore)) {
            m_processedSamples += frameSampleCount;
            return;
        }

        m_lastEventSampleIndex = m_processedSamples;
        eventTimeSeconds = static_cast<double>(m_lastEventSampleIndex) /
            static_cast<double>(m_sampleRate);
        shouldEmitEvent = true;

        if (!m_localizerActive) {
            ensureLocalizerCreated();
            shouldEmitLocalizerChanged = true;
        }

        runLocalization(
            channelSamples,
            &bestChannelIndex,
            &confidence,
            &channelScores,
            &decisionText);
        shouldEmitLocalization = !channelScores.isEmpty();
        m_processedSamples += frameSampleCount;
    }

    if (shouldEmitEvent) {
        emit intestinalSoundEventDetected(eventTimeSeconds, eventScore);
    }
    if (shouldEmitLocalizerChanged) {
        emit localizerActiveChanged();
    }
    if (shouldEmitLocalization) {
        emit localizationUpdated(bestChannelIndex, confidence, channelScores);
        emit localizationDecisionUpdated(decisionText, confidence);
    }
}

bool LocalizationIntestinalSound::running() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_running;
}

bool LocalizationIntestinalSound::localizerActive() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_localizerActive;
}

QVector<float> LocalizationIntestinalSound::fuseMiddleChannels(
    const QVector<QVector<float>>& channelSamples) const
{
    if (channelSamples.size() != kExpectedChannelCount) {
        return {};
    }

    const QVector<float>& rightMiddle = channelSamples[kRightMiddleChannelIndex];
    const QVector<float>& centerMiddle = channelSamples[kCenterMiddleChannelIndex];
    const QVector<float>& leftMiddle = channelSamples[kLeftMiddleChannelIndex];
    const int sampleCount = std::min(
        {static_cast<int>(rightMiddle.size()),
         static_cast<int>(centerMiddle.size()),
         static_cast<int>(leftMiddle.size())});
    if (sampleCount <= 0) {
        return {};
    }

    QVector<float> fused;
    fused.resize(sampleCount);
    for (int index = 0; index < sampleCount; ++index) {
        fused[index] =
            (rightMiddle[index] + centerMiddle[index] + leftMiddle[index]) / 3.0f;
    }
    return fused;
}

QVector<float> LocalizationIntestinalSound::fuseVisibleChannels(
    const QVector<QVector<float>>& channelSamples) const
{
    if (channelSamples.size() != kExpectedChannelCount) {
        return {};
    }

    int minSampleCount = std::numeric_limits<int>::max();
    for (const QVector<float>& channel : channelSamples) {
        minSampleCount = std::min(minSampleCount, static_cast<int>(channel.size()));
    }
    if (minSampleCount <= 0 || minSampleCount == std::numeric_limits<int>::max()) {
        return {};
    }

    QVector<float> fused;
    fused.resize(minSampleCount);
    for (int sampleIndex = 0; sampleIndex < minSampleCount; ++sampleIndex) {
        double sum = 0.0;
        for (int channelIndex = 0; channelIndex < kExpectedChannelCount; ++channelIndex) {
            sum += channelSamples[channelIndex][sampleIndex];
        }
        fused[sampleIndex] = static_cast<float>(sum / static_cast<double>(kExpectedChannelCount));
    }
    return fused;
}

bool LocalizationIntestinalSound::detectEvent(
    const QVector<float>& fusedMiddleSamples,
    double* eventScore) const
{
    if (!m_eventDetector) {
        return false;
    }

    return m_eventDetector->detect(fusedMiddleSamples, eventScore);
}

void LocalizationIntestinalSound::ensureLocalizerCreated()
{
    if (!m_localizer) {
        m_localizer = std::make_unique<RealtimeIntestinalSoundLocalizer>();
    }
    m_localizerActive = true;
}

void LocalizationIntestinalSound::resetLocalizer()
{
    m_localizer.reset();
    m_localizerActive = false;
}

void LocalizationIntestinalSound::runLocalization(
    const QVector<QVector<float>>& channelSamples,
    int* bestChannelIndex,
    double* confidence,
    QVector<double>* channelScores,
    QString* decisionText) const
{
    if (!m_localizer) {
        return;
    }

    if (fuseVisibleChannels(channelSamples).isEmpty()) {
        return;
    }

    const bool ok = m_localizer->localize(
        channelSamples,
        m_sampleRate,
        bestChannelIndex,
        confidence,
        channelScores,
        decisionText);
    if (!ok && decisionText != nullptr) {
        *decisionText = QStringLiteral("定位失败");
    }
}
