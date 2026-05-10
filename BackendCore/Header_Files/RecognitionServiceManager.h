#ifndef RECOGNITIONSERVICEMANAGER_H
#define RECOGNITIONSERVICEMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantMap>

class QByteArray;
class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

class RecognitionServiceManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(qint64 uploadedBytes READ uploadedBytes NOTIFY uploadProgressChanged)
    Q_PROPERTY(qint64 totalUploadBytes READ totalUploadBytes NOTIFY uploadProgressChanged)
    Q_PROPERTY(qreal uploadProgress READ uploadProgress NOTIFY uploadProgressChanged)
    Q_PROPERTY(QVariantMap result READ result NOTIFY resultChanged)
    Q_PROPERTY(QString rawResponseJson READ rawResponseJson NOTIFY rawResponseJsonChanged)
    Q_PROPERTY(QString lastRequestId READ lastRequestId NOTIFY lastRequestIdChanged)

public:
    explicit RecognitionServiceManager(QObject* parent = nullptr);
    ~RecognitionServiceManager() override;

    bool busy() const;
    bool hasResult() const;
    QString statusMessage() const;
    QString errorMessage() const;
    qint64 uploadedBytes() const;
    qint64 totalUploadBytes() const;
    qreal uploadProgress() const;
    QVariantMap result() const;
    QString rawResponseJson() const;
    QString lastRequestId() const;

    Q_INVOKABLE void recognizeFile(
        const QString& filePath,
        const QString& endpointUrl,
        const QString& apiKey,
        bool verbose,
        bool includeProbs);
    Q_INVOKABLE void cancelRecognition();

signals:
    void busyChanged();
    void resultChanged();
    void statusMessageChanged();
    void errorMessageChanged();
    void uploadProgressChanged();
    void rawResponseJsonChanged();
    void lastRequestIdChanged();
    void recognitionSucceeded();
    void recognitionFailed(const QString& message);
    void recognitionCanceled();

private:
    struct ParsedResponse
    {
        bool jsonParsed = false;
        bool hasCode = false;
        int code = 0;
        QString message;
        QString requestId;
        QVariantMap rootObject;
        QVariantMap dataObject;
    };

    void setBusy(bool busy);
    void setStatusMessage(const QString& statusMessage);
    void setErrorMessage(const QString& errorMessage);
    void setUploadProgress(qint64 uploadedBytes, qint64 totalUploadBytes);
    void setResult(const QVariantMap& result);
    void setRawResponseJson(const QString& rawResponseJson);
    void setLastRequestId(const QString& lastRequestId);
    void finishWithError(const QString& message);
    bool validateRequest(
        const QString& filePath,
        const QString& endpointUrl,
        const QString& apiKey,
        QString* normalizedFilePath,
        QUrl* normalizedUrl,
        QString* errorMessage) const;
    ParsedResponse parseResponsePayload(const QByteArray& payload) const;
    QString buildHttpErrorMessage(
        QNetworkReply* reply,
        const ParsedResponse& parsedResponse) const;

    QNetworkAccessManager* m_networkAccessManager = nullptr;
    QNetworkReply* m_reply = nullptr;
    bool m_busy = false;
    bool m_cancelRequested = false;
    qint64 m_uploadedBytes = 0;
    qint64 m_totalUploadBytes = 0;
    QVariantMap m_result;
    QString m_statusMessage;
    QString m_errorMessage;
    QString m_rawResponseJson;
    QString m_lastRequestId;
};

#endif // RECOGNITIONSERVICEMANAGER_H
