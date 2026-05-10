#ifndef ADAPTIVEDOWNSAMPLING_H
#define ADAPTIVEDOWNSAMPLING_H

#include <QObject>
#include <QPointF>
#include <QVariantList>
#include <QVector>

class QThread;
class QTimer;

class DownsamplingWorker : public QObject
{
    Q_OBJECT
public:
    explicit DownsamplingWorker(QObject* parent = nullptr);
    ~DownsamplingWorker() override;

public slots:
    void startWork();
    void stopWork();

private slots:
    void processing();

signals:
    void resultReady(const QVariantList& data);
    void pointsPerFrameChanged(int pointsPerFrame);

private:
    int calculateTargetPointCount(qsizetype sampleCount, int sampleRate) const;
    QVector<QPointF> peakDetectingDownsampling(
        const QVector<float>& rawData,
        int targetPointCount,
        int sampleRate) const;

    QTimer* m_timer = nullptr;
    int m_pointsPerFrame = 1;
    double m_elapsedSeconds = 0.0;
};

class AdaptiveDownsampling : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int pointsPerFrame READ pointsPerFrame NOTIFY pointsPerFrameChanged)
    Q_PROPERTY(QVariantList downsampledData READ downsampledData NOTIFY downsampledDataReady)
    Q_PROPERTY(int currentDownsamplingLevel READ currentDownsamplingLevel CONSTANT)
    Q_PROPERTY(int downsamplingPointsPerSecond READ downsamplingPointsPerSecond CONSTANT)
    Q_PROPERTY(float rawDataResolutionThreshold READ rawDataResolutionThreshold CONSTANT)

public:
    static constexpr int LEVEL0 = 0;
    static constexpr int LEVEL7 = 7;
    static constexpr int CURRENT_DOWNSAMPLING_LEVEL = LEVEL7;
    static constexpr int CURRENT_LEVEL_POINTS_PER_SECOND = 480;
    static constexpr float RAW_DATA_RESOLUTION_THRESHOLD = 0.05f;
    static constexpr int PROCESSING_INTERVAL_MS = 100;

    explicit AdaptiveDownsampling(QObject* parent = nullptr);
    ~AdaptiveDownsampling() override;

    Q_INVOKABLE int pointsPerFrame() const { return m_pointsPerFrame; }
    Q_INVOKABLE const QVariantList& downsampledData() const { return m_downsampledData; }
    Q_INVOKABLE int currentDownsamplingLevel() const { return CURRENT_DOWNSAMPLING_LEVEL; }
    Q_INVOKABLE int downsamplingPointsPerSecond() const { return CURRENT_LEVEL_POINTS_PER_SECOND; }
    Q_INVOKABLE float rawDataResolutionThreshold() const { return RAW_DATA_RESOLUTION_THRESHOLD; }

    static QVector<QPointF> buildCurrentLevelDownsampledPoints(
        const QVector<float>& source,
        int sampleRate);

    Q_INVOKABLE void startDownsamplingProcessing();
    Q_INVOKABLE void stopDownsamplingProcessing() { emit processingStop(); }

signals:
    void pointsPerFrameChanged();
    void downsampledDataReady();
    void processingStart();
    void processingStop();

private:
    void updatePointsPerFrame(int pointsPerFrame);
    void updateDownsampledData(QVariantList downsampledData);

    int m_pointsPerFrame =
        CURRENT_LEVEL_POINTS_PER_SECOND * PROCESSING_INTERVAL_MS / 1000;
    QVariantList m_downsampledData;
    QThread* m_thread = nullptr;
    DownsamplingWorker* m_worker = nullptr;
};

#endif  // ADAPTIVEDOWNSAMPLING_H
