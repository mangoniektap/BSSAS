/** @file GenerateManager.h
 *  @brief 报告生成管理器 —— 肠鸣音分析报告导出为 PDF
 *
 *  本模块负责将特征提取和识别分析的结果导出为 PDF 格式的报告。
 *  导出过程在工作线程中异步执行，通过信号通知主线程
 *  导出完成或失败。同时提供将分析结果持久化为临时 JSON 文件的功能。
 */

#ifndef GENERATEMANAGER_H
#define GENERATEMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <memory>

class PDFExport;
class QThread;

/** @brief 报告生成管理器
 *
 *  管理 PDF 报告的异步生成流程，导出识别与特征提取分析报告。
 *  busy 属性用于指示当前是否有导出任务正在进行。
 */
class GenerateManager : public QObject
{
    Q_OBJECT
    /** @brief 是否正在执行导出任务 */
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit GenerateManager(QObject* parent = nullptr);
    ~GenerateManager() override;

    /** @brief 查询是否正在执行导出任务 */
    bool busy() const;

    /** @brief 同步导出报告并返回生成的文件路径
     *  @returns 生成的 PDF 文件路径，失败返回空字符串
     */
    Q_INVOKABLE QString exportIdentificationAndFeatureExtractionReport();
    QString exportIdentificationAndFeatureExtractionReportTo(
        const QString& pdfFilePath,
        QString* errorMessage = nullptr);
    QString exportCollectionInformationReport(
        const QVariantMap& formData,
        const QString& pdfFilePath,
        QString* errorMessage = nullptr);
    /** @brief 异步启动导出任务（完成时通过 exportCompleted/exportFailed 信号通知） */
    Q_INVOKABLE void startExportIdentificationAndFeatureExtractionReport();
    /** @brief 将导入信号的分析摘要持久化为临时 JSON 文件
     *  @param featureValues 特征值映射
     *  @param errorMessage 可选错误输出
     *  @returns 写入成功返回 true
     */
    static bool persistImportedAnalysisTemporaryJson(
        const QVariantMap& featureValues,
        QString* errorMessage = nullptr);

signals:
    /** @brief 忙碌状态发生变化时发出 */
    void busyChanged();
    /** @brief 导出完成，携带生成的 PDF 文件路径 */
    void exportCompleted(const QString& pdfFilePath);
    /** @brief 导出失败，携带错误信息 */
    void exportFailed(const QString& errorMessage);

private:
    /** @brief 设置忙碌状态 */
    void setBusy(bool busy);
    /** @brief 确保输出目录存在 */
    static bool ensureOutputDirectoryExists();
    /** @brief 根据报告类型和日期生成唯一报告 ID */
    static QString createReportId(const QString& reportKind, const QDate& reportDate);
    /** @brief 根据报告 ID 和扩展名生成输出文件完整路径 */
    static QString createOutputFilePath(const QString& reportId, const QString& extension);
    /** @brief 构建导出报告所需的完整 JSON 数据
     *  @param featureValues 特征值
     *  @param reportId 报告 ID
     *  @param reportKind 报告类型
     *  @param generatedAt 生成时间
     *  @returns 报告 JSON 数据
     */
    static QVariantMap buildReportPayload(
        const QVariantMap& featureValues,
        const QString& reportId,
        const QString& reportKind,
        const QDateTime& generatedAt);
    static QVariantMap buildCollectionInformationReportPayload(
        const QVariantMap& formData,
        const QString& reportId,
        const QDateTime& generatedAt);

    bool m_busy = false;
    QThread* m_exportThread = nullptr;
    std::unique_ptr<PDFExport> m_pdfExport;
};

#endif // GENERATEMANAGER_H
