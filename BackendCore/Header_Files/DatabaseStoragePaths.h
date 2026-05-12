/** @file DatabaseStoragePaths.h
 *  @brief 数据库存储路径配置 —— 统一的文件系统路径解析与管理
 *
 *  本模块定义了 BSSAS 数据库相关文件的存储路径体系。
 *  路径解析优先级为：
 *  1. 环境变量 BSSAS_DATABASE_ROOT 指定的根目录
 *  2. D:/BSSASdatabase（如果 D 盘存在）
 *  3. %LOCALAPPDATA%/BSSASdatabase
 *
 *  在此根目录下进一步派生出临时文件、报告、音频样本
 *  和搜索索引数据库等子路径。
 */

#ifndef DATABASESTORAGEPATHS_H
#define DATABASESTORAGEPATHS_H

#include <QDir>
#include <QString>
#include <QStringList>
#include <QtGlobal>

/** @brief 数据库存储路径命名空间 */
namespace DatabaseStoragePaths {

/** @brief 将路径规范化为统一格式（正斜杠、去除冗余分隔符）
 *  @param path 待规范化的路径
 *  @returns 规范化后的路径字符串
 */
inline QString normalizePath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

/** @brief 解析默认数据库根目录（不使用环境变量时的后备方案）
 *  @returns 默认数据库根目录路径
 */
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

/** @brief 解析数据库根目录（最终路径）
 *
 *  优先检查 BSSAS_DATABASE_ROOT 环境变量，未设置则回退到默认路径。
 *  @returns 数据库根目录路径
 */
inline QString resolveRootPath()
{
    const QString configuredRoot =
        qEnvironmentVariable("BSSAS_DATABASE_ROOT").trimmed();
    if (!configuredRoot.isEmpty()) {
        return normalizePath(configuredRoot);
    }

    return normalizePath(resolveDefaultRootPath());
}

/** @brief 数据库根目录路径 */
inline const QString ROOT_PATH = resolveRootPath();
/** @brief 临时文件存储目录 */
inline const QString TEMPORARY_FILE_PATH =
    QDir(ROOT_PATH).filePath(QStringLiteral("TemporaryFile"));
/** @brief 识别与特征提取报告存储目录 */
inline const QString REPORTS_PATH =
    QDir(ROOT_PATH).filePath(
        QStringLiteral("Identification_and_Feature_Extraction_Reports"));
/** @brief 肠鸣音采样音频文件目录 */
inline const QString AUDIO_PATH =
    QDir(ROOT_PATH).filePath(QStringLiteral("Intestinal_sound_samples"));
/** @brief 搜索索引 SQLite 数据库文件路径 */
inline const QString SEARCH_INDEX_DATABASE_PATH =
    QDir(ROOT_PATH).filePath(QStringLiteral("BSSAS_FileIndex.sqlite"));
/** @brief 需要进行文件索引的搜索目录列表 */
inline const QStringList SEARCH_DIRECTORIES = {
    AUDIO_PATH,
    REPORTS_PATH,
};
}

#endif // DATABASESTORAGEPATHS_H
