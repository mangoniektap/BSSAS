/**
 * @file UpdateManager.cpp
 * @brief 软件自动更新管理模块，负责从远程清单检查版本、下载安装包、SHA256校验完整性，并启动独立更新器进程完成主程序替换。
 */

#include "UpdateManager.h"

#include "DatabaseStoragePaths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <array>

namespace {

struct UpdateManifest
{
    QString latestVersion;
    QString installerUrl;
    QString installerSha256;
    qint64 fileSize = 0;
    QString releaseNotes;
    bool forceUpdate = false;
    QString minSupportedVersion;
    QString publishTime;
};

QString defaultManifestUrl()
{
#ifdef BSSAS_UPDATE_MANIFEST_URL
    return QStringLiteral(BSSAS_UPDATE_MANIFEST_URL);
#else
    return QStringLiteral("http://106.12.144.22:8081/bssas/version.json");
#endif
}

QString resolveManifestUrl()
{
    const QString configuredUrl =
        qEnvironmentVariable("BSSAS_UPDATE_MANIFEST_URL").trimmed();
    return configuredUrl.isEmpty() ? defaultManifestUrl() : configuredUrl;
}

QString normalizeSha256(const QString& sha256)
{
    QString normalized = sha256.trimmed().toUpper();
    normalized.remove(QRegularExpression(QStringLiteral("[^0-9A-F]")));
    return normalized;
}

QString formatPublishTime(const QString& rawPublishTime)
{
    const QDateTime publishTime =
        QDateTime::fromString(rawPublishTime, Qt::ISODate);
    if (!publishTime.isValid()) {
        return rawPublishTime.trimmed();
    }

    return publishTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
}

QString buildReplyErrorMessage(const QString& prefix, QNetworkReply* reply)
{
    if (reply == nullptr) {
        return prefix;
    }

    QString detail = reply->errorString().trimmed();
    if (reply->error() == QNetworkReply::RemoteHostClosedError) {
        detail = QStringLiteral("远程主机主动关闭了连接");
    } else if (detail.isEmpty()) {
        detail = QStringLiteral("未知网络错误");
    }

    QStringList metadata;
    metadata.append(
        QStringLiteral("Qt错误码=%1").arg(static_cast<int>(reply->error())));

    const QVariant statusCode =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusCode.isValid()) {
        metadata.append(QStringLiteral("HTTP状态=%1").arg(statusCode.toInt()));
    }

    const QString replyUrl = reply->url().toString();
    if (!replyUrl.isEmpty()) {
        metadata.append(QStringLiteral("URL=%1").arg(replyUrl));
    }

    return QStringLiteral("%1：%2（%3）")
        .arg(prefix, detail, metadata.join(QStringLiteral("，")));
}

bool tryParseThreePartVersion(const QString& versionText, std::array<int, 3>* versionParts)
{
    if (versionParts == nullptr) {
        return false;
    }

    const QStringList parts = versionText.trimmed().split('.', Qt::SkipEmptyParts);
    if (parts.size() < 3) {
        return false;
    }

    std::array<int, 3> parsedParts { 0, 0, 0 };
    for (int index = 0; index < 3; ++index) {
        bool ok = false;
        const int value = parts.at(index).toInt(&ok);
        if (!ok || value < 0) {
            return false;
        }
        parsedParts[static_cast<std::size_t>(index)] = value;
    }

    *versionParts = parsedParts;
    return true;
}

int compareThreePartVersions(
    const QString& leftVersion,
    const QString& rightVersion,
    bool* ok = nullptr)
{
    std::array<int, 3> leftParts {};
    std::array<int, 3> rightParts {};
    const bool parsed =
        tryParseThreePartVersion(leftVersion, &leftParts) &&
        tryParseThreePartVersion(rightVersion, &rightParts);

    if (ok != nullptr) {
        *ok = parsed;
    }

    if (!parsed) {
        return 0;
    }

    for (int index = 0; index < 3; ++index) {
        if (leftParts[static_cast<std::size_t>(index)] <
            rightParts[static_cast<std::size_t>(index)]) {
            return -1;
        }

        if (leftParts[static_cast<std::size_t>(index)] >
            rightParts[static_cast<std::size_t>(index)]) {
            return 1;
        }
    }

    return 0;
}

bool parseManifest(
    const QByteArray& payload,
    const QString& manifestUrl,
    UpdateManifest* manifest,
    QString* errorMessage)
{
    if (manifest == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("内部错误：版本清单输出对象为空");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("版本清单解析失败：%1")
                                .arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject object = document.object();
    const QString latestVersion =
        object.value(QStringLiteral("latest_version")).toString().trimmed();
    const QString installerUrlValue =
        object.value(QStringLiteral("installer_url")).toString().trimmed();
    const QString installerSha256 =
        object.value(QStringLiteral("installer_sha256")).toString().trimmed();
    const QString minSupportedVersion =
        object.value(QStringLiteral("min_supported_version")).toString().trimmed();

    if (latestVersion.isEmpty() ||
        installerUrlValue.isEmpty() ||
        installerSha256.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("版本清单缺少必要字段");
        }
        return false;
    }

    bool latestVersionOk = false;
    compareThreePartVersions(latestVersion, latestVersion, &latestVersionOk);
    if (!latestVersionOk) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("latest_version 格式无效：%1")
                                .arg(latestVersion);
        }
        return false;
    }

    if (!minSupportedVersion.isEmpty()) {
        bool minimumVersionOk = false;
        compareThreePartVersions(
            minSupportedVersion,
            minSupportedVersion,
            &minimumVersionOk);
        if (!minimumVersionOk) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("min_supported_version 格式无效：%1")
                                    .arg(minSupportedVersion);
            }
            return false;
        }
    }

    const QString normalizedSha256 = normalizeSha256(installerSha256);
    if (normalizedSha256.size() != 64) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("installer_sha256 格式无效");
        }
        return false;
    }

    UpdateManifest parsedManifest;
    parsedManifest.latestVersion = latestVersion;
    parsedManifest.installerUrl =
        QUrl(manifestUrl).resolved(QUrl(installerUrlValue)).toString();
    parsedManifest.installerSha256 = normalizedSha256;
    parsedManifest.fileSize =
        static_cast<qint64>(object.value(QStringLiteral("file_size")).toDouble(0));
    parsedManifest.releaseNotes =
        object.value(QStringLiteral("release_notes")).toString().trimmed();
    parsedManifest.forceUpdate =
        object.value(QStringLiteral("force_update")).toBool(false);
    parsedManifest.minSupportedVersion = minSupportedVersion;
    parsedManifest.publishTime =
        object.value(QStringLiteral("publish_time")).toString().trimmed();

    *manifest = parsedManifest;
    return true;
}

QString sha256ForFile(const QString& filePath, QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法读取安装包进行 SHA256 校验：%1")
                                .arg(file.errorString());
        }
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("读取安装包失败：%1")
                                    .arg(file.errorString());
            }
            return {};
        }
        hash.addData(chunk);
    }

    return QString::fromLatin1(hash.result().toHex()).toUpper();
}

QString safeInstallerFileName(const QString& installerUrl, const QString& latestVersion)
{
    QString fileName = QFileInfo(QUrl(installerUrl).path()).fileName();
    if (fileName.isEmpty()) {
        fileName = QStringLiteral("BSSAS_%1.exe").arg(latestVersion);
    }

    fileName.replace(
        QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")),
        QStringLiteral("_"));
    if (!fileName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        fileName.append(QStringLiteral(".exe"));
    }
    return fileName;
}

QStringList updaterRuntimeDependencyPatterns()
{
    return {
        QStringLiteral("concrt140.dll"),
        QStringLiteral("msvcp140*.dll"),
        QStringLiteral("vcruntime140*.dll")
    };
}

QStringList updaterRuntimeFileNames()
{
    QStringList runtimeFileNames { QStringLiteral("Updater.exe") };
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    for (const QString& pattern : updaterRuntimeDependencyPatterns()) {
        const QStringList matchedFileNames =
            applicationDirectory.entryList(
                { pattern },
                QDir::Files | QDir::Readable | QDir::NoSymLinks);
        for (const QString& matchedFileName : matchedFileNames) {
            if (!runtimeFileNames.contains(matchedFileName, Qt::CaseInsensitive)) {
                runtimeFileNames.append(matchedFileName);
            }
        }
    }

    return runtimeFileNames;
}

bool copyFileReplacingExisting(
    const QString& sourceFilePath,
    const QString& targetFilePath,
    QString* errorMessage)
{
    if (!QFileInfo::exists(sourceFilePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("未找到 Updater.exe：%1").arg(sourceFilePath);
        }
        return false;
    }

    if (QFileInfo::exists(targetFilePath) && !QFile::remove(targetFilePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法覆盖临时更新器：%1").arg(targetFilePath);
        }
        return false;
    }

    if (!QFile::copy(sourceFilePath, targetFilePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法复制临时更新器到：%1").arg(targetFilePath);
        }
        return false;
    }

    const QFileDevice::Permissions permissions = QFile::permissions(sourceFilePath);
    if (permissions != QFileDevice::Permissions()) {
        QFile::setPermissions(targetFilePath, permissions);
    }

    return true;
}

} // namespace

UpdateManager::UpdateManager(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

UpdateManager::~UpdateManager()
{
    if (m_manifestReply != nullptr) {
        m_manifestReply->abort();
    }

    if (m_downloadReply != nullptr) {
        m_downloadReply->abort();
    }
}

QString UpdateManager::currentVersion() const
{
    return QCoreApplication::applicationVersion();
}

bool UpdateManager::checking() const
{
    return m_checking;
}

bool UpdateManager::downloading() const
{
    return m_downloading;
}

bool UpdateManager::busy() const
{
    return m_checking || m_downloading;
}

bool UpdateManager::updateAvailable() const
{
    return m_updateAvailable;
}

bool UpdateManager::mandatoryUpdate() const
{
    return m_mandatoryUpdate;
}

QString UpdateManager::latestVersion() const
{
    return m_latestVersion;
}

QString UpdateManager::installerUrl() const
{
    return m_installerUrl;
}

QString UpdateManager::releaseNotes() const
{
    return m_releaseNotes;
}

QString UpdateManager::minSupportedVersion() const
{
    return m_minSupportedVersion;
}

QString UpdateManager::publishTimeDisplay() const
{
    return m_publishTimeDisplay;
}

qint64 UpdateManager::fileSize() const
{
    return m_fileSize;
}

qint64 UpdateManager::downloadedBytes() const
{
    return m_downloadedBytes;
}

qint64 UpdateManager::totalBytes() const
{
    return m_totalBytes;
}

qreal UpdateManager::downloadProgress() const
{
    if (m_totalBytes <= 0) {
        return -1.0;
    }

    return static_cast<qreal>(m_downloadedBytes) / static_cast<qreal>(m_totalBytes);
}

QString UpdateManager::statusMessage() const
{
    return m_statusMessage;
}

/**
 * @brief 从配置的版本清单URL检查是否有新版本。
 * @param manual 是否为用户手动触发（手动触发时会在完成与否时显示toast提示）。
 */
void UpdateManager::checkForUpdates(bool manual)
{
    if (busy()) {
        if (manual) {
            emit toastRequested(QStringLiteral("更新检查或下载正在进行，请稍后。"), false);
        }
        return;
    }

    const QString manifestUrl = resolveManifestUrl();
    if (manifestUrl.isEmpty()) {
        notifyCheckError(QStringLiteral("未配置更新清单地址。"));
        return;
    }

    m_lastCheckWasManual = manual;
    setChecking(true);
    setStatusMessage(
        manual
            ? QStringLiteral("正在检查更新...")
            : QStringLiteral("启动后正在异步检查更新..."));

    QNetworkRequest request { QUrl(manifestUrl) };
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("%1/%2")
            .arg(QCoreApplication::applicationName(), currentVersion()));
    request.setTransferTimeout(15000);
    request.setRawHeader("Connection", "close");

    m_manifestReply = m_networkManager->get(request);
    connect(
        m_manifestReply,
        &QNetworkReply::finished,
        this,
        &UpdateManager::onManifestReplyFinished);
}

void UpdateManager::checkForUpdatesOnStartup()
{
    checkForUpdates(false);
}

/**
 * @brief 启动更新下载与安装流程：创建临时目录、下载安装包、校验SHA256，最后启动独立更新器进程。
 */
void UpdateManager::startUpdate()
{
    if (m_checking) {
        emit toastRequested(QStringLiteral("正在检查更新，请稍候再试。"), false);
        return;
    }

    if (m_downloading) {
        emit promptUpdateDialog();
        return;
    }

    if (!m_updateAvailable) {
        emit toastRequested(QStringLiteral("当前没有可安装的新版本。"), false);
        return;
    }

    QString errorMessage;
    if (!ensureTemporaryDirectory(&errorMessage)) {
        setStatusMessage(errorMessage);
        emit toastRequested(errorMessage, true);
        return;
    }

    m_downloadAbortReason.clear();
    m_downloadTargetFilePath = buildDownloadFilePath();
    m_downloadFile = std::make_unique<QSaveFile>(m_downloadTargetFilePath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        errorMessage = QStringLiteral("无法创建安装包文件：%1")
                           .arg(m_downloadFile->errorString());
        m_downloadFile.reset();
        setStatusMessage(errorMessage);
        emit toastRequested(errorMessage, true);
        return;
    }

    setDownloadProgress(0, m_fileSize);
    setDownloading(true);
    setStatusMessage(QStringLiteral("正在下载更新包..."));
    emit promptUpdateDialog();

    QNetworkRequest request { QUrl(m_installerUrl) };
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    request.setRawHeader("Connection", "close");

    m_downloadReply = m_networkManager->get(request);
    connect(
        m_downloadReply,
        &QNetworkReply::readyRead,
        this,
        &UpdateManager::onDownloadReadyRead);
    connect(
        m_downloadReply,
        &QNetworkReply::downloadProgress,
        this,
        &UpdateManager::onDownloadProgress);
    connect(
        m_downloadReply,
        &QNetworkReply::finished,
        this,
        &UpdateManager::onDownloadFinished);
}

void UpdateManager::setChecking(bool checking)
{
    if (m_checking == checking) {
        return;
    }

    const bool wasBusy = busy();
    m_checking = checking;
    emit checkingChanged();
    if (wasBusy != busy()) {
        emit busyChanged();
    }
}

void UpdateManager::setDownloading(bool downloading)
{
    if (m_downloading == downloading) {
        return;
    }

    const bool wasBusy = busy();
    m_downloading = downloading;
    emit downloadingChanged();
    if (wasBusy != busy()) {
        emit busyChanged();
    }
}

void UpdateManager::setUpdateAvailable(bool updateAvailable)
{
    if (m_updateAvailable == updateAvailable) {
        return;
    }

    m_updateAvailable = updateAvailable;
    emit updateAvailableChanged();
}

void UpdateManager::setMandatoryUpdate(bool mandatoryUpdate)
{
    if (m_mandatoryUpdate == mandatoryUpdate) {
        return;
    }

    m_mandatoryUpdate = mandatoryUpdate;
    emit updateInfoChanged();
}

void UpdateManager::setStatusMessage(const QString& statusMessage)
{
    if (m_statusMessage == statusMessage) {
        return;
    }

    m_statusMessage = statusMessage;
    emit statusMessageChanged();
}

void UpdateManager::updateManifestMetadata(
    const QString& latestVersion,
    const QString& installerUrl,
    const QString& installerSha256,
    const QString& releaseNotes,
    const QString& minSupportedVersion,
    const QString& publishTimeDisplay,
    qint64 fileSize)
{
    const bool changed =
        m_latestVersion != latestVersion ||
        m_installerUrl != installerUrl ||
        m_installerSha256 != installerSha256 ||
        m_releaseNotes != releaseNotes ||
        m_minSupportedVersion != minSupportedVersion ||
        m_publishTimeDisplay != publishTimeDisplay ||
        m_fileSize != fileSize;

    if (!changed) {
        return;
    }

    m_latestVersion = latestVersion;
    m_installerUrl = installerUrl;
    m_installerSha256 = installerSha256;
    m_releaseNotes = releaseNotes;
    m_minSupportedVersion = minSupportedVersion;
    m_publishTimeDisplay = publishTimeDisplay;
    m_fileSize = fileSize;
    emit updateInfoChanged();
}

void UpdateManager::setDownloadProgress(qint64 downloadedBytes, qint64 totalBytes)
{
    const qint64 nextTotalBytes = totalBytes > 0 ? totalBytes : m_totalBytes;
    if (m_downloadedBytes == downloadedBytes && m_totalBytes == nextTotalBytes) {
        return;
    }

    m_downloadedBytes = downloadedBytes;
    m_totalBytes = nextTotalBytes;
    emit downloadProgressChanged();
}

/**
 * @brief 处理版本清单HTTP响应：解析JSON、比较版本号、判断是否强制更新。
 */
void UpdateManager::onManifestReplyFinished()
{
    QNetworkReply* const reply = m_manifestReply;
    m_manifestReply = nullptr;
    setChecking(false);

    if (reply == nullptr) {
        return;
    }

    const QByteArray payload = reply->readAll();
    const QUrl replyUrl = reply->url();

    if (reply->error() != QNetworkReply::NoError) {
        notifyCheckError(buildReplyErrorMessage(QStringLiteral("检查更新失败"), reply));
        reply->deleteLater();
        return;
    }

    UpdateManifest manifest;
    QString errorMessage;
    if (!parseManifest(payload, replyUrl.toString(), &manifest, &errorMessage)) {
        notifyCheckError(errorMessage);
        reply->deleteLater();
        return;
    }

    updateManifestMetadata(
        manifest.latestVersion,
        manifest.installerUrl,
        manifest.installerSha256,
        manifest.releaseNotes,
        manifest.minSupportedVersion,
        formatPublishTime(manifest.publishTime),
        manifest.fileSize);

    bool versionOk = false;
    const int comparisonResult =
        compareThreePartVersions(manifest.latestVersion, currentVersion(), &versionOk);
    if (!versionOk) {
        notifyCheckError(
            QStringLiteral("当前版本号或服务端版本号格式无效，无法比较更新。"));
        reply->deleteLater();
        return;
    }

    const bool hasNewerVersion = comparisonResult > 0;
    bool mandatory = manifest.forceUpdate;
    if (!manifest.minSupportedVersion.isEmpty()) {
        bool minVersionOk = false;
        const int minimumComparison = compareThreePartVersions(
            currentVersion(),
            manifest.minSupportedVersion,
            &minVersionOk);
        if (!minVersionOk) {
            notifyCheckError(QStringLiteral("最小支持版本号格式无效。"));
            reply->deleteLater();
            return;
        }
        mandatory = mandatory || minimumComparison < 0;
    }

    setMandatoryUpdate(hasNewerVersion && mandatory);
    setUpdateAvailable(hasNewerVersion);

    if (hasNewerVersion) {
        setStatusMessage(QStringLiteral("发现新版本 %1").arg(manifest.latestVersion));
        emit promptUpdateDialog();
    } else {
        setMandatoryUpdate(false);
        setStatusMessage(QStringLiteral("当前已是最新版本 %1").arg(currentVersion()));
        if (m_lastCheckWasManual) {
            emit toastRequested(statusMessage(), false);
        }
    }

    reply->deleteLater();
}

void UpdateManager::onDownloadReadyRead()
{
    if (m_downloadReply == nullptr || m_downloadFile == nullptr) {
        return;
    }

    const QByteArray chunk = m_downloadReply->readAll();
    if (chunk.isEmpty()) {
        return;
    }

    if (m_downloadFile->write(chunk) != chunk.size()) {
        m_downloadAbortReason =
            QStringLiteral("写入安装包失败：%1").arg(m_downloadFile->errorString());
        m_downloadReply->abort();
    }
}

void UpdateManager::onDownloadProgress(qint64 receivedBytes, qint64 totalBytes)
{
    setDownloadProgress(receivedBytes, totalBytes > 0 ? totalBytes : m_fileSize);
}

void UpdateManager::onDownloadFinished()
{
    QNetworkReply* const reply = m_downloadReply;
    m_downloadReply = nullptr;

    if (reply == nullptr) {
        setDownloading(false);
        return;
    }

    onDownloadReadyRead();

    QString errorMessage = m_downloadAbortReason;
    if (errorMessage.isEmpty() && reply->error() != QNetworkReply::NoError) {
        errorMessage = QStringLiteral("下载安装包失败：%1")
                           .arg(reply->errorString());
    }

    if (m_downloadAbortReason.isEmpty() && reply->error() != QNetworkReply::NoError) {
        errorMessage =
            buildReplyErrorMessage(QStringLiteral("下载安装包失败"), reply);
    }

    if (errorMessage.isEmpty() && m_downloadFile != nullptr && m_fileSize > 0 &&
        m_downloadFile->size() != m_fileSize) {
        errorMessage = QStringLiteral("安装包大小校验失败，请重新下载。");
    }

    if (errorMessage.isEmpty() && m_downloadFile != nullptr && !m_downloadFile->commit()) {
        errorMessage = QStringLiteral("保存安装包失败：%1")
                           .arg(m_downloadFile->errorString());
    }

    if (!errorMessage.isEmpty()) {
        finalizeFailedDownload();
        setDownloading(false);
        setStatusMessage(errorMessage);
        emit toastRequested(errorMessage, true);
        reply->deleteLater();
        return;
    }

    m_downloadFile.reset();

    if (m_fileSize > 0 &&
        QFileInfo(m_downloadTargetFilePath).size() != m_fileSize) {
        QFile::remove(m_downloadTargetFilePath);
        setDownloading(false);
        setStatusMessage(QStringLiteral("安装包大小与版本清单不一致，请重新下载。"));
        emit toastRequested(statusMessage(), true);
        reply->deleteLater();
        return;
    }

    setStatusMessage(QStringLiteral("正在校验安装包完整性..."));

    QString hashErrorMessage;
    const QString localSha256 =
        sha256ForFile(m_downloadTargetFilePath, &hashErrorMessage);
    if (localSha256.isEmpty()) {
        QFile::remove(m_downloadTargetFilePath);
        setDownloading(false);
        setStatusMessage(hashErrorMessage);
        emit toastRequested(hashErrorMessage, true);
        reply->deleteLater();
        return;
    }

    if (localSha256 != m_installerSha256) {
        QFile::remove(m_downloadTargetFilePath);
        setDownloading(false);
        setStatusMessage(QStringLiteral("SHA256 校验失败，安装包已删除，请重新下载。"));
        emit toastRequested(statusMessage(), true);
        reply->deleteLater();
        return;
    }

    setDownloading(false);

    QString updaterError;
    if (!launchUpdater(&updaterError)) {
        setStatusMessage(updaterError);
        emit toastRequested(updaterError, true);
        reply->deleteLater();
        return;
    }

    setStatusMessage(QStringLiteral("更新程序已启动，主程序即将退出。"));
    reply->deleteLater();
    QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
}

QString UpdateManager::buildDownloadFilePath() const
{
    return QDir(DatabaseStoragePaths::TEMPORARY_FILE_PATH).filePath(
        safeInstallerFileName(m_installerUrl, m_latestVersion));
}

QString UpdateManager::buildTemporaryUpdaterFilePath() const
{
    const QString uniqueSuffix =
        QStringLiteral("%1_%2")
            .arg(QCoreApplication::applicationPid())
            .arg(QDateTime::currentMSecsSinceEpoch());
    const QString updaterDirectoryPath =
        QDir(DatabaseStoragePaths::TEMPORARY_FILE_PATH).filePath(
            QStringLiteral("UpdaterRuntime_%1").arg(uniqueSuffix));
    return QDir(updaterDirectoryPath).filePath(QStringLiteral("Updater.exe"));
}

bool UpdateManager::ensureTemporaryDirectory(QString* errorMessage) const
{
    if (QDir(DatabaseStoragePaths::TEMPORARY_FILE_PATH).exists() ||
        QDir().mkpath(DatabaseStoragePaths::TEMPORARY_FILE_PATH)) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("无法创建更新临时目录：%1")
                            .arg(DatabaseStoragePaths::TEMPORARY_FILE_PATH);
    }
    return false;
}

bool UpdateManager::prepareUpdaterForLaunch(
    QString* updaterPath,
    QString* errorMessage) const
{
    if (updaterPath == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("内部错误：更新器启动路径输出参数为空。");
        }
        return false;
    }

    if (!ensureTemporaryDirectory(errorMessage)) {
        return false;
    }

    const QString temporaryUpdaterPath = buildTemporaryUpdaterFilePath();
    const QString temporaryUpdaterDirectoryPath =
        QFileInfo(temporaryUpdaterPath).absolutePath();
    if (!QDir().mkpath(temporaryUpdaterDirectoryPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建临时更新器目录：%1")
                                .arg(temporaryUpdaterDirectoryPath);
        }
        return false;
    }

    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const QDir temporaryUpdaterDirectory(temporaryUpdaterDirectoryPath);
    const QStringList runtimeFileNames = updaterRuntimeFileNames();
    for (const QString& runtimeFileName : runtimeFileNames) {
        if (!copyFileReplacingExisting(
                applicationDirectory.filePath(runtimeFileName),
                temporaryUpdaterDirectory.filePath(runtimeFileName),
                errorMessage)) {
            QDir(temporaryUpdaterDirectoryPath).removeRecursively();
            return false;
        }
    }

    *updaterPath = temporaryUpdaterPath;
    return true;
}

bool UpdateManager::launchUpdater(QString* errorMessage) const
{
    QString updaterPath;
    if (!prepareUpdaterForLaunch(&updaterPath, errorMessage)) {
        return false;
    }

    const QStringList arguments {
        QStringLiteral("--wait-pid"),
        QString::number(QCoreApplication::applicationPid()),
        QStringLiteral("--installer"),
        m_downloadTargetFilePath,
        QStringLiteral("--target"),
        QCoreApplication::applicationFilePath()
    };

    if (!QProcess::startDetached(
            updaterPath,
            arguments,
            QFileInfo(updaterPath).absolutePath())) {
        QFile::remove(updaterPath);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("启动临时更新器失败：%1").arg(updaterPath);
        }
        return false;
    }

    return true;
}

void UpdateManager::finalizeFailedDownload()
{
    if (m_downloadFile != nullptr) {
        m_downloadFile->cancelWriting();
        m_downloadFile.reset();
    }

    if (!m_downloadTargetFilePath.isEmpty()) {
        QFile::remove(m_downloadTargetFilePath);
    }

    m_downloadAbortReason.clear();
    setDownloadProgress(0, m_fileSize);
}

void UpdateManager::notifyCheckError(const QString& message)
{
    setStatusMessage(message);
    setUpdateAvailable(false);
    setMandatoryUpdate(false);
    if (m_lastCheckWasManual) {
        emit toastRequested(message, true);
    }
}
