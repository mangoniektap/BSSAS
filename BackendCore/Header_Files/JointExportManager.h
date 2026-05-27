/** @file JointExportManager.h
 *  @brief 联合导出管理器，协调实时音频、分析报告和采集信息报告的成套保存流程。
 */

#ifndef JOINTEXPORTMANAGER_H
#define JOINTEXPORTMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantMap>

class GenerateManager;
class Multi_featureJointDetection;
class SignalDFTCalculation;
class WAVHandle;

class JointExportManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString outputDirectory READ outputDirectory NOTIFY outputDirectoryChanged)
    Q_PROPERTY(QString baseName READ baseName NOTIFY baseNameChanged)
    Q_PROPERTY(QString wavFilePath READ wavFilePath NOTIFY wavFilePathChanged)

public:
    JointExportManager(
        WAVHandle* wavHandle,
        SignalDFTCalculation* signalDftCalculation,
        Multi_featureJointDetection* multiFeatureJointDetection,
        GenerateManager* generateManager,
        QObject* parent = nullptr);
    ~JointExportManager() override;

    bool active() const { return m_active; }
    bool saving() const { return m_saving; }
    QString statusMessage() const { return m_statusMessage; }
    QString outputDirectory() const { return m_outputDirectory; }
    QString baseName() const { return m_baseName; }
    QString wavFilePath() const { return m_wavFilePath; }

    Q_INVOKABLE bool beginExport(const QString& parentDirectoryPath);
    Q_INVOKABLE bool saveWithMedicalRecord(const QVariantMap& formData);

signals:
    void activeChanged();
    void savingChanged();
    void statusMessageChanged();
    void outputDirectoryChanged();
    void baseNameChanged();
    void wavFilePathChanged();
    void completed(const QString& outputDirectory);
    void failed(const QString& errorMessage);

private slots:
    void handleRealtimeWavSaved(const QString& outputPath);
    void handleRealtimeWavSaveFailed(const QString& errorMessage);
    void handleImportedDataReady();
    void handleImportFailed(const QString& errorMessage);
    void handleImportedDftFinished();
    void handleImportedAnalysisCompleted(const QVariantMap& featureValues);
    void handleImportedAnalysisFailed(const QString& errorMessage);

private:
    void resetTaskState();
    void fail(const QString& errorMessage);
    void setActive(bool active);
    void setSaving(bool saving);
    void setStatusMessage(const QString& statusMessage);
    void setOutputDirectory(const QString& outputDirectory);
    void setBaseName(const QString& baseName);
    void setWavFilePath(const QString& wavFilePath);
    bool prepareOutputPaths(const QString& parentDirectoryPath, QString* errorMessage);
    void startFinalExportIfReady();
    void handleFinalExportCompleted(bool succeeded, const QString& errorMessage);

    WAVHandle* m_wavHandle = nullptr;
    SignalDFTCalculation* m_signalDftCalculation = nullptr;
    Multi_featureJointDetection* m_multiFeatureJointDetection = nullptr;
    GenerateManager* m_generateManager = nullptr;

    bool m_active = false;
    bool m_saving = false;
    bool m_wavReady = false;
    bool m_importReady = false;
    bool m_dftReady = false;
    bool m_analysisReady = false;
    bool m_finalExportRunning = false;
    QString m_statusMessage;
    QString m_outputDirectory;
    QString m_baseName;
    QString m_wavFilePath;
    QVariantMap m_medicalRecordFormData;
};

#endif // JOINTEXPORTMANAGER_H
