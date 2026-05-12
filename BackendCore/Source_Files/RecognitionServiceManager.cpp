/**
 * @file RecognitionServiceManager.cpp
 * @brief 远程肠鸣音识别服务管理模块，负责HTTP multipart文件上传、API鉴权、取消请求、JSON响应解析与进度通知。
 */

#include "RecognitionServiceManager.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {

constexpr char kApiKeyHeaderName[] = "X-API-Key";
QString trimmedOrEmpty(const QString& value)
{
    return value.trimmed();
}

QString describeHttpStatus(QNetworkReply* reply)
{
    if (reply == nullptr) {
        return {};
    }

    const QVariant statusCode =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusCode.isValid()) {
        return {};
    }

    return QString::number(statusCode.toInt());
}

} // namespace

RecognitionServiceManager::RecognitionServiceManager(QObject* parent)
    : QObject(parent)
    , m_networkAccessManager(new QNetworkAccessManager(this))
{
}

RecognitionServiceManager::~RecognitionServiceManager()
{
    if (m_reply != nullptr) {
        m_reply->abort();
    }
}

bool RecognitionServiceManager::busy() const
{
    return m_busy;
}

bool RecognitionServiceManager::hasResult() const
{
    return !m_result.isEmpty();
}

QString RecognitionServiceManager::statusMessage() const
{
    return m_statusMessage;
}

QString RecognitionServiceManager::errorMessage() const
{
    return m_errorMessage;
}

qint64 RecognitionServiceManager::uploadedBytes() const
{
    return m_uploadedBytes;
}

qint64 RecognitionServiceManager::totalUploadBytes() const
{
    return m_totalUploadBytes;
}

qreal RecognitionServiceManager::uploadProgress() const
{
    if (m_totalUploadBytes <= 0) {
        return -1.0;
    }

    return static_cast<qreal>(m_uploadedBytes) /
        static_cast<qreal>(m_totalUploadBytes);
}

QVariantMap RecognitionServiceManager::result() const
{
    return m_result;
}

QString RecognitionServiceManager::rawResponseJson() const
{
    return m_rawResponseJson;
}

QString RecognitionServiceManager::lastRequestId() const
{
    return m_lastRequestId;
}

/**
 * @brief 通过HTTP multipart向远程服务上传WAV文件并请求肠鸣音识别。
 * @param filePath 本地WAV文件路径。
 * @param endpointUrl 识别服务URL。
 * @param apiKey API鉴权密钥。
 * @param verbose 是否输出详细信息。
 * @param includeProbs 是否包含概率值。
 */
void RecognitionServiceManager::recognizeFile(
    const QString& filePath,
    const QString& endpointUrl,
    const QString& apiKey,
    bool verbose,
    bool includeProbs)
{
    if (m_busy) {
        finishWithError(QStringLiteral("当前正在进行识别，请稍后再试。"));
        return;
    }

    m_cancelRequested = false;
    setRawResponseJson(QString());
    setLastRequestId(QString());
    setUploadProgress(0, 0);

    QString normalizedFilePath;
    QUrl normalizedUrl;
    QString validationError;
    if (!validateRequest(
            filePath,
            endpointUrl,
            apiKey,
            &normalizedFilePath,
            &normalizedUrl,
            &validationError)) {
        finishWithError(validationError);
        return;
    }

    setBusy(true);
    setErrorMessage(QString());
    setStatusMessage(QStringLiteral("正在上传音频并请求识别..."));

    QUrlQuery query(normalizedUrl);
    query.addQueryItem(QStringLiteral("verbose"), verbose ? QStringLiteral("true") : QStringLiteral("false"));
    query.addQueryItem(
        QStringLiteral("include_probs"),
        (verbose && includeProbs) ? QStringLiteral("true") : QStringLiteral("false"));
    normalizedUrl.setQuery(query);

    auto* uploadFile = new QFile(normalizedFilePath);
    if (!uploadFile->open(QIODevice::ReadOnly)) {
        const QString errorMessage =
            QStringLiteral("无法读取音频文件: %1")
                .arg(uploadFile->errorString());
        uploadFile->deleteLater();
        finishWithError(errorMessage);
        return;
    }

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart filePart;
    filePart.setHeader(
        QNetworkRequest::ContentDispositionHeader,
        QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"")
                     .arg(QFileInfo(*uploadFile).fileName())));
    filePart.setHeader(
        QNetworkRequest::ContentTypeHeader,
        QVariant(QStringLiteral("audio/wav")));
    uploadFile->setParent(multiPart);
    filePart.setBodyDevice(uploadFile);
    multiPart->append(filePart);

    QNetworkRequest request(normalizedUrl);
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("%1/%2 recognition")
            .arg(
                QCoreApplication::applicationName(),
                QCoreApplication::applicationVersion()));
    request.setRawHeader(kApiKeyHeaderName, trimmedOrEmpty(apiKey).toUtf8());

    m_reply = m_networkAccessManager->post(request, multiPart);
    multiPart->setParent(m_reply);

    connect(
        m_reply,
        &QNetworkReply::uploadProgress,
        this,
        [this](qint64 bytesSent, qint64 bytesTotal) {
            setUploadProgress(bytesSent, bytesTotal);
            if (bytesTotal > 0 && bytesSent >= bytesTotal) {
                setStatusMessage(QStringLiteral("音频已上传，正在等待识别结果..."));
            }
        });

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* const reply = m_reply;
        m_reply = nullptr;

        if (reply == nullptr) {
            setBusy(false);
            setUploadProgress(0, 0);
            return;
        }

        const QByteArray payload = reply->readAll();
        const QString rawPayload = QString::fromUtf8(payload);
        setRawResponseJson(rawPayload);

        const ParsedResponse parsedResponse = parseResponsePayload(payload);
        setLastRequestId(parsedResponse.requestId);

        const bool canceledByUser =
            m_cancelRequested &&
            reply->error() == QNetworkReply::OperationCanceledError;
        m_cancelRequested = false;

        if (canceledByUser) {
            setBusy(false);
            setUploadProgress(0, 0);
            setErrorMessage(QString());
            setStatusMessage(QStringLiteral("已退出当前识别。"));
            emit recognitionCanceled();
            reply->deleteLater();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            const QString errorMessage = buildHttpErrorMessage(reply, parsedResponse);
            reply->deleteLater();
            finishWithError(errorMessage);
            return;
        }

        if (!parsedResponse.jsonParsed) {
            reply->deleteLater();
            finishWithError(QStringLiteral("识别服务返回了无法解析的 JSON 响应。"));
            return;
        }

        if (!parsedResponse.hasCode) {
            reply->deleteLater();
            finishWithError(QStringLiteral("识别服务响应缺少 code 字段。"));
            return;
        }

        if (parsedResponse.code != 0) {
            const QString errorMessage =
                parsedResponse.message.isEmpty()
                    ? QStringLiteral("识别服务返回了错误。")
                    : parsedResponse.message;
            reply->deleteLater();
            finishWithError(errorMessage);
            return;
        }

        setResult(parsedResponse.dataObject);
        setErrorMessage(QString());
        setStatusMessage(QStringLiteral("识别完成。"));
        setBusy(false);
        setUploadProgress(0, 0);
        emit recognitionSucceeded();
        reply->deleteLater();
    });
}

/** @brief 取消当前正在进行的识别请求。 */
void RecognitionServiceManager::cancelRecognition()
{
    if (!m_busy || m_reply == nullptr || m_cancelRequested) {
        return;
    }

    m_cancelRequested = true;
    setStatusMessage(QStringLiteral("正在退出当前识别..."));
    m_reply->abort();
}

void RecognitionServiceManager::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void RecognitionServiceManager::setStatusMessage(const QString& statusMessage)
{
    if (m_statusMessage == statusMessage) {
        return;
    }

    m_statusMessage = statusMessage;
    emit statusMessageChanged();
}

void RecognitionServiceManager::setErrorMessage(const QString& errorMessage)
{
    if (m_errorMessage == errorMessage) {
        return;
    }

    m_errorMessage = errorMessage;
    emit errorMessageChanged();
}

void RecognitionServiceManager::setUploadProgress(
    qint64 uploadedBytes,
    qint64 totalUploadBytes)
{
    const qint64 nextUploadedBytes = uploadedBytes >= 0 ? uploadedBytes : 0;
    const qint64 nextTotalUploadBytes = totalUploadBytes > 0 ? totalUploadBytes : 0;
    if (m_uploadedBytes == nextUploadedBytes &&
        m_totalUploadBytes == nextTotalUploadBytes) {
        return;
    }

    m_uploadedBytes = nextUploadedBytes;
    m_totalUploadBytes = nextTotalUploadBytes;
    emit uploadProgressChanged();
}

void RecognitionServiceManager::setResult(const QVariantMap& result)
{
    if (m_result == result) {
        return;
    }

    m_result = result;
    emit resultChanged();
}

void RecognitionServiceManager::setRawResponseJson(const QString& rawResponseJson)
{
    if (m_rawResponseJson == rawResponseJson) {
        return;
    }

    m_rawResponseJson = rawResponseJson;
    emit rawResponseJsonChanged();
}

void RecognitionServiceManager::setLastRequestId(const QString& lastRequestId)
{
    if (m_lastRequestId == lastRequestId) {
        return;
    }

    m_lastRequestId = lastRequestId;
    emit lastRequestIdChanged();
}

/** @brief 统一错误出口：重置busy状态并发送错误信号。 */
void RecognitionServiceManager::finishWithError(const QString& message)
{
    setBusy(false);
    setUploadProgress(0, 0);
    setErrorMessage(message);
    setStatusMessage(message);
    emit recognitionFailed(message);
}

bool RecognitionServiceManager::validateRequest(
    const QString& filePath,
    const QString& endpointUrl,
    const QString& apiKey,
    QString* normalizedFilePath,
    QUrl* normalizedUrl,
    QString* errorMessage) const
{
    const QString trimmedFilePath = trimmedOrEmpty(filePath);
    if (trimmedFilePath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("请先选择要识别的 WAV 文件。");
        }
        return false;
    }

    const QFileInfo fileInfo(trimmedFilePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("找不到选择的音频文件。");
        }
        return false;
    }

    if (fileInfo.suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) != 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前仅支持 .wav 音频文件。");
        }
        return false;
    }

    const QString trimmedEndpointUrl = trimmedOrEmpty(endpointUrl);
    if (trimmedEndpointUrl.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("请先配置识别服务 URL。");
        }
        return false;
    }

    const QUrl parsedUrl(trimmedEndpointUrl);
    if (!parsedUrl.isValid() ||
        (parsedUrl.scheme() != QStringLiteral("http") &&
         parsedUrl.scheme() != QStringLiteral("https")) ||
        parsedUrl.host().trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("识别服务 URL 格式无效，请使用 http 或 https 地址。");
        }
        return false;
    }

    if (trimmedOrEmpty(apiKey).isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("请先配置 API Key。");
        }
        return false;
    }

    if (normalizedFilePath != nullptr) {
        *normalizedFilePath = fileInfo.absoluteFilePath();
    }

    if (normalizedUrl != nullptr) {
        *normalizedUrl = parsedUrl;
    }

    return true;
}

RecognitionServiceManager::ParsedResponse RecognitionServiceManager::parseResponsePayload(
    const QByteArray& payload) const
{
    ParsedResponse parsedResponse;
    if (payload.isEmpty()) {
        return parsedResponse;
    }

    QJsonParseError jsonParseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &jsonParseError);
    if (jsonParseError.error != QJsonParseError::NoError || !document.isObject()) {
        return parsedResponse;
    }

    parsedResponse.jsonParsed = true;
    parsedResponse.rootObject = document.object().toVariantMap();
    parsedResponse.message =
        parsedResponse.rootObject.value(QStringLiteral("message")).toString().trimmed();
    parsedResponse.requestId =
        parsedResponse.rootObject.value(QStringLiteral("request_id")).toString().trimmed();

    const QVariant codeValue = parsedResponse.rootObject.value(QStringLiteral("code"));
    if (codeValue.isValid()) {
        parsedResponse.hasCode = true;
        parsedResponse.code = codeValue.toInt();
    }

    const QVariant dataValue = parsedResponse.rootObject.value(QStringLiteral("data"));
    if (dataValue.metaType().id() == QMetaType::QVariantMap) {
        parsedResponse.dataObject = dataValue.toMap();
    }

    return parsedResponse;
}

QString RecognitionServiceManager::buildHttpErrorMessage(
    QNetworkReply* reply,
    const ParsedResponse& parsedResponse) const
{
    QString detail = parsedResponse.message;
    if (detail.isEmpty() && reply != nullptr) {
        detail = reply->errorString().trimmed();
    }
    if (detail.isEmpty()) {
        detail = QStringLiteral("未知错误");
    }

    const QString statusCode = describeHttpStatus(reply);
    if (!statusCode.isEmpty()) {
        return QStringLiteral("识别服务请求失败 (HTTP %1): %2")
            .arg(statusCode, detail);
    }

    return QStringLiteral("识别服务请求失败: %1").arg(detail);
}
