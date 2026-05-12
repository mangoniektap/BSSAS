/** @file UpdateManager.h
 *  @brief 软件更新管理模块，支持版本检测、升级包下载及静默安装。
 */

#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include <QObject>
#include <QString>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

/** @brief 软件更新管理器，封装更新检查、下载与启动安装程序的全流程。 */
class UpdateManager : public QObject
{
    Q_OBJECT
    /** @brief 当前软件版本号 */
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    /** @brief 是否正在检查更新 */
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    /** @brief 是否正在下载 */
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged)
    /** @brief 是否忙碌（检查或下载中） */
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /** @brief 是否存在可用更新 */
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)
    /** @brief 是否为强制更新 */
    Q_PROPERTY(bool mandatoryUpdate READ mandatoryUpdate NOTIFY updateInfoChanged)
    /** @brief 最新版本号 */
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateInfoChanged)
    /** @brief 安装包下载 URL */
    Q_PROPERTY(QString installerUrl READ installerUrl NOTIFY updateInfoChanged)
    /** @brief 版本更新日志 */
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY updateInfoChanged)
    /** @brief 最低兼容版本 */
    Q_PROPERTY(QString minSupportedVersion READ minSupportedVersion NOTIFY updateInfoChanged)
    /** @brief 发布时间显示文字 */
    Q_PROPERTY(QString publishTimeDisplay READ publishTimeDisplay NOTIFY updateInfoChanged)
    /** @brief 安装包文件大小 (字节) */
    Q_PROPERTY(qint64 fileSize READ fileSize NOTIFY updateInfoChanged)
    /** @brief 已下载字节数 */
    Q_PROPERTY(qint64 downloadedBytes READ downloadedBytes NOTIFY downloadProgressChanged)
    /** @brief 待下载总字节数 */
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY downloadProgressChanged)
    /** @brief 下载进度 (0.0 ~ 1.0) */
    Q_PROPERTY(qreal downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    /** @brief 当前状态消息 */
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit UpdateManager(QObject* parent = nullptr);
    ~UpdateManager() override;

    QString currentVersion() const;
    bool checking() const;
    bool downloading() const;
    bool busy() const;
    bool updateAvailable() const;
    bool mandatoryUpdate() const;
    QString latestVersion() const;
    QString installerUrl() const;
    QString releaseNotes() const;
    QString minSupportedVersion() const;
    QString publishTimeDisplay() const;
    qint64 fileSize() const;
    qint64 downloadedBytes() const;
    qint64 totalBytes() const;
    qreal downloadProgress() const;
    QString statusMessage() const;

    /**
     * @brief 检查是否有可用更新。
     * @param manual 是否由用户手动触发，true 时即使有频控限制也会检查
     */
    Q_INVOKABLE void checkForUpdates(bool manual = true);

    /** @brief 启动时静默检查更新（受频控限制） */
    Q_INVOKABLE void checkForUpdatesOnStartup();

    /** @brief 开始下载并安装更新 */
    Q_INVOKABLE void startUpdate();

signals:
    /** @brief 检查状态变化信号 */
    void checkingChanged();
    /** @brief 下载状态变化信号 */
    void downloadingChanged();
    /** @brief 忙碌状态变化信号 */
    void busyChanged();
    /** @brief 更新可用状态变化信号 */
    void updateAvailableChanged();
    /** @brief 更新信息变化信号 */
    void updateInfoChanged();
    /** @brief 下载进度变化信号 */
    void downloadProgressChanged();
    /** @brief 状态消息变化信号 */
    void statusMessageChanged();
    /** @brief 弹出更新对话框信号 */
    void promptUpdateDialog();
    /** @brief 弹出 Toast 提示信号 @param message 提示消息 @param error 是否为错误 */
    void toastRequested(const QString& message, bool error);

private:
    /** @brief 设置检查状态 @param checking 是否检查中 */
    void setChecking(bool checking);
    /** @brief 设置下载状态 @param downloading 是否下载中 */
    void setDownloading(bool downloading);
    /** @brief 设置更新可用 @param updateAvailable 是否有可用更新 */
    void setUpdateAvailable(bool updateAvailable);
    /** @brief 设置强制更新 @param mandatoryUpdate 是否强制 */
    void setMandatoryUpdate(bool mandatoryUpdate);
    /** @brief 设置状态消息 @param statusMessage 状态文字 */
    void setStatusMessage(const QString& statusMessage);
    /**
     * @brief 更新版本清单元数据。
     * @param latestVersion      最新版本号
     * @param installerUrl       安装包 URL
     * @param installerSha256    安装包 SHA256
     * @param releaseNotes       更新日志
     * @param minSupportedVersion 最低兼容版本
     * @param publishTimeDisplay 发布时间显示
     * @param fileSize           文件大小 (字节)
     */
    void updateManifestMetadata(
        const QString& latestVersion,
        const QString& installerUrl,
        const QString& installerSha256,
        const QString& releaseNotes,
        const QString& minSupportedVersion,
        const QString& publishTimeDisplay,
        qint64 fileSize);
    /** @brief 设置下载进度 @param downloadedBytes 已下载 @param totalBytes 总计 */
    void setDownloadProgress(qint64 downloadedBytes, qint64 totalBytes);

    /** @brief 版本清单网络请求响应回调 */
    void onManifestReplyFinished();
    /** @brief 下载数据就绪回调 */
    void onDownloadReadyRead();
    /** @brief 下载进度回调 @param receivedBytes 已接收 @param totalBytes 总计 */
    void onDownloadProgress(qint64 receivedBytes, qint64 totalBytes);
    /** @brief 下载完成回调 */
    void onDownloadFinished();

    /** @brief 构建下载文件路径 @returns 完整路径 */
    QString buildDownloadFilePath() const;
    /** @brief 构建临时更新程序路径 @returns 完整路径 */
    QString buildTemporaryUpdaterFilePath() const;
    /** @brief 确保临时目录存在 @param errorMessage 输出: 错误消息 @returns 是否成功 */
    bool ensureTemporaryDirectory(QString* errorMessage) const;
    /** @brief 准备更新程序以便启动 @param updaterPath 输出: 更新程序路径 @param errorMessage 输出: 错误消息 @returns 是否成功 */
    bool prepareUpdaterForLaunch(QString* updaterPath, QString* errorMessage) const;
    /** @brief 启动更新程序 @param errorMessage 输出: 错误消息 @returns 是否成功 */
    bool launchUpdater(QString* errorMessage) const;
    /** @brief 清理下载失败后的临时文件 */
    void finalizeFailedDownload();
    /** @brief 通知检查错误 @param message 错误消息 */
    void notifyCheckError(const QString& message);

    QNetworkAccessManager* m_networkManager = nullptr;
    QNetworkReply* m_manifestReply = nullptr;
    QNetworkReply* m_downloadReply = nullptr;
    std::unique_ptr<QSaveFile> m_downloadFile;
    QString m_downloadTargetFilePath;
    QString m_downloadAbortReason;
    bool m_lastCheckWasManual = false;
    bool m_checking = false;
    bool m_downloading = false;
    bool m_updateAvailable = false;
    bool m_mandatoryUpdate = false;
    QString m_latestVersion;
    QString m_installerUrl;
    QString m_installerSha256;
    QString m_releaseNotes;
    QString m_minSupportedVersion;
    QString m_publishTimeDisplay;
    qint64 m_fileSize = 0;
    qint64 m_downloadedBytes = 0;
    qint64 m_totalBytes = 0;
    QString m_statusMessage;
};

#endif // UPDATEMANAGER_H
