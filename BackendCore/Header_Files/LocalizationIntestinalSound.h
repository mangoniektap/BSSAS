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

class LocalizationIntestinalSound : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool running READ running NOTIFY runningChanged)
	Q_PROPERTY(bool localizerActive READ localizerActive NOTIFY localizerActiveChanged)

public:
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

	Q_INVOKABLE void startRealtimePipeline(int sampleRate);
	Q_INVOKABLE void stopRealtimePipeline();

	// Entry point for realtime multi-channel frames from acquisition path.
	Q_INVOKABLE void processRealtimeFrame(const QVector<QVector<float>>& channelSamples);

	bool running() const;
	bool localizerActive() const;

signals:
	void intestinalSoundEventDetected(double eventTimeSeconds, double eventScore);
	void localizationUpdated(int bestChannelIndex, double confidence, const QVector<double>& channelScores);
	void localizationDecisionUpdated(const QString& decisionText, double confidence);
	void runningChanged();
	void localizerActiveChanged();

private:
	explicit LocalizationIntestinalSound(QObject* parent = nullptr);
	~LocalizationIntestinalSound() override;

	QVector<float> fuseMiddleChannels(const QVector<QVector<float>>& channelSamples) const;
	QVector<float> fuseVisibleChannels(const QVector<QVector<float>>& channelSamples) const;
	bool detectEvent(const QVector<float>& fusedMiddleSamples, double* eventScore) const;
	void ensureLocalizerCreated();
	void resetLocalizer();
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
	qint64 m_lastEventSampleIndex = -1;

	std::unique_ptr<RealtimeIntestinalSoundEventDetector> m_eventDetector;
	std::unique_ptr<RealtimeIntestinalSoundLocalizer> m_localizer;
};

#endif // LOCALIZATIONINTESTINALSOUND_H
