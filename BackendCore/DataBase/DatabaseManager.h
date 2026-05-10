#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariantList>

class DatabaseManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString databaseFilePath READ databaseFilePath CONSTANT)

public:
    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager() override;

    static DatabaseManager* instance();

    bool openDatabase(const QString& dbPath);
    void closeDatabase();

    bool addFileTags(const QString& filePath, const QStringList& tags);
    QStringList searchFilesByTag(const QString& tagKeyword);
    bool initializeTables();

    Q_INVOKABLE bool initializeSearchDatabase();
    Q_INVOKABLE bool rebuildFileIndex();
    Q_INVOKABLE QVariantList searchFiles(const QString& keyword);
    Q_INVOKABLE bool openFile(const QString& filePath);
    Q_INVOKABLE QString databaseFilePath() const;
    Q_INVOKABLE QStringList indexedRoots() const;
    QString lastError() const;

signals:
    void lastErrorChanged();

private:
    bool ensureSearchDatabaseReady();
    bool ensureManagedDirectoriesExist();
    bool clearIndexedFiles();
    bool indexManagedDirectory(const QString& directoryPath, const QString& category);
    bool insertIndexedFile(
        const QString& absolutePath,
        const QString& relativePath,
        const QString& category);
    void setLastError(const QString& message);

    static QString buildSearchText(
        const QString& fileName,
        const QString& relativePath,
        const QString& category);
    static bool isDirectlyOpenable(const QString& filePath);
    static QString normalizedExtension(const QString& filePath);

    QSqlDatabase m_db;
    QString m_databaseFilePath;
    QString m_lastError;
};

#endif // DATABASEMANAGER_H
