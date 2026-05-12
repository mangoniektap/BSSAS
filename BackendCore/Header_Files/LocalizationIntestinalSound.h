/** @file LocalizationIntestinalSound.h
 *  @brief 肠鸣音定位 —— 多通道肠鸣音事件检测与声源定位
 *
 *  本模块实现了实时多通道肠鸣音的事件检测和声源定位管线。
 *  处理流程包括：
 *  - 多通道信号融合（中间通道融合 + 可视化通道融合）
 *  - 基于融合信号的肠鸣音事件检测（打分）
 *  - 检测到事件后触发多通道定位器（估算最佳通道与置信度）
 *
 *  通过信号将检测到的事件时间/分数和定位结果通知前端。
 */

#ifndef LOCALIZATIONINTESTINALSOUND_H
#define LOCALIZATIONINTESTINALSOUND_H

#include <QCoreApplication>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

class RealtimeIntestinalSoundEventDetector;
class RealtimeIntestinalSoundLocalizer;

/** @brief 肠鸣音定位管理器（单例）
 *
 *  管理实时肠鸣音事件检测和声源定位管线，
 *  接收多通道原始帧并输出检测和定位结果。
 */
class LocalizationIntestinalSound : public QObject
{
	Q_OBJECT
	/** @brief 实时管线是否正在运行 */
	Q_PROPERTY(bool running READ running NOTIFY runningChanged)
	/** @brief 定位器是否已激活 */
	Q_PROPERTY(bool localizerActive READ localizerActive NOTIFY localizerActiveChanged)

public:
	/** @brief 获取全局单例实例（双重检查锁定线程安全） */
	static LocalizationIntestinalSound* instance()
	{
		if (m_instance == nullptr) {
			static QMutex mutex;
			QMutexLocker locker(&mutex);
			if (m_instance == nullptr) {
				m_instance = new LocalizationIntestinalSound(qApp);
			}
		}
		return m_instance;
	}

	/** @brief 启动实时处理管线
	 *  @param sampleRate 采样率 (Hz)
	 */
	Q_INVOKABLE void startRealtimePipeline(int sampleRate);
	/** @brief 停止实时处理管线 */
	Q_INVOKABLE void stopRealtimePipeline();

	/** @brief 实时多通道帧处理入口（来自采集路径）
	 *  @param channelSamples 二维向量 [通道][样本]
	 */
	Q_INVOKABLE void processRealtimeFrame(const QVector<QVector<float>>& channelSamples);

	/** @brief 查询管线是否正在运行 */
	bool running() const;
	/** @brief 查询定位器是否已激活 */
	bool localizerActive() const;

signals:
	/** @brief 检测到肠鸣音事件时发出
	 *  @param eventTimeSeconds 事件发生的时刻 (秒)
	 *  @param eventScore 事件置信度分数
	 */
	void intestinalSoundEventDetected(double eventTimeSeconds, double eventScore);
	/** @brief 定位结果更新
	 *  @param bestChannelIndex 最佳通道索引
	 *  @param confidence 置信度
	 *  @param channelScores 各通道分数
	 */
	void localizationUpdated(int bestChannelIndex, double confidence, const QVector<double>& channelScores);
	/** @brief 定位决策文本更新
	 *  @param decisionText 决策描述文本
	 *  @param confidence 置信度
	 */
	void localizationDecisionUpdated(const QString& decisionText, double confidence);
	/** @brief 管线运行状态变更 */
	void runningChanged();
	/** @brief 定位器激活状态变更 */
	void localizerActiveChanged();

private:
	explicit LocalizationIntestinalSound(QObject* parent = nullptr);
	~LocalizationIntestinalSound() override;

	/** @brief 融合中间通道样本用于事件检测 */
	QVector<float> fuseMiddleChannels(const QVector<QVector<float>>& channelSamples) const;
	/** @brief 融合所有可见通道样本用于定位 */
	QVector<float> fuseVisibleChannels(const QVector<QVector<float>>& channelSamples) const;
	/** @brief 检测融合信号中是否存在肠鸣音事件
	 *  @param fusedMiddleSamples 中间通道融合信号
	 *  @param eventScore 输出事件分数
	 *  @returns 检测到事件返回 true
	 */
	bool detectEvent(const QVector<float>& fusedMiddleSamples, double* eventScore) const;
	/** @brief 确保定位器已创建 */
	void ensureLocalizerCreated();
	/** @brief 重置定位器状态 */
	void resetLocalizer();
	/** @brief 执行多通道声源定位
	 *  @param channelSamples 所有通道样本
	 *  @param bestChannelIndex 输出最佳通道索引
	 *  @param confidence 输出置信度
	 *  @param channelScores 输出各通道分数
	 *  @param decisionText 输出决策文本
	 */
	void runLocalization(
		const QVector<QVector<float>>& channelSamples,
		int* bestChannelIndex,
		double* confidence,
		QVector<double>* channelScores,
		QString* decisionText) const;

	static LocalizationIntestinalSound* m_instance;

	mutable QMutex m_stateMutex;
	bool m_running = false;
	bool m_localizerActive = false;
	int m_sampleRate = 0;
	qint64 m_processedSamples = 0;
	/** @brief 上一次检测到事件的样本索引，用于防止相邻帧重复触发 */
	qint64 m_lastEventSampleIndex = -1;

	std::unique_ptr<RealtimeIntestinalSoundEventDetector> m_eventDetector;
	std::unique_ptr<RealtimeIntestinalSoundLocalizer> m_localizer;
};

#endif // LOCALIZATIONINTESTINALSOUND_H
