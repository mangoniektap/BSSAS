#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include <QObject>
#include <QString>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

class UpdateManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(bool mandatoryUpdate READ mandatoryUpdate NOTIFY updateInfoChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateInfoChanged)
    Q_PROPERTY(QString installerUrl READ installerUrl NOTIFY updateInfoChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY updateInfoChanged)
    Q_PROPERTY(QString minSupportedVersion READ minSupportedVersion NOTIFY updateInfoChanged)
    Q_PROPERTY(QString publishTimeDisplay READ publishTimeDisplay NOTIFY updateInfoChanged)
    Q_PROPERTY(qint64 fileSize READ fileSize NOTIFY updateInfoChanged)
    Q_PROPERTY(qint64 downloadedBytes READ downloadedBytes NOTIFY downloadProgressChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY downloadProgressChanged)
    Q_PROPERTY(qreal downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
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

    Q_INVOKABLE void checkForUpdates(bool manual = true);
    Q_INVOKABLE void checkForUpdatesOnStartup();
    Q_INVOKABLE void startUpdate();

signals:
    void checkingChanged();
    void downloadingChanged();
    void busyChanged();
    void updateAvailableChanged();
    void updateInfoChanged();
    void downloadProgressChanged();
    void statusMessageChanged();
    void promptUpdateDialog();
    void toastRequested(const QString& message, bool error);

private:
    void setChecking(bool checking);
    void setDownloading(bool downloading);
    void setUpdateAvailable(bool updateAvailable);
    void setMandatoryUpdate(bool mandatoryUpdate);
    void setStatusMessage(const QString& statusMessage);
    void updateManifestMetadata(
        const QString& latestVersion,
        const QString& installerUrl,
        const QString& installerSha256,
        const QString& releaseNotes,
        const QString& minSupportedVersion,
        const QString& publishTimeDisplay,
        qint64 fileSize);
    void setDownloadProgress(qint64 downloadedBytes, qint64 totalBytes);

    void onManifestReplyFinished();
    void onDownloadReadyRead();
    void onDownloadProgress(qint64 receivedBytes, qint64 totalBytes);
    void onDownloadFinished();

    QString buildDownloadFilePath() const;
    QString buildTemporaryUpdaterFilePath() const;
    bool ensureTemporaryDirectory(QString* errorMessage) const;
    bool prepareUpdaterForLaunch(QString* updaterPath, QString* errorMessage) const;
    bool launchUpdater(QString* errorMessage) const;
    void finalizeFailedDownload();
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
