/** @file AdaptiveNoiseReduction.cpp
 *  @brief WebRTC Audio Processing based adaptive noise reduction implementation.
 */

#include "AdaptiveNoiseReduction.h"

#include "DataManager.h"

#include <QDebug>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace {
constexpr int kMinimumWebRtcSampleRate = 8000;
constexpr int kMaximumWebRtcSampleRate = 384000;
constexpr float kVoltageFullScale = 5.0f;
constexpr float kMinimumVoltageScale = 1e-6f;

std::atomic_bool g_debugLoggingEnabled = true;

AdaptiveNoiseReduction::Parameters normalizedParameters(
    AdaptiveNoiseReduction::Parameters parameters)
{
    parameters.noiseSuppressionLevel = std::clamp(
        parameters.noiseSuppressionLevel,
        static_cast<int>(AdaptiveNoiseReduction::NoiseSuppressionLow),
        static_cast<int>(AdaptiveNoiseReduction::NoiseSuppressionVeryHigh));
    return parameters;
}

bool parametersEqual(
    const AdaptiveNoiseReduction::Parameters& left,
    const AdaptiveNoiseReduction::Parameters& right)
{
    return left.noiseSuppressionLevel == right.noiseSuppressionLevel &&
        left.highPassFilterEnabled == right.highPassFilterEnabled &&
        left.automaticGainControlEnabled == right.automaticGainControlEnabled &&
        left.transientSuppressionEnabled == right.transientSuppressionEnabled;
}

int normalizeSampleRate(int sampleRate)
{
    return sampleRate > 0 ? sampleRate : DataManager::DEFAULT_SAMPLE_RATE;
}

bool isSupportedSampleRate(int sampleRate)
{
    return sampleRate >= kMinimumWebRtcSampleRate &&
        sampleRate <= kMaximumWebRtcSampleRate &&
        webrtc::AudioProcessing::GetFrameSize(sampleRate) > 0;
}

webrtc::AudioProcessing::Config::NoiseSuppression::Level toWebRtcNoiseLevel(
    int level)
{
    using NoiseSuppression = webrtc::AudioProcessing::Config::NoiseSuppression;
    switch (level) {
    case AdaptiveNoiseReduction::NoiseSuppressionLow:
        return NoiseSuppression::kLow;
    case AdaptiveNoiseReduction::NoiseSuppressionHigh:
        return NoiseSuppression::kHigh;
    case AdaptiveNoiseReduction::NoiseSuppressionVeryHigh:
        return NoiseSuppression::kVeryHigh;
    case AdaptiveNoiseReduction::NoiseSuppressionModerate:
    default:
        return NoiseSuppression::kModerate;
    }
}

webrtc::AudioProcessing::Config makeWebRtcConfig(
    const AdaptiveNoiseReduction::Parameters& parameters)
{
    webrtc::AudioProcessing::Config config;
    config.pipeline.maximum_internal_processing_rate = 48000;
    config.high_pass_filter.enabled = parameters.highPassFilterEnabled;
    config.echo_canceller.enabled = false;
    config.noise_suppression.enabled = true;
    config.noise_suppression.level =
        toWebRtcNoiseLevel(parameters.noiseSuppressionLevel);
    config.transient_suppression.enabled =
        parameters.transientSuppressionEnabled;
    config.gain_controller1.enabled = false;
    config.gain_controller2.enabled =
        parameters.automaticGainControlEnabled;
    config.gain_controller2.input_volume_controller.enabled = false;
    config.gain_controller2.adaptive_digital.enabled =
        parameters.automaticGainControlEnabled;
    config.gain_controller2.fixed_digital.gain_db = 0.0f;
    return config;
}

webrtc::ProcessingConfig makeProcessingConfig(int sampleRate)
{
    const webrtc::StreamConfig streamConfig(sampleRate, 1);
    webrtc::ProcessingConfig processingConfig;
    processingConfig.input_stream() = streamConfig;
    processingConfig.output_stream() = streamConfig;
    processingConfig.reverse_input_stream() = streamConfig;
    processingConfig.reverse_output_stream() = streamConfig;
    return processingConfig;
}

bool initializeAudioProcessing(
    AdaptiveNoiseReduction::StreamingState& state,
    int sampleRate,
    const AdaptiveNoiseReduction::Parameters& parameters)
{
    if (!isSupportedSampleRate(sampleRate)) {
        qWarning() << "AdaptiveNoiseReduction: unsupported WebRTC sample rate"
                   << sampleRate;
        AdaptiveNoiseReduction::resetStreamingState(state);
        return false;
    }

    const AdaptiveNoiseReduction::Parameters normalized =
        normalizedParameters(parameters);
    webrtc::AudioProcessing::Config config = makeWebRtcConfig(normalized);
    webrtc::AudioProcessingBuilder builder;
    builder.SetConfig(config);
    webrtc::scoped_refptr<webrtc::AudioProcessing> audioProcessing =
        builder.Create();
    if (!audioProcessing) {
        qWarning() << "AdaptiveNoiseReduction: failed to create WebRTC APM";
        AdaptiveNoiseReduction::resetStreamingState(state);
        return false;
    }

    const int initializeResult =
        audioProcessing->Initialize(makeProcessingConfig(sampleRate));
    if (initializeResult != webrtc::AudioProcessing::kNoError) {
        qWarning() << "AdaptiveNoiseReduction: WebRTC APM initialization failed"
                   << initializeResult;
        AdaptiveNoiseReduction::resetStreamingState(state);
        return false;
    }
    audioProcessing->ApplyConfig(config);

    state.initialized = true;
    state.sampleRate = sampleRate;
    state.parameters = normalized;
    state.audioProcessing = audioProcessing;
    state.pendingInput.clear();
    state.pendingOutput.clear();
    return true;
}

bool ensureAudioProcessing(
    AdaptiveNoiseReduction::StreamingState& state,
    int sampleRate,
    const AdaptiveNoiseReduction::Parameters& parameters)
{
    const AdaptiveNoiseReduction::Parameters normalized =
        normalizedParameters(parameters);
    if (state.initialized &&
        state.sampleRate == sampleRate &&
        parametersEqual(state.parameters, normalized) &&
        state.audioProcessing) {
        return true;
    }

    return initializeAudioProcessing(state, sampleRate, normalized);
}

float finiteSample(float sample)
{
    return std::isfinite(sample) ? sample : 0.0f;
}

float normalizeVoltageSample(float sample)
{
    const float normalized =
        finiteSample(sample) / std::max(kVoltageFullScale, kMinimumVoltageScale);
    return std::clamp(normalized, -1.0f, 1.0f);
}

float restoreVoltageSample(float sample)
{
    const float restored = finiteSample(sample) * kVoltageFullScale;
    return std::isfinite(restored) ? restored : 0.0f;
}

bool processFrame(
    webrtc::AudioProcessing& audioProcessing,
    const QVector<float>& frame,
    int sampleRate,
    QVector<float>& outputFrame)
{
    const int frameSize = webrtc::AudioProcessing::GetFrameSize(sampleRate);
    if (frame.size() != frameSize || frameSize <= 0) {
        return false;
    }

    QVector<float> normalizedFrame(frameSize, 0.0f);
    for (int index = 0; index < frameSize; ++index) {
        normalizedFrame[index] = normalizeVoltageSample(frame[index]);
    }

    QVector<float> processedFrame(frameSize, 0.0f);
    const float* sourceChannels[] = { normalizedFrame.constData() };
    float* destinationChannels[] = { processedFrame.data() };
    const webrtc::StreamConfig streamConfig(sampleRate, 1);
    const int processResult = audioProcessing.ProcessStream(
        sourceChannels,
        streamConfig,
        streamConfig,
        destinationChannels);
    if (processResult != webrtc::AudioProcessing::kNoError) {
        qWarning() << "AdaptiveNoiseReduction: WebRTC ProcessStream failed"
                   << processResult;
        return false;
    }

    outputFrame.resize(frameSize);
    for (int index = 0; index < frameSize; ++index) {
        outputFrame[index] = restoreVoltageSample(processedFrame[index]);
    }
    return true;
}

void appendProcessedFrame(
    AdaptiveNoiseReduction::StreamingState& state,
    const QVector<float>& processedFrame)
{
    state.pendingOutput.reserve(state.pendingOutput.size() + processedFrame.size());
    for (float sample : processedFrame) {
        state.pendingOutput.append(sample);
    }
}

void logDenoisingMetrics(
    const QVector<float>& input,
    const QVector<float>& output,
    const char* label)
{
    if (!g_debugLoggingEnabled.load(std::memory_order_relaxed)) {
        return;
    }

    const int sampleCount = std::min(input.size(), output.size());
    if (sampleCount <= 0) {
        return;
    }

    double sumInputSq = 0.0;
    double sumOutputSq = 0.0;
    double sumDiffSq = 0.0;
    double maxInput = 0.0;
    double maxOutput = 0.0;

    for (int index = 0; index < sampleCount; ++index) {
        const double inputValue = finiteSample(input[index]);
        const double outputValue = finiteSample(output[index]);
        const double diff = inputValue - outputValue;
        sumInputSq += inputValue * inputValue;
        sumOutputSq += outputValue * outputValue;
        sumDiffSq += diff * diff;
        maxInput = std::max(maxInput, std::abs(inputValue));
        maxOutput = std::max(maxOutput, std::abs(outputValue));
    }

    const double inputRms =
        std::sqrt(sumInputSq / static_cast<double>(sampleCount));
    const double outputRms =
        std::sqrt(sumOutputSq / static_cast<double>(sampleCount));
    const double inputRmsDb = 20.0 * std::log10(std::max(inputRms, 1e-12));
    const double outputRmsDb = 20.0 * std::log10(std::max(outputRms, 1e-12));
    const double noiseReductionDb =
        (sumInputSq > 1e-12 && sumDiffSq > 1e-12)
            ? 10.0 * std::log10(sumInputSq / sumDiffSq)
            : 99.0;
    const double signalAttenuationDb =
        10.0 * std::log10(
            std::max(sumOutputSq, 1e-12) / std::max(sumInputSq, 1e-12));
    const double peakRetention =
        maxInput > 1e-12 ? maxOutput / maxInput : 1.0;

    qDebug().nospace()
        << "[" << label << "]"
        << " Input RMS:" << QString::number(inputRmsDb, 'f', 1) << "dB"
        << " Output RMS:" << QString::number(outputRmsDb, 'f', 1) << "dB"
        << " NR:" << QString::number(noiseReductionDb, 'f', 1) << "dB"
        << " Attenuation:" << QString::number(signalAttenuationDb, 'f', 1) << "dB"
        << " PeakRetention:" << QString::number(peakRetention, 'f', 3);
}
} // namespace

AdaptiveNoiseReduction::Parameters AdaptiveNoiseReduction::defaultParameters()
{
    return Parameters{};
}

QVector<float> AdaptiveNoiseReduction::denoise(
    const QVector<float>& input,
    int sampleRate)
{
    return denoise(input, sampleRate, defaultParameters());
}

QVector<float> AdaptiveNoiseReduction::denoise(
    const QVector<float>& input,
    int sampleRate,
    const Parameters& parameters)
{
    if (input.isEmpty()) {
        return {};
    }

    const int effectiveSampleRate = normalizeSampleRate(sampleRate);
    if (!isSupportedSampleRate(effectiveSampleRate)) {
        qWarning() << "AdaptiveNoiseReduction: bypassing unsupported sample rate"
                   << effectiveSampleRate;
        return input;
    }

    StreamingState state;
    if (!initializeAudioProcessing(
            state,
            effectiveSampleRate,
            normalizedParameters(parameters))) {
        return input;
    }

    const int frameSize =
        webrtc::AudioProcessing::GetFrameSize(effectiveSampleRate);
    const int inputSize = static_cast<int>(input.size());
    QVector<float> output;
    output.reserve(inputSize + frameSize);

    const int frameCount = std::max(
        1,
        (inputSize + frameSize - 1) / frameSize);
    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        QVector<float> frame(frameSize, 0.0f);
        const int frameStart = frameIndex * frameSize;
        const int availableCount =
            std::max(0, std::min(frameSize, inputSize - frameStart));
        for (int index = 0; index < availableCount; ++index) {
            frame[index] = input[frameStart + index];
        }
        if (availableCount > 0 && availableCount < frameSize) {
            const float paddingSample = finiteSample(frame[availableCount - 1]);
            for (int index = availableCount; index < frameSize; ++index) {
                frame[index] = paddingSample;
            }
        }

        QVector<float> processedFrame;
        if (!processFrame(
                *state.audioProcessing,
                frame,
                effectiveSampleRate,
                processedFrame)) {
            return input;
        }

        const int appendCount =
            frameIndex == frameCount - 1 && availableCount > 0
                ? availableCount
                : processedFrame.size();
        for (int index = 0; index < appendCount; ++index) {
            output.append(processedFrame[index]);
        }
    }

    if (output.size() > input.size()) {
        output.resize(input.size());
    }

    logDenoisingMetrics(input, output, "WebRtcAdaptiveDenoise");
    return output;
}

QVector<float> AdaptiveNoiseReduction::denoiseStreaming(
    const QVector<float>& input,
    int sampleRate,
    StreamingState& state)
{
    return denoiseStreaming(input, sampleRate, state, defaultParameters());
}

QVector<float> AdaptiveNoiseReduction::denoiseStreaming(
    const QVector<float>& input,
    int sampleRate,
    StreamingState& state,
    const Parameters& parameters)
{
    if (input.isEmpty()) {
        return {};
    }

    const int effectiveSampleRate = normalizeSampleRate(sampleRate);
    if (!ensureAudioProcessing(
            state,
            effectiveSampleRate,
            normalizedParameters(parameters))) {
        return input;
    }

    const int frameSize =
        webrtc::AudioProcessing::GetFrameSize(effectiveSampleRate);
    state.pendingInput.reserve(state.pendingInput.size() + input.size());
    for (float sample : input) {
        state.pendingInput.append(finiteSample(sample));
    }

    while (state.pendingInput.size() >= frameSize) {
        QVector<float> frame(frameSize, 0.0f);
        for (int index = 0; index < frameSize; ++index) {
            frame[index] = state.pendingInput[index];
        }

        QVector<float> processedFrame;
        if (!processFrame(
                *state.audioProcessing,
                frame,
                effectiveSampleRate,
                processedFrame)) {
            return input;
        }

        appendProcessedFrame(state, processedFrame);
        state.pendingInput.erase(
            state.pendingInput.begin(),
            state.pendingInput.begin() + frameSize);
    }

    const int outputCount = std::min(input.size(), state.pendingOutput.size());
    QVector<float> output;
    output.reserve(outputCount);
    for (int index = 0; index < outputCount; ++index) {
        output.append(state.pendingOutput[index]);
    }
    if (outputCount > 0) {
        state.pendingOutput.erase(
            state.pendingOutput.begin(),
            state.pendingOutput.begin() + outputCount);
    }

    logDenoisingMetrics(input, output, "WebRtcAdaptiveDenoise");
    return output;
}

QVector<float> AdaptiveNoiseReduction::flushStreaming(
    int sampleRate,
    StreamingState& state)
{
    return flushStreaming(sampleRate, state, state.parameters);
}

QVector<float> AdaptiveNoiseReduction::flushStreaming(
    int sampleRate,
    StreamingState& state,
    const Parameters& parameters)
{
    const int effectiveSampleRate = normalizeSampleRate(sampleRate);
    QVector<float> output = state.pendingOutput;
    QVector<float> pendingInput = state.pendingInput;
    state.pendingOutput.clear();
    state.pendingInput.clear();

    if (!pendingInput.isEmpty()) {
        if (!ensureAudioProcessing(
                state,
                effectiveSampleRate,
                normalizedParameters(parameters))) {
            output += pendingInput;
            return output;
        }

        const int pendingCount = pendingInput.size();
        const int frameSize =
            webrtc::AudioProcessing::GetFrameSize(effectiveSampleRate);
        QVector<float> frame(frameSize, 0.0f);
        for (int index = 0; index < pendingCount && index < frameSize; ++index) {
            frame[index] = pendingInput[index];
        }
        const float paddingSample =
            pendingCount > 0 ? finiteSample(pendingInput[pendingCount - 1]) : 0.0f;
        for (int index = pendingCount; index < frameSize; ++index) {
            frame[index] = paddingSample;
        }

        QVector<float> processedFrame;
        if (processFrame(
                *state.audioProcessing,
                frame,
                effectiveSampleRate,
                processedFrame)) {
            for (int index = 0; index < pendingCount; ++index) {
                output.append(processedFrame[index]);
            }
        } else {
            output += pendingInput;
        }
    }

    return output;
}

void AdaptiveNoiseReduction::resetStreamingState(StreamingState& state)
{
    state = StreamingState{};
}

void AdaptiveNoiseReduction::setDebugLoggingEnabled(bool enabled)
{
    g_debugLoggingEnabled.store(enabled, std::memory_order_relaxed);
}

bool AdaptiveNoiseReduction::debugLoggingEnabled()
{
    return g_debugLoggingEnabled.load(std::memory_order_relaxed);
}
