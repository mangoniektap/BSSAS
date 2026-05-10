#ifndef SIGNALDFTCALCULATION_H
#define SIGNALDFTCALCULATION_H

#include <QMutex>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class QThread;
class QTimer;

class DFTWorker : public QObject
{
    Q_OBJECT
public:
    explicit DFTWorker(QObject* parent = nullptr);
    ~DFTWorker();

public slots:
    void startWork();
    void stopWork();

private slots:
    void processing();

signals:
    void resultReady(const QVariantList& frequencies, double centerTimeSeconds);

private:
    void produceStftFrames();

    QTimer* m_timer = nullptr;
    QVector<float> m_pendingSamples;
    double m_processedSeconds = 0.0;
    int m_stftWindowSampleCount = 0;
    const int COLLECTION_INTERVAL_MS = 100;
    QMutex m_dataMutex;
};

class SignalDFTCalculation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList dftData READ dftData NOTIFY dftResultReady)
    Q_PROPERTY(bool importBusy READ importBusy NOTIFY importBusyChanged)

public:
    explicit SignalDFTCalculation(QObject* parent = nullptr);
    ~SignalDFTCalculation();

    Q_INVOKABLE const QVariantList& dftData() const { return m_dftData; }
    Q_INVOKABLE bool importBusy() const { return m_importBusy; }
    Q_INVOKABLE void startDFTProcessing();
    Q_INVOKABLE void stopDFTProcessing();
    Q_INVOKABLE void startImportedDftProcessing();
    Q_INVOKABLE QVariantList realtimeStftPointsAtTime(
        double fromFrequencyHz,
        double toFrequencyHz,
        double centerSeconds) const;
    Q_INVOKABLE QVariantMap realtimeStftTimeRangeAtTime(double centerSeconds) const;

signals:
    void dftResultReady();
    void importBusyChanged();
    void importedDftProcessingFinished();
    void processingStart();
    void processingStop();

private:
    void setImportBusy(bool busy);
    void updateDFTData(QVariantList dftData, double centerTimeSeconds);
    void clearRealtimeStftCache();

    QVariantList m_dftData;
    QVector<double> m_realtimeStftCenterTimes;
    QVector<QVariantList> m_realtimeStftFrames;
    static const int MAX_REALTIME_STFT_FRAMES = 300;
    QThread* m_thread = nullptr;
    DFTWorker* m_worker = nullptr;
    QThread* m_importThread = nullptr;
    bool m_importBusy = false;
};

#endif // SIGNALDFTCALCULATION_H
