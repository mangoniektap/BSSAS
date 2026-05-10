#ifndef DATABASESTORAGEPATHS_H
#define DATABASESTORAGEPATHS_H

#include <QDir>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace DatabaseStoragePaths {
inline QString normalizePath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

inline QString resolveDefaultRootPath()
{
    const QString preferredDriveRoot = QStringLiteral("D:/");
    if (QDir(preferredDriveRoot).exists()) {
        return QStringLiteral("D:/BSSASdatabase");
    }

    QString localAppData = qEnvironmentVariable("LOCALAPPDATA").trimmed();
    if (localAppData.isEmpty()) {
        localAppData =
            QDir(QDir::homePath()).filePath(QStringLiteral("AppData/Local"));
    }

    return QDir(normalizePath(localAppData)).filePath(QStringLiteral("BSSASdatabase"));
}

inline QString resolveRootPath()
{
    const QString configuredRoot =
        qEnvironmentVariable("BSSAS_DATABASE_ROOT").trimmed();
    if (!configuredRoot.isEmpty()) {
        return normalizePath(configuredRoot);
    }

    return normalizePath(resolveDefaultRootPath());
}

inline const QString ROOT_PATH = resolveRootPath();
inline const QString TEMPORARY_FILE_PATH =
    QDir(ROOT_PATH).filePath(QStringLiteral("TemporaryFile"));
inline const QString REPORTS_PATH =
    QDir(ROOT_PATH).filePath(
        QStringLiteral("Identification_and_Feature_Extraction_Reports"));
inline const QString AUDIO_PATH =
    QDir(ROOT_PATH).filePath(QStringLiteral("Intestinal_sound_samples"));
inline const QString SEARCH_INDEX_DATABASE_PATH =
    QDir(ROOT_PATH).filePath(QStringLiteral("BSSAS_FileIndex.sqlite"));
inline const QStringList SEARCH_DIRECTORIES = {
    AUDIO_PATH,
    REPORTS_PATH,
};
}

#endif // DATABASESTORAGEPATHS_H
