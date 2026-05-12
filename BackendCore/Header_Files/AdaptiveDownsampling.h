/** @file AdaptiveDownsampling.h
 *  @brief 自适应降采样 —— 将高采样率时域波形压缩为 QML 波形显示所需的数据点
 *
 *  本模块采用峰值检测降采样 (peak-detecting downsampling) 策略，
 *  在保留信号包络特征的前提下将实时采集的浮点样本压缩到固定点数，
 *  并通过工作线程周期性处理以保持 UI 流畅。对外通过 Q_PROPERTY
 *  暴露降采样结果供 QML 波形组件渲染。
 */

#ifndef ADAPTIVEDOWNSAMPLING_H
#define ADAPTIVEDOWNSAMPLING_H

#include <QObject>
#include <QPointF>
#include <QVariantList>
#include <QVector>

class QThread;
class QTimer;

/** @brief 降采样工作线程对象
 *
 *  在独立线程中运行，通过定时器周期性将原始数据缓冲压缩为目标点数，
 *  并将结果以信号形式发送回主线程。
 */
class DownsamplingWorker : public QObject
{
    Q_OBJECT
public:
    explicit DownsamplingWorker(QObject* parent = nullptr);
    ~DownsamplingWorker() override;

public slots:
    /** @brief 启动周期性降采样处理 */
    void startWork();
    /** @brief 停止周期性降采样处理 */
    void stopWork();

private slots:
    /** @brief 定时器回调，执行一帧降采样处理 */
    void processing();

signals:
    /** @brief 降采样结果就绪，以 QVariantList 格式发送 */
    void resultReady(const QVariantList& data);
    /** @brief 每帧点数发生变化时通知 */
    void pointsPerFrameChanged(int pointsPerFrame);

private:
    /** @brief 根据样本总数和采样率计算目标降采样点数 */
    int calculateTargetPointCount(qsizetype sampleCount, int sampleRate) const;
    /** @brief 峰值检测降采样：在局部窗口内保留最大值，形成包络
     *  @param rawData 原始浮点样本
     *  @param targetPointCount 目标点数
     *  @param sampleRate 采样率
     *  @returns 降采样后的 QPointF 序列
     */
    QVector<QPointF> peakDetectingDownsampling(
        const QVector<float>& rawData,
        int targetPointCount,
        int sampleRate) const;

    QTimer* m_timer = nullptr;
    int m_pointsPerFrame = 1;
    double m_elapsedSeconds = 0.0;
};

/** @brief 自适应降采样管理器
 *
 *  作为主线程与工作线程之间的桥梁，管理降采样处理的启动/停止，
 *  并将结果通过属性暴露给 QML 层。
 */
class AdaptiveDownsampling : public QObject
{
    Q_OBJECT
    /** @brief 每帧的数据点数量 */
    Q_PROPERTY(int pointsPerFrame READ pointsPerFrame NOTIFY pointsPerFrameChanged)
    /** @brief 当前帧的降采样数据（供 QML 波形组件使用） */
    Q_PROPERTY(QVariantList downsampledData READ downsampledData NOTIFY downsampledDataReady)
    /** @brief 当前降采样级别（0~7） */
    Q_PROPERTY(int currentDownsamplingLevel READ currentDownsamplingLevel CONSTANT)
    /** @brief 每秒降采样后保留的数据点数 */
    Q_PROPERTY(int downsamplingPointsPerSecond READ downsamplingPointsPerSecond CONSTANT)
    /** @brief 原始数据精度阈值：小于此精度的数据视为无效 */
    Q_PROPERTY(float rawDataResolutionThreshold READ rawDataResolutionThreshold CONSTANT)

public:
    static constexpr int LEVEL0 = 0;
    static constexpr int LEVEL7 = 7;
    /** @brief 当前使用的降采样级别 */
    static constexpr int CURRENT_DOWNSAMPLING_LEVEL = LEVEL7;
    /** @brief 当前级别下每秒保留点数 */
    static constexpr int CURRENT_LEVEL_POINTS_PER_SECOND = 480;
    /** @brief 原始数据精度阈值 */
    static constexpr float RAW_DATA_RESOLUTION_THRESHOLD = 0.05f;
    /** @brief 处理间隔 (ms) */
    static constexpr int PROCESSING_INTERVAL_MS = 100;

    explicit AdaptiveDownsampling(QObject* parent = nullptr);
    ~AdaptiveDownsampling() override;

    Q_INVOKABLE int pointsPerFrame() const { return m_pointsPerFrame; }
    Q_INVOKABLE const QVariantList& downsampledData() const { return m_downsampledData; }
    Q_INVOKABLE int currentDownsamplingLevel() const { return CURRENT_DOWNSAMPLING_LEVEL; }
    Q_INVOKABLE int downsamplingPointsPerSecond() const { return CURRENT_LEVEL_POINTS_PER_SECOND; }
    Q_INVOKABLE float rawDataResolutionThreshold() const { return RAW_DATA_RESOLUTION_THRESHOLD; }

    /** @brief 将原始样本直接降采样为当前级别的 QPointF 序列（静态工具方法）
     *  @param source 原始浮点样本
     *  @param sampleRate 采样率
     *  @returns 降采样后的点序列
     */
    static QVector<QPointF> buildCurrentLevelDownsampledPoints(
        const QVector<float>& source,
        int sampleRate);

    /** @brief 启动降采样处理 */
    Q_INVOKABLE void startDownsamplingProcessing();
    /** @brief 停止降采样处理 */
    Q_INVOKABLE void stopDownsamplingProcessing() { emit processingStop(); }

signals:
    /** @brief 每帧点数发生变化时发出 */
    void pointsPerFrameChanged();
    /** @brief 降采样数据就绪时发出 */
    void downsampledDataReady();
    /** @brief 通知工作线程开始处理 */
    void processingStart();
    /** @brief 通知工作线程停止处理 */
    void processingStop();

private:
    void updatePointsPerFrame(int pointsPerFrame);
    void updateDownsampledData(QVariantList downsampledData);

    int m_pointsPerFrame =
        CURRENT_LEVEL_POINTS_PER_SECOND * PROCESSING_INTERVAL_MS / 1000;
    QVariantList m_downsampledData;
    QThread* m_thread = nullptr;
    DownsamplingWorker* m_worker = nullptr;
};

#endif  // ADAPTIVEDOWNSAMPLING_H
