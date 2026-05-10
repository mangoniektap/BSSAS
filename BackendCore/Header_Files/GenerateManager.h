#ifndef GENERATEMANAGER_H
#define GENERATEMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <memory>

class PDFExport;
class QThread;

class GenerateManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit GenerateManager(QObject* parent = nullptr);
    ~GenerateManager() override;

    bool busy() const;

    Q_INVOKABLE QString exportIdentificationAndFeatureExtractionReport();
    Q_INVOKABLE void startExportIdentificationAndFeatureExtractionReport();
    static bool persistImportedAnalysisTemporaryJson(
        const QVariantMap& featureValues,
        QString* errorMessage = nullptr);

signals:
    void busyChanged();
    void exportCompleted(const QString& pdfFilePath);
    void exportFailed(const QString& errorMessage);

private:
    void setBusy(bool busy);
    static bool ensureOutputDirectoryExists();
    static QString createReportId(const QString& reportKind, const QDate& reportDate);
    static QString createOutputFilePath(const QString& reportId, const QString& extension);
    static QVariantMap buildReportPayload(
        const QVariantMap& featureValues,
        const QString& reportId,
        const QString& reportKind,
        const QDateTime& generatedAt);

    bool m_busy = false;
    QThread* m_exportThread = nullptr;
    std::unique_ptr<PDFExport> m_pdfExport;
};

#endif // GENERATEMANAGER_H
