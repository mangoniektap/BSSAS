/** @file RecognitionServiceManager.h
 *  @brief 识别服务管理模块，负责将音频文件上传至远程识别服务并解析返回结果。
 */

#ifndef RECOGNITIONSERVICEMANAGER_H
#define RECOGNITIONSERVICEMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantMap>

class QByteArray;
class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

/** @brief 远程识别服务管理器，封装 HTTP 上传与响应解析逻辑。 */
class RecognitionServiceManager : public QObject
{
    Q_OBJECT
    /** @brief 是否正在忙碌 */
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /** @brief 是否有识别结果 */
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultChanged)
    /** @brief 当前状态消息 */
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    /** @brief 错误消息 */
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    /** @brief 已上传字节数 */
    Q_PROPERTY(qint64 uploadedBytes READ uploadedBytes NOTIFY uploadProgressChanged)
    /** @brief 待上传总字节数 */
    Q_PROPERTY(qint64 totalUploadBytes READ totalUploadBytes NOTIFY uploadProgressChanged)
    /** @brief 上传进度 (0.0 ~ 1.0) */
    Q_PROPERTY(qreal uploadProgress READ uploadProgress NOTIFY uploadProgressChanged)
    /** @brief 识别结果 */
    Q_PROPERTY(QVariantMap result READ result NOTIFY resultChanged)
    /** @brief 服务端原始 JSON 响应 */
    Q_PROPERTY(QString rawResponseJson READ rawResponseJson NOTIFY rawResponseJsonChanged)
    /** @brief 最近一次请求 ID */
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

    /**
     * @brief 发起远程识别请求，上传音频文件到指定端点。
     * @param filePath     本地音频文件路径
     * @param endpointUrl  识别服务 URL
     * @param apiKey       API 密钥
     * @param verbose      是否返回详细信息
     * @param includeProbs 是否包含概率值
     */
    Q_INVOKABLE void recognizeFile(
        const QString& filePath,
        const QString& endpointUrl,
        const QString& apiKey,
        bool verbose,
        bool includeProbs);

    /** @brief 取消正在进行的识别请求 */
    Q_INVOKABLE void cancelRecognition();

    /**
     * @brief 将最近一次识别服务返回的 JSON 保存到指定文件。
     * @param filePath 输出 JSON 文件路径
     * @returns 保存失败时的错误消息；成功时返回空字符串。
     */
    Q_INVOKABLE QString saveRecognitionResultJson(const QString& filePath) const;

signals:
    /** @brief 忙碌状态变化信号 */
    void busyChanged();
    /** @brief 识别结果变化信号 */
    void resultChanged();
    /** @brief 状态消息变化信号 */
    void statusMessageChanged();
    /** @brief 错误消息变化信号 */
    void errorMessageChanged();
    /** @brief 上传进度变化信号 */
    void uploadProgressChanged();
    /** @brief 原始 JSON 响应变化信号 */
    void rawResponseJsonChanged();
    /** @brief 请求 ID 变化信号 */
    void lastRequestIdChanged();
    /** @brief 识别成功信号 */
    void recognitionSucceeded();
    /** @brief 识别失败信号 @param message 错误消息 */
    void recognitionFailed(const QString& message);
    /** @brief 识别已取消信号 */
    void recognitionCanceled();

private:
    /** @brief 解析后的 HTTP 响应结构体 */
    struct ParsedResponse
    {
        bool jsonParsed = false;       /**< JSON 是否解析成功 */
        bool hasCode = false;          /**< 是否包含状态码 */
        int code = 0;                  /**< 业务状态码 */
        QString message;               /**< 业务消息 */
        QString requestId;             /**< 请求 ID */
        QVariantMap rootObject;        /**< 根 JSON 对象 */
        QVariantMap dataObject;        /**< data 子对象 */
    };

    /** @brief 设置忙碌状态 @param busy 是否忙碌 */
    void setBusy(bool busy);
    /** @brief 设置状态消息 @param statusMessage 状态文字 */
    void setStatusMessage(const QString& statusMessage);
    /** @brief 设置错误消息 @param errorMessage 错误文字 */
    void setErrorMessage(const QString& errorMessage);
    /** @brief 设置上传进度 @param uploadedBytes 已上传字节 @param totalUploadBytes 总字节 */
    void setUploadProgress(qint64 uploadedBytes, qint64 totalUploadBytes);
    /** @brief 设置识别结果 @param result 结果映射 */
    void setResult(const QVariantMap& result);
    /** @brief 设置原始 JSON 响应 @param rawResponseJson JSON 字符串 */
    void setRawResponseJson(const QString& rawResponseJson);
    /** @brief 设置最近请求 ID @param lastRequestId 请求 ID */
    void setLastRequestId(const QString& lastRequestId);
    /** @brief 以错误结束当前请求 @param message 错误消息 */
    void finishWithError(const QString& message);
    /**
     * @brief 校验请求参数。
     * @param filePath          输入文件路径
     * @param endpointUrl       端点 URL
     * @param apiKey            API 密钥
     * @param normalizedFilePath 输出: 规范化文件路径
     * @param normalizedUrl     输出: 规范化 URL
     * @param errorMessage      输出: 错误消息
     * @returns 校验是否通过
     */
    bool validateRequest(
        const QString& filePath,
        const QString& endpointUrl,
        const QString& apiKey,
        QString* normalizedFilePath,
        QUrl* normalizedUrl,
        QString* errorMessage) const;
    /** @brief 解析 HTTP 响应载荷 @param payload 原始字节 @returns 解析结果 */
    ParsedResponse parseResponsePayload(const QByteArray& payload) const;
    /**
     * @brief 构建 HTTP 错误消息。
     * @param reply          网络回复对象
     * @param parsedResponse 解析后的响应
     * @returns 错误描述字符串
     */
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
