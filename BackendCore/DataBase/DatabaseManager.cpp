#include "DatabaseManager.h"
#include "DatabaseStoragePaths.h"

#include <algorithm>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>
#include <QVariant>

namespace {
const QString kConnectionName = QStringLiteral("BSSASFileSearchConnection");
const QString kAudioCategory = QStringLiteral("Intestinal_sound_samples");
const QString kReportCategory =
    QStringLiteral("Identification_and_Feature_Extraction_Reports");
constexpr qint64 kMaxReportScanBytes = 4 * 1024 * 1024;

struct DirectoryStats
{
    int total = 0;
    int today = 0;
    int yesterday = 0;
    int abnormal = 0;
    int abnormalToday = 0;
    int abnormalYesterday = 0;
};

bool isFileOnDate(const QFileInfo& fileInfo, const QDate& date)
{
    return fileInfo.lastModified().date() == date;
}

bool containsAnyAbnormalMarker(const QString& text)
{
    return text.contains(QStringLiteral("abnormal")) ||
           text.contains(QStringLiteral("异常")) ||
           text.contains(QStringLiteral("高风险"));
}

bool reportLooksAbnormal(const QFileInfo& fileInfo)
{
    if (containsAnyAbnormalMarker(fileInfo.fileName().toLower())) {
        return true;
    }

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray bytes = file.read(std::min(fileInfo.size(), kMaxReportScanBytes));
    return containsAnyAbnormalMarker(QString::fromUtf8(bytes).toLower()) ||
           containsAnyAbnormalMarker(QString::fromLatin1(bytes).toLower());
}

DirectoryStats collectDirectoryStats(const QString& directoryPath, bool scanAbnormalReports)
{
    DirectoryStats stats;
    const QDir directory(directoryPath);
    if (!directory.exists()) {
        return stats;
    }

    const QDate today = QDate::currentDate();
    const QDate yesterday = today.addDays(-1);

    QDirIterator iterator(
        directoryPath,
        QDir::Files | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo fileInfo = iterator.fileInfo();
        const bool modifiedToday = isFileOnDate(fileInfo, today);
        const bool modifiedYesterday = isFileOnDate(fileInfo, yesterday);

        ++stats.total;
        if (modifiedToday) {
            ++stats.today;
        }
        if (modifiedYesterday) {
            ++stats.yesterday;
        }

        if (scanAbnormalReports && reportLooksAbnormal(fileInfo)) {
            ++stats.abnormal;
            if (modifiedToday) {
                ++stats.abnormalToday;
            }
            if (modifiedYesterday) {
                ++stats.abnormalYesterday;
            }
        }
    }

    return stats;
}
}

DatabaseManager::DatabaseManager(QObject* parent)
    : QObject(parent)
{
    if (QSqlDatabase::contains(kConnectionName)) {
        m_db = QSqlDatabase::database(kConnectionName);
    } else {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnectionName);
    }
}

DatabaseManager::~DatabaseManager()
{
    closeDatabase();
}

DatabaseManager* DatabaseManager::instance()
{
    static DatabaseManager manager;
    return &manager;
}

bool DatabaseManager::openDatabase(const QString& dbPath)
{
    m_databaseFilePath = dbPath;
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        setLastError(QStringLiteral("无法打开数据库：%1").arg(m_db.lastError().text()));
        return false;
    }

    setLastError(QString());
    return initializeTables();
}

void DatabaseManager::closeDatabase()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool DatabaseManager::initializeTables()
{
    if (!m_db.isOpen()) {
        setLastError(QStringLiteral("数据库尚未打开。"));
        return false;
    }

    QSqlQuery query(m_db);
    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS files ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "file_path TEXT UNIQUE, "
            "file_name TEXT)")) {
        setLastError(QStringLiteral("创建 files 表失败：%1").arg(query.lastError().text()));
        return false;
    }

    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS tags ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "tag_name TEXT UNIQUE)")) {
        setLastError(QStringLiteral("创建 tags 表失败：%1").arg(query.lastError().text()));
        return false;
    }

    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS file_tags ("
            "file_id INTEGER, "
            "tag_id INTEGER, "
            "PRIMARY KEY (file_id, tag_id), "
            "FOREIGN KEY(file_id) REFERENCES files(id), "
            "FOREIGN KEY(tag_id) REFERENCES tags(id))")) {
        setLastError(QStringLiteral("创建 file_tags 表失败：%1").arg(query.lastError().text()));
        return false;
    }

    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS indexed_files ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "file_path TEXT UNIQUE NOT NULL, "
            "file_name TEXT NOT NULL, "
            "relative_path TEXT NOT NULL, "
            "category TEXT NOT NULL, "
            "file_extension TEXT NOT NULL, "
            "search_text TEXT NOT NULL, "
            "openable INTEGER NOT NULL DEFAULT 0, "
            "last_modified_utc TEXT NOT NULL)")) {
        setLastError(QStringLiteral("创建 indexed_files 表失败：%1").arg(query.lastError().text()));
        return false;
    }

    if (!query.exec(
            "CREATE INDEX IF NOT EXISTS idx_indexed_files_name "
            "ON indexed_files(file_name)")) {
        setLastError(QStringLiteral("创建 indexed_files 索引失败：%1").arg(query.lastError().text()));
        return false;
    }

    setLastError(QString());
    return true;
}

bool DatabaseManager::addFileTags(const QString& filePath, const QStringList& tags)
{
    if (!m_db.isOpen()) {
        setLastError(QStringLiteral("数据库尚未打开。"));
        return false;
    }

    if (!m_db.transaction()) {
        setLastError(QStringLiteral("开启数据库事务失败：%1").arg(m_db.lastError().text()));
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("INSERT OR IGNORE INTO files (file_path, file_name) VALUES (?, ?)");
    query.addBindValue(filePath);
    query.addBindValue(QFileInfo(filePath).fileName());
    if (!query.exec()) {
        m_db.rollback();
        setLastError(QStringLiteral("插入文件记录失败：%1").arg(query.lastError().text()));
        return false;
    }

    query.prepare("SELECT id FROM files WHERE file_path = ?");
    query.addBindValue(filePath);
    if (!query.exec() || !query.next()) {
        m_db.rollback();
        setLastError(QStringLiteral("读取文件 ID 失败：%1").arg(query.lastError().text()));
        return false;
    }
    const int fileId = query.value(0).toInt();

    for (const QString& tag : tags) {
        query.prepare("INSERT OR IGNORE INTO tags (tag_name) VALUES (?)");
        query.addBindValue(tag);
        if (!query.exec()) {
            m_db.rollback();
            setLastError(QStringLiteral("插入标签失败：%1").arg(query.lastError().text()));
            return false;
        }

        query.prepare("SELECT id FROM tags WHERE tag_name = ?");
        query.addBindValue(tag);
        if (!query.exec() || !query.next()) {
            m_db.rollback();
            setLastError(QStringLiteral("读取标签 ID 失败：%1").arg(query.lastError().text()));
            return false;
        }
        const int tagId = query.value(0).toInt();

        query.prepare("INSERT OR IGNORE INTO file_tags (file_id, tag_id) VALUES (?, ?)");
        query.addBindValue(fileId);
        query.addBindValue(tagId);
        if (!query.exec()) {
            m_db.rollback();
            setLastError(QStringLiteral("建立标签关联失败：%1").arg(query.lastError().text()));
            return false;
        }
    }

    if (!m_db.commit()) {
        setLastError(QStringLiteral("提交文件标签事务失败：%1").arg(m_db.lastError().text()));
        return false;
    }

    setLastError(QString());
    return true;
}

QStringList DatabaseManager::searchFilesByTag(const QString& tagKeyword)
{
    QStringList results;
    if (!m_db.isOpen()) {
        setLastError(QStringLiteral("数据库尚未打开。"));
        return results;
    }

    QSqlQuery query(m_db);
    query.prepare(
        "SELECT DISTINCT f.file_path, f.file_name "
        "FROM files f "
        "JOIN file_tags ft ON f.id = ft.file_id "
        "JOIN tags t ON ft.tag_id = t.id "
        "WHERE t.tag_name LIKE ? "
        "ORDER BY f.file_name");
    query.addBindValue(QStringLiteral("%") + tagKeyword + QStringLiteral("%"));
    if (!query.exec()) {
        setLastError(QStringLiteral("按标签搜索失败：%1").arg(query.lastError().text()));
        return results;
    }

    while (query.next()) {
        results.append(query.value(0).toString());
    }

    setLastError(QString());
    return results;
}

bool DatabaseManager::initializeSearchDatabase()
{
    if (!ensureSearchDatabaseReady()) {
        return false;
    }

    return rebuildFileIndex();
}

bool DatabaseManager::rebuildFileIndex()
{
    if (!ensureSearchDatabaseReady()) {
        return false;
    }

    if (!m_db.transaction()) {
        setLastError(QStringLiteral("开启文件索引事务失败：%1").arg(m_db.lastError().text()));
        return false;
    }

    if (!clearIndexedFiles()) {
        m_db.rollback();
        return false;
    }

    if (!indexManagedDirectory(DatabaseStoragePaths::AUDIO_PATH, kAudioCategory) ||
        !indexManagedDirectory(DatabaseStoragePaths::REPORTS_PATH, kReportCategory)) {
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        setLastError(QStringLiteral("提交文件索引失败：%1").arg(m_db.lastError().text()));
        return false;
    }

    setLastError(QString());
    return true;
}

QVariantList DatabaseManager::searchFiles(const QString& keyword)
{
    QVariantList results;
    const QString trimmedKeyword = keyword.trimmed();
    if (trimmedKeyword.isEmpty()) {
        setLastError(QStringLiteral("请输入要搜索的关键词。"));
        return results;
    }

    if (!rebuildFileIndex()) {
        return results;
    }

    QSqlQuery query(m_db);
    query.prepare(
        "SELECT file_name, file_path, relative_path, category, file_extension, openable "
        "FROM indexed_files "
        "WHERE search_text LIKE ? "
        "ORDER BY category ASC, file_name COLLATE NOCASE ASC");
    query.addBindValue(
        QStringLiteral("%") + trimmedKeyword.toLower() + QStringLiteral("%"));

    if (!query.exec()) {
        setLastError(QStringLiteral("执行文件搜索失败：%1").arg(query.lastError().text()));
        return results;
    }

    while (query.next()) {
        QVariantMap item;
        const QString absolutePath = query.value(1).toString();
        const QString extension = query.value(4).toString();
        const bool openable = query.value(5).toInt() == 1;

        item.insert(QStringLiteral("fileName"), query.value(0).toString());
        item.insert(QStringLiteral("absolutePath"), absolutePath);
        item.insert(QStringLiteral("relativePath"), query.value(2).toString());
        item.insert(QStringLiteral("category"), query.value(3).toString());
        item.insert(QStringLiteral("extension"), extension);
        item.insert(QStringLiteral("typeLabel"), extension.isEmpty() ? QStringLiteral("FILE")
                                                                     : extension.mid(1).toUpper());
        item.insert(QStringLiteral("openable"), openable);
        item.insert(
            QStringLiteral("openHint"),
            openable ? QStringLiteral("可直接打开")
                     : QStringLiteral("当前仅展示路径"));
        results.append(item);
    }

    setLastError(QString());
    return results;
}

QVariantMap DatabaseManager::homeOverviewStats()
{
    if (!ensureManagedDirectoriesExist()) {
        return {};
    }

    const DirectoryStats audioStats =
        collectDirectoryStats(DatabaseStoragePaths::AUDIO_PATH, false);
    const DirectoryStats reportStats =
        collectDirectoryStats(DatabaseStoragePaths::REPORTS_PATH, true);

    QVariantMap stats;
    stats.insert(QStringLiteral("todayCollected"), audioStats.today);
    stats.insert(QStringLiteral("todayCollectedYesterday"), audioStats.yesterday);
    stats.insert(QStringLiteral("collectedTotal"), audioStats.total);
    stats.insert(QStringLiteral("analyzed"), reportStats.total);
    stats.insert(QStringLiteral("analyzedToday"), reportStats.today);
    stats.insert(QStringLiteral("analyzedYesterday"), reportStats.yesterday);
    stats.insert(QStringLiteral("abnormal"), reportStats.abnormal);
    stats.insert(QStringLiteral("abnormalToday"), reportStats.abnormalToday);
    stats.insert(QStringLiteral("abnormalYesterday"), reportStats.abnormalYesterday);
    stats.insert(QStringLiteral("databaseRecords"), audioStats.total + reportStats.total);
    stats.insert(QStringLiteral("databaseToday"), audioStats.today + reportStats.today);
    stats.insert(QStringLiteral("databaseYesterday"), audioStats.yesterday + reportStats.yesterday);
    stats.insert(
        QStringLiteral("databaseRoot"),
        QDir::toNativeSeparators(DatabaseStoragePaths::ROOT_PATH));
    stats.insert(
        QStringLiteral("audioRoot"),
        QDir::toNativeSeparators(DatabaseStoragePaths::AUDIO_PATH));
    stats.insert(
        QStringLiteral("reportRoot"),
        QDir::toNativeSeparators(DatabaseStoragePaths::REPORTS_PATH));
    stats.insert(
        QStringLiteral("updatedAt"),
        QDateTime::currentDateTime().toString(Qt::ISODate));

    return stats;
}

bool DatabaseManager::openFile(const QString& filePath)
{
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        setLastError(QStringLiteral("文件不存在：%1").arg(QDir::toNativeSeparators(filePath)));
        return false;
    }

    if (!isDirectlyOpenable(fileInfo.absoluteFilePath())) {
        setLastError(QStringLiteral("当前仅支持直接打开 txt、pdf、doc 和 docx 文件。"));
        return false;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absoluteFilePath()))) {
        setLastError(QStringLiteral("无法使用系统默认程序打开文件。"));
        return false;
    }

    setLastError(QString());
    return true;
}

QString DatabaseManager::databaseFilePath() const
{
    const QString filePath =
        m_databaseFilePath.isEmpty() ? DatabaseStoragePaths::SEARCH_INDEX_DATABASE_PATH
                                     : m_databaseFilePath;
    return QDir::toNativeSeparators(filePath);
}

QStringList DatabaseManager::indexedRoots() const
{
    QStringList roots;
    roots.reserve(DatabaseStoragePaths::SEARCH_DIRECTORIES.size());
    for (const QString& directoryPath : DatabaseStoragePaths::SEARCH_DIRECTORIES) {
        roots.append(QDir::toNativeSeparators(directoryPath));
    }
    return roots;
}

QString DatabaseManager::lastError() const
{
    return m_lastError;
}

bool DatabaseManager::ensureSearchDatabaseReady()
{
    if (!ensureManagedDirectoriesExist()) {
        return false;
    }

    const QString desiredPath = DatabaseStoragePaths::SEARCH_INDEX_DATABASE_PATH;
    if (m_db.isOpen() && m_databaseFilePath == desiredPath) {
        return true;
    }

    if (m_db.isOpen()) {
        m_db.close();
    }

    return openDatabase(desiredPath);
}

bool DatabaseManager::ensureManagedDirectoriesExist()
{
    const QStringList requiredDirectories = {
        DatabaseStoragePaths::ROOT_PATH,
        DatabaseStoragePaths::TEMPORARY_FILE_PATH,
        DatabaseStoragePaths::AUDIO_PATH,
        DatabaseStoragePaths::REPORTS_PATH,
    };

    for (const QString& directoryPath : requiredDirectories) {
        QDir directory(directoryPath);
        if (directory.exists()) {
            continue;
        }

        if (!QDir().mkpath(directoryPath)) {
            setLastError(
                QStringLiteral("无法创建目录：%1").arg(QDir::toNativeSeparators(directoryPath)));
            return false;
        }
    }

    return true;
}

bool DatabaseManager::clearIndexedFiles()
{
    QSqlQuery query(m_db);
    if (!query.exec("DELETE FROM indexed_files")) {
        setLastError(QStringLiteral("清空旧索引失败：%1").arg(query.lastError().text()));
        return false;
    }

    return true;
}

bool DatabaseManager::indexManagedDirectory(
    const QString& directoryPath,
    const QString& category)
{
    const QDir rootDirectory(directoryPath);
    if (!rootDirectory.exists()) {
        return true;
    }

    QDirIterator iterator(
        directoryPath,
        QDir::Files | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo fileInfo = iterator.fileInfo();
        const QString absolutePath =
            QDir::toNativeSeparators(fileInfo.absoluteFilePath());
        const QString relativePath =
            QDir::toNativeSeparators(rootDirectory.relativeFilePath(fileInfo.absoluteFilePath()));

        if (!insertIndexedFile(absolutePath, relativePath, category)) {
            return false;
        }
    }

    return true;
}

bool DatabaseManager::insertIndexedFile(
    const QString& absolutePath,
    const QString& relativePath,
    const QString& category)
{
    const QFileInfo fileInfo(absolutePath);
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT OR REPLACE INTO indexed_files ("
        "file_path, file_name, relative_path, category, file_extension, search_text, "
        "openable, last_modified_utc"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(absolutePath);
    query.addBindValue(fileInfo.fileName());
    query.addBindValue(relativePath);
    query.addBindValue(category);
    query.addBindValue(normalizedExtension(absolutePath));
    query.addBindValue(buildSearchText(fileInfo.fileName(), relativePath, category));
    query.addBindValue(isDirectlyOpenable(absolutePath) ? 1 : 0);
    query.addBindValue(fileInfo.lastModified().toUTC().toString(Qt::ISODate));

    if (!query.exec()) {
        setLastError(
            QStringLiteral("写入文件索引失败：%1").arg(query.lastError().text()));
        return false;
    }

    return true;
}

void DatabaseManager::setLastError(const QString& message)
{
    if (m_lastError == message) {
        return;
    }

    m_lastError = message;
    emit lastErrorChanged();
}

QString DatabaseManager::buildSearchText(
    const QString& fileName,
    const QString& relativePath,
    const QString& category)
{
    return QStringLiteral("%1 %2 %3")
        .arg(fileName, relativePath, category)
        .toLower();
}

bool DatabaseManager::isDirectlyOpenable(const QString& filePath)
{
    const QString extension = normalizedExtension(filePath);
    return extension == QStringLiteral(".txt") || extension == QStringLiteral(".pdf") ||
           extension == QStringLiteral(".doc") || extension == QStringLiteral(".docx");
}

QString DatabaseManager::normalizedExtension(const QString& filePath)
{
    QString extension = QFileInfo(filePath).suffix().trimmed().toLower();
    if (extension.isEmpty()) {
        return {};
    }

    return QStringLiteral(".") + extension;
}
