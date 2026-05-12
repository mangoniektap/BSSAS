/** @file WAVHandle.h
 *  @brief WAV 文件处理模块，提供 WAV 格式的导入导出及实时多通道导出功能。
 */

#ifndef WAVHANDLE_H
#define WAVHANDLE_H

#include <QCoreApplication> // qApp definition
#include <QObject>
#include <QString>
#include <QVector>

class QThread;

/** @brief WAV 处理工作线程，在后台执行导入导出以避免阻塞 UI。 */
class WAVHandleWorker : public QObject
{
    Q_OBJECT
public:
    explicit WAVHandleWorker(QObject* parent = nullptr);
    ~WAVHandleWorker() override;

signals:
    /** @brief 操作完成信号（通用） */
    void operationCompleted();
    /** @brief 导出成功信号 @param outputPath 导出文件路径 */
    void exportSucceeded(const QString& outputPath);
    /** @brief 导出失败信号 @param errorMessage 错误消息 */
    void exportFailed(const QString& errorMessage);
    /** @brief 导入数据就绪信号 @param dataImportSamplingRate 导入采样率 @param dataImport 导入的时域数据 */
    void importDataReady(int dataImportSamplingRate, const QVector<float>& dataImport);
    /** @brief 导入失败信号 @param errorMessage 错误消息 */
    void importFailed(const QString& errorMessage);

public slots:
    /**
     * @brief 将临时浮点数据导出为 WAV 文件。
     * @param temporaryFloatFilePath 临时浮点数据文件路径
     * @param sampleRate             采样率 (Hz)
     * @param pythonScriptPath       Python 脚本路径
     * @param channelCount           通道数，默认 1
     */
    void exportToWav(
        const QString& temporaryFloatFilePath,
        int sampleRate,
        const QString& pythonScriptPath,
        int channelCount = 1);
    /**
     * @brief 将实时多通道数据导出为 WAV 文件。
     * @param channelTemporaryFilePaths       各通道临时文件路径列表
     * @param postProcessedChannelCount       后处理通道数
     * @param sampleRate                      采样率 (Hz)
     * @param waveletDenoisingEnabled         小波去噪是否启用
     * @param transientNoiseSuppressionEnabled 瞬态噪声抑制是否启用
     * @param motionArtifactReductionEnabled  运动伪影削减是否启用
     * @param pythonScriptPath                Python 脚本路径
     */
    void exportRealtimeChannelsToWav(
        const QVector<QString>& channelTemporaryFilePaths,
        int postProcessedChannelCount,
        int sampleRate,
        bool waveletDenoisingEnabled,
        bool transientNoiseSuppressionEnabled,
        bool motionArtifactReductionEnabled,
        const QString& pythonScriptPath);
    /**
     * @brief 从 WAV 文件导入数据。
     * @param pythonScriptPath Python 脚本路径
     * @param wavFilePATH      WAV 文件路径
     */
    void importFromWav(const QString& pythonScriptPath, const QString& wavFilePATH);

private:
    inline static const float MAX_VOLTAGE = 5.0f;      /**< 最大电压范围 */
    inline static const float MIN_VOLTAGE = -5.0f;     /**< 最小电压范围 */
    inline static const float INT16_MAX_VAL = 32767.0f; /**< int16 最大值 */
};

/** @brief WAV 处理管理器 (单例)，协调 WAVHandleWorker 后台线程与 QML 界面交互。 */
class WAVHandle : public QObject
{
    Q_OBJECT
    /** @brief 参考噪声通道保存是否启用 */
    Q_PROPERTY(
        bool referenceNoiseChannelSaveEnabled
        READ referenceNoiseChannelSaveEnabled
        WRITE setReferenceNoiseChannelSaveEnabled
        NOTIFY referenceNoiseChannelSaveEnabledChanged)
public:
    static WAVHandle* instance()
    {
        if (m_instance == nullptr) {
            if (m_instance == nullptr) {
                m_instance = new WAVHandle(qApp);
            }
        }
        return m_instance;
    }

    /** @brief 启动实时数据保存为 WAV */
    Q_INVOKABLE void startSaveAsWav();
    /** @brief 启动导入数据保存为 WAV */
    Q_INVOKABLE void startSaveImportedAsWav();
    /**
     * @brief 启动从 WAV 文件读取数据。
     * @param wavFilePATH WAV 文件路径
     */
    Q_INVOKABLE void startReadFromWav(const QString& wavFilePATH);

    /** @brief 获取导入的时域数据 @returns 时域数据向量 */
    const QVector<float> timeDomainDataImport() const { return m_timeDomainDataImport; }
    /** @brief 获取导入数据的采样率 @returns 采样率 (Hz) */
    int importSampleRate() const { return m_importSampleRate; }
    /** @brief 参考噪声通道保存是否启用 @returns 启用状态 */
    bool referenceNoiseChannelSaveEnabled() const { return m_referenceNoiseChannelSaveEnabled; }
    /** @brief 设置参考噪声通道保存开关 @param enabled 是否启用 */
    void setReferenceNoiseChannelSaveEnabled(bool enabled);

public slots:
    /** @brief 更新导入时域数据缓存 @param dataImportSamplingRate 采样率 @param dataImport 时域数据 */
    void updateTimeDomainDataImport(int dataImportSamplingRate, const QVector<float>& dataImport);
    /** @brief 处理导出成功 @param outputPath 输出文件路径 */
    void handleExportSucceeded(const QString& outputPath);
    /** @brief 处理导出失败 @param errorMessage 错误消息 */
    void handleExportFailed(const QString& errorMessage);
    /** @brief 处理导入失败 @param errorMessage 错误消息 */
    void handleImportFailed(const QString& errorMessage);

signals:
    /** @brief 实时保存完成信号 @param outputPath 输出文件路径 */
    void saveCompleted(const QString& outputPath);
    /** @brief 实时保存失败信号 @param errorMessage 错误消息 */
    void saveFailed(const QString& errorMessage);
    /** @brief 导入数据保存完成信号 @param outputPath 输出文件路径 */
    void importedSaveCompleted(const QString& outputPath);
    /** @brief 导入数据保存失败信号 @param errorMessage 错误消息 */
    void importedSaveFailed(const QString& errorMessage);
    /** @brief 导入数据就绪信号 */
    void importDataReady();
    /** @brief 导入失败信号 @param errorMessage 错误消息 */
    void importFailed(const QString& errorMessage);
    /** @brief 参考噪声通道保存开关变化信号 */
    void referenceNoiseChannelSaveEnabledChanged();

private:
    explicit WAVHandle(QObject* parent = nullptr);
    ~WAVHandle() override;
    /**
     * @brief 启动保存操作（在线程中执行）。
     * @param temporaryFloatFilePath 临时浮点数据文件路径
     * @param sampleRate             采样率 (Hz)
     * @param importedSave           是否为导入数据保存
     */
    void startSaveOperation(
        const QString& temporaryFloatFilePath,
        int sampleRate,
        bool importedSave);

    static WAVHandle* m_instance;

    QThread* m_thread = nullptr;
    WAVHandleWorker* m_worker = nullptr;
    QVector<float> m_timeDomainDataImport;
    int m_importSampleRate;
    bool m_referenceNoiseChannelSaveEnabled = false;
};

#endif // WAVHANDLE_H
