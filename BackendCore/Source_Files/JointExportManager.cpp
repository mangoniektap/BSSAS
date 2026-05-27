#include "JointExportManager.h"

#include "GenerateManager.h"
#include "Multi_featureJointDetection.h"
#include "SignalDFTCalculation.h"
#include "WAVHandle.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>

namespace {
QString normalizeLocalDirectoryPath(const QString& pathOrUrl)
{
    QString normalizedPath = pathOrUrl.trimmed();
    if (normalizedPath.isEmpty()) {
        return {};
    }

    if (normalizedPath.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl fileUrl(normalizedPath);
        if (fileUrl.isValid() && fileUrl.isLocalFile()) {
            normalizedPath = fileUrl.toLocalFile();
        }
    }

    if (normalizedPath.contains(QLatin1Char('%'))) {
        normalizedPath = QUrl::fromPercentEncoding(normalizedPath.toUtf8());
    }

    return QDir::toNativeSeparators(normalizedPath);
}

QString sanitizedBaseName(QString baseName)
{
    baseName = baseName.trimmed();
    if (baseName.isEmpty()) {
        baseName = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    }

    static const QRegularExpression invalidCharacters(QStringLiteral(R"([<>:"/\\|?*])"));
    baseName.replace(invalidCharacters, QStringLiteral("_"));
    while (baseName.contains(QStringLiteral(".."))) {
        baseName.replace(QStringLiteral(".."), QStringLiteral("_"));
    }

    return baseName.trimmed().isEmpty()
        ? QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"))
        : baseName.trimmed();
}

QString nextAvailableBaseName(const QString& parentDirectoryPath, const QString& requestedBaseName)
{
    const QDir parentDirectory(parentDirectoryPath);
    const QString baseName = sanitizedBaseName(requestedBaseName);
    QString candidate = baseName;
    int suffix = 1;

    while (QFileInfo::exists(parentDirectory.filePath(candidate))) {
        candidate = QStringLiteral("%1_%2")
            .arg(baseName)
            .arg(suffix, 3, 10, QLatin1Char('0'));
        ++suffix;
    }

    return candidate;
}
} // namespace

JointExportManager::JointExportManager(
    WAVHandle* wavHandle,
    SignalDFTCalculation* signalDftCalculation,
    Multi_featureJointDetection* multiFeatureJointDetection,
    GenerateManager* generateManager,
    QObject* parent)
    : QObject(parent)
    , m_wavHandle(wavHandle)
    , m_signalDftCalculation(signalDftCalculation)
    , m_multiFeatureJointDetection(multiFeatureJointDetection)
    , m_generateManager(generateManager)
{
    if (m_wavHandle != nullptr) {
        connect(
            m_wavHandle,
            &WAVHandle::saveCompleted,
            this,
            &JointExportManager::handleRealtimeWavSaved);
        connect(
            m_wavHandle,
            &WAVHandle::saveFailed,
            this,
            &JointExportManager::handleRealtimeWavSaveFailed);
        connect(
            m_wavHandle,
            &WAVHandle::importDataReady,
            this,
            &JointExportManager::handleImportedDataReady);
        connect(
            m_wavHandle,
            &WAVHandle::importFailed,
            this,
            &JointExportManager::handleImportFailed);
    }

    if (m_signalDftCalculation != nullptr) {
        connect(
            m_signalDftCalculation,
            &SignalDFTCalculation::importedDftProcessingFinished,
            this,
            &JointExportManager::handleImportedDftFinished);
    }

    if (m_multiFeatureJointDetection != nullptr) {
        connect(
            m_multiFeatureJointDetection,
            &Multi_featureJointDetection::importedAnalysisCompleted,
            this,
            &JointExportManager::handleImportedAnalysisCompleted);
        connect(
            m_multiFeatureJointDetection,
            &Multi_featureJointDetection::importedAnalysisFailed,
            this,
            &JointExportManager::handleImportedAnalysisFailed);
    }
}

JointExportManager::~JointExportManager() = default;

bool JointExportManager::beginExport(const QString& parentDirectoryPath)
{
    if (m_wavHandle == nullptr ||
        m_signalDftCalculation == nullptr ||
        m_multiFeatureJointDetection == nullptr ||
        m_generateManager == nullptr) {
        fail(QStringLiteral("联合导出后台对象未初始化完整"));
        return false;
    }

    if (m_active || m_saving || m_finalExportRunning) {
        const QString errorMessage = QStringLiteral("当前已有联合导出任务正在进行");
        setStatusMessage(errorMessage);
        emit failed(errorMessage);
        return false;
    }

    resetTaskState();

    QString errorMessage;
    if (!prepareOutputPaths(parentDirectoryPath, &errorMessage)) {
        fail(errorMessage);
        return false;
    }

    setActive(true);
    setStatusMessage(QStringLiteral("正在保存音频并执行后处理..."));
    m_wavHandle->startSaveAsWav(m_outputDirectory, m_baseName);
    return true;
}

bool JointExportManager::saveWithMedicalRecord(const QVariantMap& formData)
{
    if (!m_active) {
        fail(QStringLiteral("当前没有正在进行的联合导出任务"));
        return false;
    }

    m_medicalRecordFormData = formData;
    setSaving(true);
    setStatusMessage(
        m_analysisReady
            ? QStringLiteral("正在生成分析报告与采集信息报告...")
            : QStringLiteral("后台分析仍在进行，请稍候..."));
    startFinalExportIfReady();
    return true;
}

void JointExportManager::handleRealtimeWavSaved(const QString& outputPath)
{
    if (!m_active || m_wavReady) {
        return;
    }

    m_wavReady = true;
    setWavFilePath(outputPath);
    setStatusMessage(QStringLiteral("音频保存完成，正在读取 WAV 进行分析..."));
    m_wavHandle->startReadFromWav(outputPath);
}

void JointExportManager::handleRealtimeWavSaveFailed(const QString& errorMessage)
{
    if (!m_active) {
        return;
    }

    fail(errorMessage.trimmed().isEmpty()
             ? QStringLiteral("联合导出音频保存失败")
             : QStringLiteral("联合导出音频保存失败：%1").arg(errorMessage));
}

void JointExportManager::handleImportedDataReady()
{
    if (!m_active || m_importReady) {
        return;
    }

    m_importReady = true;
    setStatusMessage(QStringLiteral("WAV 读取完成，正在计算频谱..."));
    m_signalDftCalculation->startImportedDftProcessing();
}

void JointExportManager::handleImportFailed(const QString& errorMessage)
{
    if (!m_active) {
        return;
    }

    fail(errorMessage.trimmed().isEmpty()
             ? QStringLiteral("联合导出 WAV 读取失败")
             : QStringLiteral("联合导出 WAV 读取失败：%1").arg(errorMessage));
}

void JointExportManager::handleImportedDftFinished()
{
    if (!m_active || m_dftReady) {
        return;
    }

    m_dftReady = true;
    setStatusMessage(QStringLiteral("频谱计算完成，正在进行特征与时间识别..."));
    m_multiFeatureJointDetection->startImportedAnalysis();
}

void JointExportManager::handleImportedAnalysisCompleted(const QVariantMap& featureValues)
{
    Q_UNUSED(featureValues)

    if (!m_active || m_analysisReady) {
        return;
    }

    m_analysisReady = true;
    setStatusMessage(
        m_saving
            ? QStringLiteral("后台分析完成，正在生成报告...")
            : QStringLiteral("后台分析完成，等待保存采集信息..."));
    startFinalExportIfReady();
}

void JointExportManager::handleImportedAnalysisFailed(const QString& errorMessage)
{
    if (!m_active) {
        return;
    }

    fail(errorMessage.trimmed().isEmpty()
             ? QStringLiteral("联合导出特征与时间识别失败")
             : QStringLiteral("联合导出特征与时间识别失败：%1").arg(errorMessage));
}

void JointExportManager::resetTaskState()
{
    m_wavReady = false;
    m_importReady = false;
    m_dftReady = false;
    m_analysisReady = false;
    m_finalExportRunning = false;
    m_medicalRecordFormData = {};
    setSaving(false);
    setActive(false);
    setStatusMessage({});
    setOutputDirectory({});
    setBaseName({});
    setWavFilePath({});
}

void JointExportManager::fail(const QString& errorMessage)
{
    const QString effectiveErrorMessage =
        errorMessage.trimmed().isEmpty()
            ? QStringLiteral("联合导出失败")
            : errorMessage.trimmed();

    m_wavReady = false;
    m_importReady = false;
    m_dftReady = false;
    m_analysisReady = false;
    m_finalExportRunning = false;
    setSaving(false);
    setActive(false);
    setStatusMessage(effectiveErrorMessage);
    emit failed(effectiveErrorMessage);
}

void JointExportManager::setActive(bool active)
{
    if (m_active == active) {
        return;
    }

    m_active = active;
    emit activeChanged();
}

void JointExportManager::setSaving(bool saving)
{
    if (m_saving == saving) {
        return;
    }

    m_saving = saving;
    emit savingChanged();
}

void JointExportManager::setStatusMessage(const QString& statusMessage)
{
    if (m_statusMessage == statusMessage) {
        return;
    }

    m_statusMessage = statusMessage;
    emit statusMessageChanged();
}

void JointExportManager::setOutputDirectory(const QString& outputDirectory)
{
    if (m_outputDirectory == outputDirectory) {
        return;
    }

    m_outputDirectory = outputDirectory;
    emit outputDirectoryChanged();
}

void JointExportManager::setBaseName(const QString& baseName)
{
    if (m_baseName == baseName) {
        return;
    }

    m_baseName = baseName;
    emit baseNameChanged();
}

void JointExportManager::setWavFilePath(const QString& wavFilePath)
{
    if (m_wavFilePath == wavFilePath) {
        return;
    }

    m_wavFilePath = wavFilePath;
    emit wavFilePathChanged();
}

bool JointExportManager::prepareOutputPaths(
    const QString& parentDirectoryPath,
    QString* errorMessage)
{
    const QString localParentPath = normalizeLocalDirectoryPath(parentDirectoryPath);
    if (localParentPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("请选择有效的联合导出目录");
        }
        return false;
    }

    QDir parentDirectory(localParentPath);
    if (!parentDirectory.exists() && !QDir().mkpath(localParentPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建联合导出父目录：%1").arg(localParentPath);
        }
        return false;
    }

    parentDirectory = QDir(localParentPath);
    if (!parentDirectory.exists()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("联合导出父目录不存在：%1").arg(localParentPath);
        }
        return false;
    }

    const QString initialBaseName =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    const QString baseName = nextAvailableBaseName(parentDirectory.absolutePath(), initialBaseName);
    const QString outputDirectoryPath = parentDirectory.filePath(baseName);

    if (!QDir().mkpath(outputDirectoryPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建联合导出目录：%1").arg(outputDirectoryPath);
        }
        return false;
    }

    setBaseName(baseName);
    setOutputDirectory(QDir::toNativeSeparators(QFileInfo(outputDirectoryPath).absoluteFilePath()));
    setWavFilePath(
        QDir::toNativeSeparators(QDir(m_outputDirectory).filePath(baseName + QStringLiteral(".wav"))));
    return true;
}

void JointExportManager::startFinalExportIfReady()
{
    if (!m_active || !m_saving || !m_analysisReady || m_finalExportRunning) {
        return;
    }

    m_finalExportRunning = true;
    setStatusMessage(QStringLiteral("正在生成分析报告与采集信息报告..."));

    const QVariantMap formData = m_medicalRecordFormData;
    const QString outputDirectory = m_outputDirectory;
    const QString baseName = m_baseName;
    const QString analysisPdfPath =
        QDir(outputDirectory).filePath(baseName + QStringLiteral("_分析报告.pdf"));
    const QString collectionPdfPath =
        QDir(outputDirectory).filePath(baseName + QStringLiteral("_采集信息报告.pdf"));

    QThread* exportThread = QThread::create(
        [this, formData, analysisPdfPath, collectionPdfPath]() {
            QString errorMessage;
            const QString analysisReportPath =
                m_generateManager->exportIdentificationAndFeatureExtractionReportTo(
                    analysisPdfPath,
                    &errorMessage);

            bool succeeded = !analysisReportPath.isEmpty();
            if (succeeded) {
                const QString collectionReportPath =
                    m_generateManager->exportCollectionInformationReport(
                        formData,
                        collectionPdfPath,
                        &errorMessage);
                succeeded = !collectionReportPath.isEmpty();
            }

            QMetaObject::invokeMethod(
                this,
                [this, succeeded, errorMessage]() {
                    handleFinalExportCompleted(succeeded, errorMessage);
                },
                Qt::QueuedConnection);
        });

    connect(exportThread, &QThread::finished, exportThread, &QObject::deleteLater);
    exportThread->start();
}

void JointExportManager::handleFinalExportCompleted(bool succeeded, const QString& errorMessage)
{
    if (!m_active) {
        return;
    }

    m_finalExportRunning = false;
    if (!succeeded) {
        fail(errorMessage.trimmed().isEmpty()
                 ? QStringLiteral("联合导出报告生成失败")
                 : QStringLiteral("联合导出报告生成失败：%1").arg(errorMessage));
        return;
    }

    const QString completedDirectory = m_outputDirectory;
    setSaving(false);
    setActive(false);
    setStatusMessage(QStringLiteral("联合导出完成"));
    emit completed(completedDirectory);
}
