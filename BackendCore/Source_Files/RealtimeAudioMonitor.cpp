/** @file RealtimeAudioMonitor.cpp
 *  @brief 实时音频监听器实现。
 */

#include "RealtimeAudioMonitor.h"

#include "DaqDeviceManager.h"
#include "DataManager.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QDebug>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float kMaximumVoltage = 5.0f;
constexpr int kFallbackSampleRate = 48000;
constexpr int kFallbackChannelCount = 1;
constexpr int kOutputBufferMilliseconds = 300;
constexpr double kMinimumVolumeDelta = 0.001;

double normalizeVolume(double volume)
{
    if (!std::isfinite(volume)) {
        return 0.0;
    }
    return std::clamp(volume, 0.0, 1.0);
}

qsizetype sampleFormatByteCount(QAudioFormat::SampleFormat sampleFormat)
{
    switch (sampleFormat) {
    case QAudioFormat::UInt8:
        return static_cast<qsizetype>(sizeof(quint8));
    case QAudioFormat::Int16:
        return static_cast<qsizetype>(sizeof(qint16));
    case QAudioFormat::Int32:
        return static_cast<qsizetype>(sizeof(qint32));
    case QAudioFormat::Float:
        return static_cast<qsizetype>(sizeof(float));
    case QAudioFormat::Unknown:
        break;
    }

    return static_cast<qsizetype>(sizeof(qint16));
}
} // namespace

RealtimeAudioMonitor::RealtimeAudioMonitor(DaqDeviceManager* daqManager, QObject* parent)
    : QObject(parent)
    , m_daqManager(daqManager)
{
    if (m_daqManager == nullptr) {
        qWarning() << "RealtimeAudioMonitor: DAQ manager is null";
        return;
    }

    connect(
        m_daqManager,
        &DaqDeviceManager::realtimeDataUpdated,
        this,
        &RealtimeAudioMonitor::handleRealtimeDataUpdated);
    connect(
        m_daqManager,
        &DaqDeviceManager::readingChanged,
        this,
        &RealtimeAudioMonitor::handleDaqStateChanged);
    connect(
        m_daqManager,
        &DaqDeviceManager::collectionChanged,
        this,
        &RealtimeAudioMonitor::handleDaqStateChanged);
}

RealtimeAudioMonitor::~RealtimeAudioMonitor()
{
    stopAudioOutput();
}

void RealtimeAudioMonitor::setEnabled(bool enabled)
{
    const bool normalizedEnabled = enabled && m_daqManager != nullptr;
    if (m_enabled == normalizedEnabled) {
        return;
    }

    m_enabled = normalizedEnabled;
    if (!m_enabled) {
        stopAudioOutput();
    } else if (m_daqManager->isReading()) {
        startAudioOutput(DataManager::instance()->configuredSampleRate());
    }

    emit enabledChanged();
}

void RealtimeAudioMonitor::setVolume(double volume)
{
    const double normalized = normalizeVolume(volume);
    if (std::abs(m_volume - normalized) <= kMinimumVolumeDelta) {
        return;
    }

    m_volume = normalized;
    if (m_audioSink != nullptr) {
        m_audioSink->setVolume(m_volume);
    }

    emit volumeChanged();
}

void RealtimeAudioMonitor::adjustVolume(double delta)
{
    setVolume(m_volume + delta);
}

void RealtimeAudioMonitor::setChannelIndex(int channelIndex)
{
    const int normalizedChannelIndex = std::max(0, channelIndex);
    if (m_channelIndex == normalizedChannelIndex) {
        return;
    }

    m_channelIndex = normalizedChannelIndex;
    if (m_audioSink != nullptr) {
        m_audioSink->reset();
        m_audioOutput = m_audioSink->start();
        if (m_audioOutput == nullptr) {
            qWarning() << "RealtimeAudioMonitor: failed to restart audio sink after channel change";
            stopAudioOutput();
        }
    }

    emit channelIndexChanged();
}

void RealtimeAudioMonitor::handleRealtimeDataUpdated(
    const QVector<QVector<float>>& channelsData)
{
    if (!m_enabled ||
        m_daqManager == nullptr ||
        !m_daqManager->isReading() ||
        m_channelIndex < 0 ||
        m_channelIndex >= channelsData.size()) {
        return;
    }

    const QVector<float>& channelSamples = channelsData[m_channelIndex];
    if (channelSamples.isEmpty()) {
        return;
    }

    const int inputSampleRate = DataManager::instance()->configuredSampleRate();
    startAudioOutput(inputSampleRate);
    if (m_audioSink == nullptr || m_audioOutput == nullptr) {
        return;
    }

    const QByteArray audioBuffer = buildAudioBuffer(channelSamples, inputSampleRate);
    if (audioBuffer.isEmpty()) {
        return;
    }

    if (m_audioSink->bytesFree() < audioBuffer.size()) {
        m_audioSink->reset();
        m_audioOutput = m_audioSink->start();
    }

    if (m_audioOutput == nullptr) {
        return;
    }

    const qint64 writableBytes =
        std::min<qint64>(audioBuffer.size(), m_audioSink->bytesFree());
    if (writableBytes <= 0) {
        return;
    }

    const qint64 writtenBytes = m_audioOutput->write(audioBuffer.constData(), writableBytes);
    if (writtenBytes < 0) {
        qWarning() << "RealtimeAudioMonitor: failed to write audio buffer";
    }
}

void RealtimeAudioMonitor::handleDaqStateChanged()
{
    if (!m_enabled || m_daqManager == nullptr) {
        return;
    }

    if (!m_daqManager->isCollecting() || !m_daqManager->isReading()) {
        setEnabled(false);
        return;
    }

    startAudioOutput(DataManager::instance()->configuredSampleRate());
}

void RealtimeAudioMonitor::startAudioOutput(int inputSampleRate)
{
    const int effectiveInputSampleRate =
        inputSampleRate > 0 ? inputSampleRate : DataManager::DEFAULT_SAMPLE_RATE;
    if (m_audioSink != nullptr && m_inputSampleRate == effectiveInputSampleRate) {
        return;
    }

    stopAudioOutput();

    const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    if (outputDevice.isNull()) {
        qWarning() << "RealtimeAudioMonitor: no default audio output device";
        return;
    }

    m_audioFormat = outputDevice.preferredFormat();
    if (!m_audioFormat.isValid()) {
        m_audioFormat.setSampleRate(kFallbackSampleRate);
        m_audioFormat.setChannelCount(kFallbackChannelCount);
        m_audioFormat.setSampleFormat(QAudioFormat::Int16);
    }
    if (m_audioFormat.sampleRate() <= 0) {
        m_audioFormat.setSampleRate(kFallbackSampleRate);
    }
    if (m_audioFormat.channelCount() <= 0) {
        m_audioFormat.setChannelCount(kFallbackChannelCount);
    }
    if (m_audioFormat.sampleFormat() == QAudioFormat::Unknown) {
        m_audioFormat.setSampleFormat(QAudioFormat::Int16);
    }

    m_audioSink = new QAudioSink(outputDevice, m_audioFormat, this);
    const qsizetype bytesPerFrame = m_audioFormat.bytesPerFrame();
    if (bytesPerFrame > 0) {
        m_audioSink->setBufferSize(
            static_cast<qsizetype>(
                (static_cast<qint64>(m_audioFormat.sampleRate()) *
                 bytesPerFrame *
                 kOutputBufferMilliseconds) /
                1000));
    }
    m_audioSink->setVolume(m_volume);
    m_audioOutput = m_audioSink->start();
    if (m_audioOutput == nullptr) {
        qWarning() << "RealtimeAudioMonitor: failed to start audio sink";
        stopAudioOutput();
        return;
    }

    m_inputSampleRate = effectiveInputSampleRate;
}

void RealtimeAudioMonitor::stopAudioOutput()
{
    if (m_audioSink != nullptr) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
    }

    m_audioOutput = nullptr;
    m_inputSampleRate = 0;
}

QByteArray RealtimeAudioMonitor::buildAudioBuffer(
    const QVector<float>& inputSamples,
    int inputSampleRate) const
{
    if (m_audioFormat.sampleRate() <= 0 || m_audioFormat.channelCount() <= 0) {
        return {};
    }

    const QVector<float> outputSamples = resampleToOutputRate(inputSamples, inputSampleRate);
    if (outputSamples.isEmpty()) {
        return {};
    }

    const int outputChannelCount = std::max(1, m_audioFormat.channelCount());
    const qsizetype bytesPerSample = sampleFormatByteCount(m_audioFormat.sampleFormat());
    QByteArray buffer;
    buffer.resize(outputSamples.size() * outputChannelCount * bytesPerSample);

    char* writeCursor = buffer.data();
    for (float sample : outputSamples) {
        const float normalizedSample = normalizeVoltage(sample);
        for (int channel = 0; channel < outputChannelCount; ++channel) {
            switch (m_audioFormat.sampleFormat()) {
            case QAudioFormat::UInt8: {
                const quint8 value = static_cast<quint8>(
                    std::lround((normalizedSample * 0.5f + 0.5f) * 255.0f));
                std::memcpy(writeCursor, &value, sizeof(value));
                writeCursor += sizeof(value);
                break;
            }
            case QAudioFormat::Int16: {
                const qint16 value = static_cast<qint16>(
                    std::lround(normalizedSample * 32767.0f));
                std::memcpy(writeCursor, &value, sizeof(value));
                writeCursor += sizeof(value);
                break;
            }
            case QAudioFormat::Int32: {
                const qint32 value = static_cast<qint32>(
                    std::llround(normalizedSample * 2147483647.0f));
                std::memcpy(writeCursor, &value, sizeof(value));
                writeCursor += sizeof(value);
                break;
            }
            case QAudioFormat::Float: {
                std::memcpy(writeCursor, &normalizedSample, sizeof(normalizedSample));
                writeCursor += sizeof(normalizedSample);
                break;
            }
            case QAudioFormat::Unknown:
                break;
            }
        }
    }

    return buffer;
}

QVector<float> RealtimeAudioMonitor::resampleToOutputRate(
    const QVector<float>& inputSamples,
    int inputSampleRate) const
{
    const int effectiveInputSampleRate =
        inputSampleRate > 0 ? inputSampleRate : DataManager::DEFAULT_SAMPLE_RATE;
    const int outputSampleRate = m_audioFormat.sampleRate();
    if (inputSamples.isEmpty() || outputSampleRate <= 0) {
        return {};
    }

    if (effectiveInputSampleRate == outputSampleRate) {
        return inputSamples;
    }

    const qsizetype outputSampleCount = std::max<qsizetype>(
        1,
        static_cast<qsizetype>(
            std::llround(
                static_cast<double>(inputSamples.size()) *
                static_cast<double>(outputSampleRate) /
                static_cast<double>(effectiveInputSampleRate))));
    QVector<float> outputSamples;
    outputSamples.resize(outputSampleCount);

    const double inputStep =
        static_cast<double>(effectiveInputSampleRate) /
        static_cast<double>(outputSampleRate);
    const qsizetype lastInputIndex = inputSamples.size() - 1;
    for (qsizetype outputIndex = 0; outputIndex < outputSampleCount; ++outputIndex) {
        const double inputPosition = static_cast<double>(outputIndex) * inputStep;
        const qsizetype lowerIndex = std::clamp<qsizetype>(
            static_cast<qsizetype>(std::floor(inputPosition)),
            0,
            lastInputIndex);
        const qsizetype upperIndex = std::min(lowerIndex + 1, lastInputIndex);
        const double fraction = inputPosition - static_cast<double>(lowerIndex);
        outputSamples[outputIndex] = static_cast<float>(
            inputSamples[lowerIndex] +
            (inputSamples[upperIndex] - inputSamples[lowerIndex]) * fraction);
    }

    return outputSamples;
}

float RealtimeAudioMonitor::normalizeVoltage(float voltage) const
{
    if (!std::isfinite(voltage)) {
        return 0.0f;
    }

    return std::clamp(voltage / kMaximumVoltage, -1.0f, 1.0f);
}
