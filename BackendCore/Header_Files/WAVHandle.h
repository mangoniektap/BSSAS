#ifndef WAVHANDLE_H
#define WAVHANDLE_H

#include <QCoreApplication> // qApp definition
#include <QObject>
#include <QString>
#include <QVector>

class QThread;

class WAVHandleWorker : public QObject
{
    Q_OBJECT
public:
    explicit WAVHandleWorker(QObject* parent = nullptr);
    ~WAVHandleWorker() override;

signals:
    void operationCompleted();
    void exportSucceeded(const QString& outputPath);
    void exportFailed(const QString& errorMessage);
    void importDataReady(int dataImportSamplingRate, const QVector<float>& dataImport);
    void importFailed(const QString& errorMessage);

public slots:
    void exportToWav(
        const QString& temporaryFloatFilePath,
        int sampleRate,
        const QString& pythonScriptPath,
        int channelCount = 1);
    void exportRealtimeChannelsToWav(
        const QVector<QString>& channelTemporaryFilePaths,
        int postProcessedChannelCount,
        int sampleRate,
        bool waveletDenoisingEnabled,
        bool transientNoiseSuppressionEnabled,
        bool motionArtifactReductionEnabled,
        const QString& pythonScriptPath);
    void importFromWav(const QString& pythonScriptPath, const QString& wavFilePATH);

private:
    inline static const float MAX_VOLTAGE = 5.0f;
    inline static const float MIN_VOLTAGE = -5.0f;
    inline static const float INT16_MAX_VAL = 32767.0f;
};

class WAVHandle : public QObject
{
    Q_OBJECT
    Q_PROPERTY(
        bool referenceNoiseChannelSaveEnabled
        READ referenceNoiseChannelSaveEnabled
        WRITE setReferenceNoiseChannelSaveEnabled
        NOTIFY referenceNoiseChannelSaveEnabledChanged)
public:
    static WAVHandle* instance()
    {
        if (m_instance == nullptr) {
            if (m_instance == nullptr) {
                m_instance = new WAVHandle(qApp);
            }
        }
        return m_instance;
    }

    Q_INVOKABLE void startSaveAsWav();
    Q_INVOKABLE void startSaveImportedAsWav();
    Q_INVOKABLE void startReadFromWav(const QString& wavFilePATH);

    const QVector<float> timeDomainDataImport() const { return m_timeDomainDataImport; }
    int importSampleRate() const { return m_importSampleRate; }
    bool referenceNoiseChannelSaveEnabled() const { return m_referenceNoiseChannelSaveEnabled; }
    void setReferenceNoiseChannelSaveEnabled(bool enabled);

public slots:
    void updateTimeDomainDataImport(int dataImportSamplingRate, const QVector<float>& dataImport);
    void handleExportSucceeded(const QString& outputPath);
    void handleExportFailed(const QString& errorMessage);
    void handleImportFailed(const QString& errorMessage);

signals:
    void saveCompleted(const QString& outputPath);
    void saveFailed(const QString& errorMessage);
    void importedSaveCompleted(const QString& outputPath);
    void importedSaveFailed(const QString& errorMessage);
    void importDataReady();
    void importFailed(const QString& errorMessage);
    void referenceNoiseChannelSaveEnabledChanged();

private:
    explicit WAVHandle(QObject* parent = nullptr);
    ~WAVHandle() override;
    void startSaveOperation(
        const QString& temporaryFloatFilePath,
        int sampleRate,
        bool importedSave);

    static WAVHandle* m_instance;

    QThread* m_thread = nullptr;
    WAVHandleWorker* m_worker = nullptr;
    QVector<float> m_timeDomainDataImport;
    int m_importSampleRate;
    bool m_referenceNoiseChannelSaveEnabled = false;
};

#endif // WAVHANDLE_H
