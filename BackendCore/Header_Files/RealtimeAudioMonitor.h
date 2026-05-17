/** @file RealtimeAudioMonitor.h
 *  @brief 实时音频监听器，将 DAQ 通道一数据输出到系统默认播放设备。
 */

#ifndef REALTIMEAUDIOMONITOR_H
#define REALTIMEAUDIOMONITOR_H

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QVector>

class DaqDeviceManager;
class QAudioSink;
class QIODevice;

/** @brief 实时监听管理器，供 QML 控制监听开关与音量。 */
class RealtimeAudioMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(int channelIndex READ channelIndex CONSTANT)

public:
    explicit RealtimeAudioMonitor(DaqDeviceManager* daqManager, QObject* parent = nullptr);
    ~RealtimeAudioMonitor() override;

    bool enabled() const { return m_enabled; }
    double volume() const { return m_volume; }
    int channelIndex() const { return m_channelIndex; }

    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void adjustVolume(double delta);

signals:
    void enabledChanged();
    void volumeChanged();

private slots:
    void handleRealtimeDataUpdated(const QVector<QVector<float>>& channelsData);
    void handleDaqStateChanged();

private:
    void startAudioOutput(int inputSampleRate);
    void stopAudioOutput();
    QByteArray buildAudioBuffer(const QVector<float>& inputSamples, int inputSampleRate) const;
    QVector<float> resampleToOutputRate(
        const QVector<float>& inputSamples,
        int inputSampleRate) const;
    float normalizeVoltage(float voltage) const;

    DaqDeviceManager* m_daqManager = nullptr;
    bool m_enabled = false;
    double m_volume = 0.6;
    int m_channelIndex = 0;
    int m_inputSampleRate = 0;
    QAudioFormat m_audioFormat;
    QAudioSink* m_audioSink = nullptr;
    QIODevice* m_audioOutput = nullptr;
};

#endif // REALTIMEAUDIOMONITOR_H
