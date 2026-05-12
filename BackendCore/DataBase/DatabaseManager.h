/** @file DatabaseManager.h
 *  @brief 数据库管理器 —— SQLite 文件索引、全文搜索与主面板统计
 *
 *  本模块管理 BSSAS 的文件索引数据库（BSSAS_FileIndex.sqlite），
 *  提供文件的标签管理、关键词全文检索、主面板总览统计以及
 *  通过系统关联程序直接打开文件等功能。
 */

#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

/** @brief 数据库管理器
 *
 *  封装 SQLite 数据库操作，提供文件索引重建、关键词搜索、
 *  标签管理以及主面板统计信息查询。通过 Q_PROPERTY 暴露
 *  最后一次错误信息供 QML 界面显示。
 */
class DatabaseManager : public QObject
{
    Q_OBJECT
    /** @brief 最近一次数据库操作的错误信息 */
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    /** @brief 当前打开的数据库文件路径（常量属性） */
    Q_PROPERTY(QString databaseFilePath READ databaseFilePath CONSTANT)

public:
    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager() override;

    /** @brief 获取全局单例实例 */
    static DatabaseManager* instance();

    /** @brief 打开指定路径的 SQLite 数据库
     *  @param dbPath 数据库文件完整路径
     *  @returns 打开成功返回 true
     */
    bool openDatabase(const QString& dbPath);
    /** @brief 关闭当前数据库连接 */
    void closeDatabase();

    /** @brief 为指定文件追加标签
     *  @param filePath 文件绝对路径
     *  @param tags 待添加的标签列表
     *  @returns 操作成功返回 true
     */
    bool addFileTags(const QString& filePath, const QStringList& tags);
    /** @brief 根据标签关键词搜索文件
     *  @param tagKeyword 标签关键词（支持模糊匹配）
     *  @returns 匹配的文件路径列表
     */
    QStringList searchFilesByTag(const QString& tagKeyword);
    /** @brief 创建数据库基础表结构
     *  @returns 创建成功返回 true
     */
    bool initializeTables();

    /** @brief 初始化搜索数据库（确保表结构和索引就绪） */
    Q_INVOKABLE bool initializeSearchDatabase();
    /** @brief 重建文件索引：扫描托管目录并将文件信息写入索引表 */
    Q_INVOKABLE bool rebuildFileIndex();
    /** @brief 根据关键词全文搜索文件
     *  @param keyword 搜索关键词
     *  @returns 搜索结果列表，每项包含文件名、路径、类别等字段
     */
    Q_INVOKABLE QVariantList searchFiles(const QString& keyword);
    /** @brief 获取主面板总览统计数据（文件总数、各类别计数等） */
    Q_INVOKABLE QVariantMap homeOverviewStats();
    /** @brief 通过系统关联程序打开指定文件
     *  @param filePath 文件绝对路径
     *  @returns 打开成功返回 true
     */
    Q_INVOKABLE bool openFile(const QString& filePath);
    /** @brief 获取当前数据库文件路径 */
    Q_INVOKABLE QString databaseFilePath() const;
    /** @brief 获取已索引的根目录列表 */
    Q_INVOKABLE QStringList indexedRoots() const;
    /** @brief 获取最近一次错误信息 */
    QString lastError() const;

signals:
    /** @brief 错误信息发生变化时发出 */
    void lastErrorChanged();

private:
    /** @brief 确保搜索数据库已打开并完成初始化 */
    bool ensureSearchDatabaseReady();
    /** @brief 确保托管目录在文件系统中存在 */
    bool ensureManagedDirectoriesExist();
    /** @brief 清空已索引文件记录 */
    bool clearIndexedFiles();
    /** @brief 索引指定目录下的所有文件
     *  @param directoryPath 待扫描的目录路径
     *  @param category 文件类别标签（如 "Audio"、"Report"）
     *  @returns 索引成功返回 true
     */
    bool indexManagedDirectory(const QString& directoryPath, const QString& category);
    /** @brief 向索引表中插入一条文件记录
     *  @param absolutePath 文件绝对路径
     *  @param relativePath 文件相对路径
     *  @param category 文件类别
     *  @returns 插入成功返回 true
     */
    bool insertIndexedFile(
        const QString& absolutePath,
        const QString& relativePath,
        const QString& category);
    /** @brief 设置最后一次错误信息 */
    void setLastError(const QString& message);

    /** @brief 构造用于 FTS5 全文检索的文本
     *  @param fileName 文件名
     *  @param relativePath 相对路径
     *  @param category 文件类别
     *  @returns 组合后的搜索文本
     */
    static QString buildSearchText(
        const QString& fileName,
        const QString& relativePath,
        const QString& category);
    /** @brief 判断文件是否可直接通过系统关联打开 */
    static bool isDirectlyOpenable(const QString& filePath);
    /** @brief 获取文件扩展名（统一小写） */
    static QString normalizedExtension(const QString& filePath);

    QSqlDatabase m_db;
    QString m_databaseFilePath;
    QString m_lastError;
};

#endif // DATABASEMANAGER_H
