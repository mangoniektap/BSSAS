#ifndef MULTI_FEATUREJOINTDETECTION_H
#define MULTI_FEATUREJOINTDETECTION_H

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVariantMap>
#include <QVector>

class QThread;

class FeatureDetectionAlgorithm
{
public:
    FeatureDetectionAlgorithm() = default;

    // Preserve the original detector configuration entry points.
    FeatureDetectionAlgorithm(
        int sampleRate,
        double frameLengthMs = 30.0,
        double frameShiftMs = 15.0,
        double maxSilenceMs = 200.0,
        double thresholdT = 0.01);

    void setThresholds(double steTh, double zcrTh, double fdTh);
    QVector<QPair<int, int>> detect(const QVector<double>& rawSignal);
    QVariantMap analyze(const QVector<double>& rawSignal);

private:
    QVector<QVector<double>> frameSignal(const QVector<double>& signal);
    void extractFeatures(
        const QVector<QVector<double>>& frames,
        QVector<double>& steVec,
        QVector<double>& fdVec,
        QVector<double>& zcrVec);
    double computeSTE(const QVector<double>& frame);
    double computeFD(const QVector<double>& frame);
    double computeZCR(const QVector<double>& frame, double thresholdT);
    void estimateAdaptiveThresholds(
        const QVector<double>& ste,
        const QVector<double>& zcr,
        const QVector<double>& fd);
    QVector<QPair<int, int>> detectByThreeStage(
        const QVector<double>& ste,
        const QVector<double>& zcr,
        const QVector<double>& fd,
        int frameShiftSamples);

    int m_sampleRate = 0;
    double m_frameLengthMs = 30.0;
    double m_frameShiftMs = 15.0;
    double m_maxSilenceMs = 200.0;
    double m_thresholdT = 0.01;

    int m_frameLengthSamples = 0;
    int m_frameShiftSamples = 0;
    int m_maxSilenceFrames = 0;

    double m_steThresh = 0.0;
    double m_zcrThresh = 0.0;
    double m_fdThresh = 0.0;
    double m_logEnergyThresh = 0.0;
    double m_envPeakThresh = 0.0;
    double m_transientThresh = 0.0;
    double m_entropyUpperBound = 0.0;
    bool m_thresholdsSet = false;

    QVector<double> m_logSteVec;
    QVector<double> m_envPeakVec;
    QVector<double> m_envSlopeVec;
    QVector<double> m_kurtosisVec;
    QVector<double> m_subbandLowVec;
    QVector<double> m_subbandMidVec;
    QVector<double> m_subbandHighVec;
    QVector<double> m_dominantFreqVec;
    QVector<double> m_spectralCentroidVec;
    QVector<double> m_spectralBandwidthVec;
    QVector<double> m_spectralRolloffVec;
    QVector<double> m_spectralEntropyVec;
    QVector<double> m_multiscaleEnergy1Vec;
    QVector<double> m_multiscaleEnergy2Vec;
    QVector<double> m_multiscaleEnergy3Vec;
    QVector<double> m_waveletEntropyVec;
    QVector<double> m_transientStrengthVec;
    QVector<double> m_logMel1Vec;
    QVector<double> m_logMel2Vec;
    QVector<double> m_logMel3Vec;
    QVector<double> m_candidateScoreVec;
    QVector<double> m_frameRmsVec;
    QVector<double> m_frameDurationMsVec;
    QVector<bool> m_candidateMask;
};

class MultiFeatureJointDetectionWorker : public QObject
{
    Q_OBJECT
public:
    explicit MultiFeatureJointDetectionWorker(
        bool thresholdsConfigured,
        double steThreshold,
        double zcrThreshold,
        double fdThreshold,
        QObject* parent = nullptr);
    ~MultiFeatureJointDetectionWorker() override;

public slots:
    // One-shot worker: read the temp file, analyze it, then write features back.
    void analyzeImportedTemporaryFile(const QString& temporaryFilePath);

signals:
    void analysisCompleted(const QVariantMap& featureValues);
    void analysisFailed(const QString& errorMessage);
    void finished();

private:
    bool m_thresholdsConfigured = false;
    double m_steThreshold = 0.0;
    double m_zcrThreshold = 0.0;
    double m_fdThreshold = 0.0;
};

class Multi_featureJointDetection : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap importedFeatureValues READ importedFeatureValues NOTIFY importedFeatureValuesChanged)

public:
    static Multi_featureJointDetection* instance()
    {
        if (m_instance == nullptr) {
            static QMutex mutex;
            QMutexLocker locker(&mutex);
            if (m_instance == nullptr) {
                m_instance = new Multi_featureJointDetection(qApp);
            }
        }
        return m_instance;
    }

    Q_INVOKABLE void startImportedAnalysis();
    Q_INVOKABLE void setThresholds(double steTh, double zcrTh, double fdTh);

    bool busy() const;
    QVariantMap importedFeatureValues() const;

signals:
    void importedAnalysisCompleted(const QVariantMap& featureValues);
    void importedAnalysisFailed(const QString& errorMessage);
    void busyChanged();
    void importedFeatureValuesChanged();

private slots:
    void handleImportedDataReady();
    void handleWorkerCompleted(const QVariantMap& featureValues);
    void handleWorkerFailed(const QString& errorMessage);
    void handleWorkerFinished();

private:
    explicit Multi_featureJointDetection(QObject* parent = nullptr);
    ~Multi_featureJointDetection() override;

    // Keep the QML-facing instance alive, and spin up a fresh worker thread per run.
    void tryStartImportedAnalysis();
    void setBusy(bool busy);

    static Multi_featureJointDetection* m_instance;

    mutable QMutex m_stateMutex;
    bool m_busy = false;
    bool m_thresholdsConfigured = false;
    bool m_analysisRequested = false;
    double m_steThreshold = 0.0;
    double m_zcrThreshold = 0.0;
    double m_fdThreshold = 0.0;
    QThread* m_workerThread = nullptr;
};

#endif // MULTI_FEATUREJOINTDETECTION_H

