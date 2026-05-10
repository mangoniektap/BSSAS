#ifndef SIGNALPREPROCESSING_H
#define SIGNALPREPROCESSING_H

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QVector>
#include <array>

#include "ActiveNoiseCancellation.h"
#include "AdaptiveNoiseReduction.h"
#include "MotionArtifactReduction.h"
#include "WaveletTransform.h"

#include <kfr/dsp.hpp>

class QThread;
class QTimer;

struct SignalPreprocessOptions
{
    bool bandpassEnabled = true;
    bool notchEnabled = true;
    bool firFilterEnabled = false;
    bool activeNoiseCancellationEnabled = true;
    bool adaptiveNoiseReductionEnabled = true;
    bool waveletEnabled = true;
    bool transientNoiseSuppressionEnabled = true;
    bool motionArtifactReductionEnabled = true;
    double gain = 1.0;
};

class PreprocessingWorker : public QObject
{
    Q_OBJECT

public:
    explicit PreprocessingWorker(QObject* parent = nullptr);
    ~PreprocessingWorker() override;

public slots:
    void updateChannel(int channel);
    void updateProcessingSession(int session);
    void processing();
    void flushProcessing();

signals:
    void resultReady(const QVector<float>& preprocessedData, int processingSession);

private:
    struct RealtimePreFilterState
    {
        kfr::iir_state<double, 4> state{ kfr::iir_params<double, 4>() };
    };

    void resetRealtimeAncStates();
    void resetRealtimeDenoiseStates();
    void initPreFilter(int sampleRate, const SignalPreprocessOptions& options);

    QMutex m_dataMutex;
    int m_currentChannel = 0;
    int m_processingSession = 0;
    int m_preFilterSampleRate = 0;
    bool m_preFilterBandpassEnabled = true;
    bool m_preFilterNotchEnabled = true;
    bool m_hasPreFilterState = false;
    std::array<RealtimePreFilterState, 8> m_preFilterStates{};
    std::array<AdaptiveNoiseReduction::StreamingState, 8> m_realtimeAdaptiveStates{};
    std::array<ActiveNoiseCancellation::StreamingState, 8> m_realtimeAncStates{};
    int m_realtimeDenoiseSampleRate = 0;
    bool m_realtimeAncEnabled = true;
    bool m_realtimeAdaptiveEnabled = true;
};

class SignalPreprocessing : public QObject
{
    Q_OBJECT

    Q_PROPERTY(
        int currentChannel
        READ currentChannel
        WRITE setCurrentChannel
        NOTIFY channelChanged)
    Q_PROPERTY(
        bool importAllProcessingEnabled
        READ importAllProcessingEnabled
        NOTIFY importProcessingSettingsChanged)
    Q_PROPERTY(
        bool importBandpassEnabled
        READ importBandpassEnabled
        WRITE setImportBandpassEnabled
        NOTIFY importBandpassEnabledChanged)
    Q_PROPERTY(
        bool importNotchEnabled
        READ importNotchEnabled
        WRITE setImportNotchEnabled
        NOTIFY importNotchEnabledChanged)
    Q_PROPERTY(
        bool importAdaptiveNoiseReductionEnabled
        READ importAdaptiveNoiseReductionEnabled
        WRITE setImportAdaptiveNoiseReductionEnabled
        NOTIFY importAdaptiveNoiseReductionEnabledChanged)
    Q_PROPERTY(
        bool importWaveletDenoisingEnabled
        READ importWaveletDenoisingEnabled
        WRITE setImportWaveletDenoisingEnabled
        NOTIFY importWaveletDenoisingEnabledChanged)
    Q_PROPERTY(
        bool importTransientNoiseSuppressionEnabled
        READ importTransientNoiseSuppressionEnabled
        WRITE setImportTransientNoiseSuppressionEnabled
        NOTIFY importTransientNoiseSuppressionEnabledChanged)
    Q_PROPERTY(
        bool importMotionArtifactReductionEnabled
        READ importMotionArtifactReductionEnabled
        WRITE setImportMotionArtifactReductionEnabled
        NOTIFY importMotionArtifactReductionEnabledChanged)
    Q_PROPERTY(
        bool importFirFilterEnabled
        READ importFirFilterEnabled
        WRITE setImportFirFilterEnabled
        NOTIFY importFirFilterEnabledChanged)
    Q_PROPERTY(
        bool realtimeAllProcessingEnabled
        READ realtimeAllProcessingEnabled
        NOTIFY realtimeProcessingSettingsChanged)
    Q_PROPERTY(
        bool realtimeBandpassEnabled
        READ realtimeBandpassEnabled
        WRITE setRealtimeBandpassEnabled
        NOTIFY realtimeBandpassEnabledChanged)
    Q_PROPERTY(
        bool realtimeNotchEnabled
        READ realtimeNotchEnabled
        WRITE setRealtimeNotchEnabled
        NOTIFY realtimeNotchEnabledChanged)
    Q_PROPERTY(
        bool realtimeActiveNoiseCancellationEnabled
        READ realtimeActiveNoiseCancellationEnabled
        WRITE setRealtimeActiveNoiseCancellationEnabled
        NOTIFY realtimeActiveNoiseCancellationEnabledChanged)
    Q_PROPERTY(
        bool realtimeAdaptiveNoiseReductionEnabled
        READ realtimeAdaptiveNoiseReductionEnabled
        WRITE setRealtimeAdaptiveNoiseReductionEnabled
        NOTIFY realtimeAdaptiveNoiseReductionEnabledChanged)
    Q_PROPERTY(
        bool realtimeWaveletDenoisingEnabled
        READ realtimeWaveletDenoisingEnabled
        WRITE setRealtimeWaveletDenoisingEnabled
        NOTIFY realtimeWaveletDenoisingEnabledChanged)
    Q_PROPERTY(
        bool realtimeTransientNoiseSuppressionEnabled
        READ realtimeTransientNoiseSuppressionEnabled
        WRITE setRealtimeTransientNoiseSuppressionEnabled
        NOTIFY realtimeTransientNoiseSuppressionEnabledChanged)
    Q_PROPERTY(
        bool realtimeMotionArtifactReductionEnabled
        READ realtimeMotionArtifactReductionEnabled
        WRITE setRealtimeMotionArtifactReductionEnabled
        NOTIFY realtimeMotionArtifactReductionEnabledChanged)
    Q_PROPERTY(
        bool realtimeFirFilterEnabled
        READ realtimeFirFilterEnabled
        WRITE setRealtimeFirFilterEnabled
        NOTIFY realtimeFirFilterEnabledChanged)
    Q_PROPERTY(
        double realtimeGain
        READ realtimeGain
        WRITE setRealtimeGain
        NOTIFY realtimeGainChanged)

public:
    friend class PreprocessingWorker;

    static SignalPreprocessing* instance()
    {
        if (m_instance == nullptr) {
            static QMutex mutex;
            QMutexLocker locker(&mutex);
            if (m_instance == nullptr) {
                m_instance = new SignalPreprocessing(qApp);
            }
        }
        return m_instance;
    }

    QVector<float> filterDataImport(int samplingRate, const QVector<float>& rawData);

    int currentChannel() const { return m_currentChannel; }
    bool importAllProcessingEnabled() const;
    bool importBandpassEnabled() const;
    bool importNotchEnabled() const;
    bool importAdaptiveNoiseReductionEnabled() const;
    bool importWaveletDenoisingEnabled() const;
    bool importTransientNoiseSuppressionEnabled() const;
    bool importMotionArtifactReductionEnabled() const;
    bool importFirFilterEnabled() const;
    bool realtimeAllProcessingEnabled() const;
    bool realtimeBandpassEnabled() const;
    bool realtimeNotchEnabled() const;
    bool realtimeActiveNoiseCancellationEnabled() const;
    bool realtimeAdaptiveNoiseReductionEnabled() const;
    bool realtimeWaveletDenoisingEnabled() const;
    bool realtimeTransientNoiseSuppressionEnabled() const;
    bool realtimeMotionArtifactReductionEnabled() const;
    bool realtimeFirFilterEnabled() const;
    double realtimeGain() const;

    Q_INVOKABLE void setCurrentChannel(int channel);
    Q_INVOKABLE void setAllImportProcessingEnabled(bool enabled);
    Q_INVOKABLE void setAllRealtimeProcessingEnabled(bool enabled);
    void setImportBandpassEnabled(bool enabled);
    void setImportNotchEnabled(bool enabled);
    void setImportAdaptiveNoiseReductionEnabled(bool enabled);
    void setImportWaveletDenoisingEnabled(bool enabled);
    void setImportTransientNoiseSuppressionEnabled(bool enabled);
    void setImportMotionArtifactReductionEnabled(bool enabled);
    void setImportFirFilterEnabled(bool enabled);
    void setRealtimeBandpassEnabled(bool enabled);
    void setRealtimeNotchEnabled(bool enabled);
    void setRealtimeActiveNoiseCancellationEnabled(bool enabled);
    void setRealtimeAdaptiveNoiseReductionEnabled(bool enabled);
    void setRealtimeWaveletDenoisingEnabled(bool enabled);
    void setRealtimeTransientNoiseSuppressionEnabled(bool enabled);
    void setRealtimeMotionArtifactReductionEnabled(bool enabled);
    void setRealtimeFirFilterEnabled(bool enabled);
    void setRealtimeGain(double gain);

    Q_INVOKABLE void startPreprocessing();
    Q_INVOKABLE void stopPreprocessing();

    void updateDataCache(const QVector<float>& preprocessedData, int processingSession);
    QVector<float> getDataCache() const;

private:
    explicit SignalPreprocessing(QObject* parent = nullptr);
    ~SignalPreprocessing() override;

    SignalPreprocessOptions importPreprocessOptions() const;
    SignalPreprocessOptions realtimePreprocessOptions() const;

    static SignalPreprocessing* m_instance;
    static constexpr int COLLECTION_INTERVAL_MS = 100;

    QThread* m_thread = nullptr;
    PreprocessingWorker* m_worker = nullptr;
    QTimer* m_timer = nullptr;
    int m_currentChannel = 0;
    int m_processingSession = 0;
    bool m_processingActive = false;
    QVector<float> m_preprocessedDataCache;
    mutable QMutex m_dataCacheMutex;
    mutable QMutex m_importSettingsMutex;
    mutable QMutex m_realtimeSettingsMutex;
    bool m_importBandpassEnabled = true;
    bool m_importNotchEnabled = true;
    bool m_importAdaptiveNoiseReductionEnabled = true;
    bool m_importWaveletDenoisingEnabled = true;
    bool m_importTransientNoiseSuppressionEnabled = true;
    bool m_importMotionArtifactReductionEnabled = true;
    bool m_importFirFilterEnabled = false;
    bool m_realtimeBandpassEnabled = true;
    bool m_realtimeNotchEnabled = true;
    bool m_realtimeActiveNoiseCancellationEnabled = true;
    bool m_realtimeAdaptiveNoiseReductionEnabled = true;
    bool m_realtimeWaveletDenoisingEnabled = true;
    bool m_realtimeTransientNoiseSuppressionEnabled = true;
    bool m_realtimeMotionArtifactReductionEnabled = true;
    bool m_realtimeFirFilterEnabled = false;
    double m_realtimeGain = 1.0;

signals:
    void channelChanged(int channel);
    void importProcessingSettingsChanged();
    void importBandpassEnabledChanged();
    void importNotchEnabledChanged();
    void importAdaptiveNoiseReductionEnabledChanged();
    void importWaveletDenoisingEnabledChanged();
    void importTransientNoiseSuppressionEnabledChanged();
    void importMotionArtifactReductionEnabledChanged();
    void importFirFilterEnabledChanged();
    void realtimeProcessingSettingsChanged();
    void realtimeBandpassEnabledChanged();
    void realtimeNotchEnabledChanged();
    void realtimeActiveNoiseCancellationEnabledChanged();
    void realtimeAdaptiveNoiseReductionEnabledChanged();
    void realtimeWaveletDenoisingEnabledChanged();
    void realtimeTransientNoiseSuppressionEnabledChanged();
    void realtimeMotionArtifactReductionEnabledChanged();
    void realtimeFirFilterEnabledChanged();
    void realtimeGainChanged();
};

#endif // SIGNALPREPROCESSING_H
