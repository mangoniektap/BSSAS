/**
 * @file WAVHandle.cpp
 * @brief WAV文件导入/导出处理模块，通过内嵌Python脚本实现Float32-缓存到WAV格式的双向转换；支持实时多通道批量保存与离线单通道导入。
 */

#include "WAVHandle.h"

#include "DatabaseStoragePaths.h"
#include "DataManager.h"
#include "DaqDeviceManager.h"
#include "MotionArtifactReduction.h"
#include "SignalPreprocessing.h"
#include "TransientNoiseSuppressor.h"
#include "WaveletTransform.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QTemporaryFile>
#include <QUrl>
#include <QVersionNumber>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace {
constexpr int kWavExportTimeoutMs = 10000;
constexpr int kWavImportTimeoutMs = 600000;
constexpr int kRealtimeVisibleChannelCount = 7;
constexpr int kRealtimeReferenceNoiseChannel = 7;
constexpr qsizetype kExportInterleavedFramesPerWrite = 4096;

struct WavExportResult
{
    bool succeeded = false;
    QString outputPath;
    QString errorMessage;
};

QString resolveBundledPythonExecutable()
{
    const QStringList runtimeRootCandidates {
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("PythonModules/Runtime")),
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("../PythonModules/Runtime")),
        QDir::current().filePath(QStringLiteral("PythonModules/Runtime"))
    };

    const QRegularExpression runtimeDirPattern(
        QStringLiteral(
            "^Python-(\\d+\\.\\d+\\.\\d+)-embed-(amd64|x64|arm64|x86)$"),
        QRegularExpression::CaseInsensitiveOption);

    QString selectedPythonPath;
    QVersionNumber selectedVersion(-1, -1, -1);

    for (const QString& runtimeRootPath : runtimeRootCandidates) {
        const QDir runtimeRoot(runtimeRootPath);
        if (!runtimeRoot.exists()) {
            continue;
        }

        const QString directPythonPath =
            runtimeRoot.filePath(QStringLiteral("python.exe"));
        if (QFileInfo::exists(directPythonPath)) {
            return QFileInfo(directPythonPath).absoluteFilePath();
        }

        const QFileInfoList runtimeCandidates = runtimeRoot.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name);
        for (const QFileInfo& runtimeCandidate : runtimeCandidates) {
            const QRegularExpressionMatch match =
                runtimeDirPattern.match(runtimeCandidate.fileName());
            if (!match.hasMatch()) {
                continue;
            }

            const QVersionNumber currentVersion =
                QVersionNumber::fromString(match.captured(1));
            if (currentVersion.isNull()) {
                continue;
            }

            const QString pythonExecutablePath =
                QDir(runtimeCandidate.absoluteFilePath())
                    .filePath(QStringLiteral("python.exe"));
            if (!QFileInfo::exists(pythonExecutablePath)) {
                continue;
            }

            if (selectedPythonPath.isEmpty() ||
                QVersionNumber::compare(currentVersion, selectedVersion) > 0) {
                selectedVersion = currentVersion;
                selectedPythonPath =
                    QFileInfo(pythonExecutablePath).absoluteFilePath();
            }
        }
    }

    return selectedPythonPath;
}

QString resolvePythonExecutable()
{
    return resolveBundledPythonExecutable();
}

int resolveExportSampleRate(int primarySampleRate, int fallbackSampleRate)
{
    if (primarySampleRate > 0) {
        return primarySampleRate;
    }

    if (fallbackSampleRate > 0) {
        return fallbackSampleRate;
    }

    return DataManager::DEFAULT_SAMPLE_RATE;
}

QString parseExportedWavPath(const QString& processOutput)
{
    const QString successPrefix = QStringLiteral("Success: Saved to ");
    const QStringList outputLines =
        processOutput.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                            Qt::SkipEmptyParts);

    for (auto iterator = outputLines.crbegin(); iterator != outputLines.crend(); ++iterator) {
        const QString line = iterator->trimmed();
        if (line.startsWith(successPrefix, Qt::CaseSensitive)) {
            return line.mid(successPrefix.size()).trimmed();
        }
    }

    return {};
}

QString resolvePythonScriptPath(const QString& scriptFileName)
{
    const QStringList candidatePaths {
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("PythonModules/Scripts/") + scriptFileName),
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("../PythonModules/Scripts/") + scriptFileName),
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("PythonModules/") + scriptFileName),
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("../PythonModules/") + scriptFileName),
        QDir::current().filePath(
            QStringLiteral("PythonModules/Scripts/") + scriptFileName),
        QDir::current().filePath(QStringLiteral("PythonModules/") + scriptFileName)
    };

    for (const QString& candidatePath : candidatePaths) {
        if (QFileInfo::exists(candidatePath)) {
            return QFileInfo(candidatePath).absoluteFilePath();
        }
    }

    return candidatePaths.constFirst();
}

double elapsedMilliseconds(const QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
}

QString normalizeLocalFilePath(const QString& pathOrUrl)
{
    QString normalizedPath = pathOrUrl.trimmed();
    if (normalizedPath.isEmpty()) {
        return {};
    }

    if (normalizedPath.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl fileUrl(normalizedPath);
        if (fileUrl.isValid() && fileUrl.isLocalFile()) {
            const QString localPath = fileUrl.toLocalFile();
            if (!localPath.isEmpty()) {
                return QDir::toNativeSeparators(localPath);
            }
        }
    }

    if (normalizedPath.contains(QLatin1Char('%'))) {
        const QString decodedPath =
            QUrl::fromPercentEncoding(normalizedPath.toUtf8());
        if (!decodedPath.isEmpty() && decodedPath != normalizedPath) {
            const QFileInfo originalInfo(normalizedPath);
            const QFileInfo decodedInfo(decodedPath);
            if (decodedInfo.exists() || !originalInfo.exists()) {
                normalizedPath = decodedPath;
            }
        }
    }

    return QDir::toNativeSeparators(normalizedPath);
}

QVector<float> readFloatCacheSamples(const QString& temporaryFloatFilePath)
{
    if (temporaryFloatFilePath.isEmpty() || !QFileInfo::exists(temporaryFloatFilePath)) {
        return {};
    }

    QFile temporaryFile(temporaryFloatFilePath);
    if (!temporaryFile.open(QIODevice::ReadOnly)) {
        qWarning() << "WAVHandle: failed to open realtime channel cache"
                   << temporaryFloatFilePath;
        return {};
    }

    if (temporaryFile.size() <= static_cast<qint64>(sizeof(float))) {
        return {};
    }

    if (!temporaryFile.seek(static_cast<qint64>(sizeof(float)))) {
        qWarning() << "WAVHandle: failed to skip sample-rate header"
                   << temporaryFloatFilePath;
        return {};
    }

    QByteArray sampleBytes = temporaryFile.readAll();
    const qsizetype alignedByteCount =
        sampleBytes.size() - (sampleBytes.size() % static_cast<qsizetype>(sizeof(float)));
    if (alignedByteCount <= 0) {
        return {};
    }

    QVector<float> samples(alignedByteCount / static_cast<qsizetype>(sizeof(float)));
    std::memcpy(
        samples.data(),
        sampleBytes.constData(),
        static_cast<size_t>(alignedByteCount));
    return samples;
}

QVector<float> applyRealtimeSavePostProcessing(
    QVector<float> channelSamples,
    int sampleRate,
    bool waveletDenoisingEnabled,
    bool transientNoiseSuppressionEnabled,
    bool motionArtifactReductionEnabled)
{
    const int effectiveSampleRate =
        sampleRate > 0 ? sampleRate : DataManager::DEFAULT_SAMPLE_RATE;

    if (waveletDenoisingEnabled) {
        channelSamples = WaveletTransform::denoise(channelSamples, effectiveSampleRate);
    }

    if (transientNoiseSuppressionEnabled) {
        channelSamples = TransientNoiseSuppressor::suppress(channelSamples, effectiveSampleRate);
    }

    if (motionArtifactReductionEnabled) {
        channelSamples = MotionArtifactReduction::reduce(channelSamples, effectiveSampleRate);
    }

    return channelSamples;
}

bool writeMultichannelFloatCache(
    const QVector<QVector<float>>& channelSamples,
    int sampleRate,
    QTemporaryFile& temporaryFloatFile)
{
    if (channelSamples.isEmpty()) {
        return false;
    }

    qsizetype maximumSampleCount = 0;
    for (const QVector<float>& samples : channelSamples) {
        maximumSampleCount = std::max(maximumSampleCount, samples.size());
    }

    if (maximumSampleCount <= 0) {
        return false;
    }

    const float sampleRateHeader = static_cast<float>(
        sampleRate > 0 ? sampleRate : DataManager::DEFAULT_SAMPLE_RATE);
    if (temporaryFloatFile.write(
            reinterpret_cast<const char*>(&sampleRateHeader),
            static_cast<qint64>(sizeof(sampleRateHeader))) !=
        static_cast<qint64>(sizeof(sampleRateHeader))) {
        return false;
    }

    const qsizetype channelCount = channelSamples.size();
    QVector<float> interleavedBuffer;
    interleavedBuffer.reserve(kExportInterleavedFramesPerWrite * channelCount);

    qsizetype frameIndex = 0;
    while (frameIndex < maximumSampleCount) {
        const qsizetype framesThisWrite = std::min(
            kExportInterleavedFramesPerWrite,
            maximumSampleCount - frameIndex);
        interleavedBuffer.clear();

        for (qsizetype frameOffset = 0; frameOffset < framesThisWrite; ++frameOffset) {
            const qsizetype sampleIndex = frameIndex + frameOffset;
            for (const QVector<float>& samples : channelSamples) {
                interleavedBuffer.append(
                    sampleIndex < samples.size() ? samples[sampleIndex] : 0.0f);
            }
        }

        const qint64 bytesToWrite =
            static_cast<qint64>(interleavedBuffer.size() * sizeof(float));
        if (temporaryFloatFile.write(
                reinterpret_cast<const char*>(interleavedBuffer.constData()),
                bytesToWrite) != bytesToWrite) {
            return false;
        }

        frameIndex += framesThisWrite;
    }

    return temporaryFloatFile.flush();
}

WavExportResult runWavExport(
    const QString& temporaryFloatFilePath,
    int sampleRate,
    int channelCount,
    const QString& pythonScriptPath,
    const QString& outputDirectoryPath = QString(),
    const QString& outputBaseName = QString())
{
    if (temporaryFloatFilePath.isEmpty() || !QFile::exists(temporaryFloatFilePath)) {
        qCritical() << "WAVHandle: temporary float cache file does not exist:"
                    << temporaryFloatFilePath;
        return {
            false,
            {},
            QStringLiteral("临时缓存文件不存在，无法导出 WAV")
        };
    }

    if (pythonScriptPath.isEmpty() || !QFileInfo::exists(pythonScriptPath)) {
        qCritical() << "WAVHandle: python export script does not exist:" << pythonScriptPath;
        return {
            false,
            {},
            QStringLiteral("WAV 导出脚本不存在: %1").arg(pythonScriptPath)
        };
    }

    const QString pythonExecutablePath = resolvePythonExecutable();
    if (pythonExecutablePath.isEmpty() || !QFileInfo::exists(pythonExecutablePath)) {
        return {
            false,
            {},
            QStringLiteral("未找到内置 Python 运行时，请检查 PythonModules/Runtime 目录")
        };
    }

    const int effectiveChannelCount = std::max(1, channelCount);
    const QString effectiveOutputDirectoryPath =
        outputDirectoryPath.trimmed().isEmpty()
            ? DatabaseStoragePaths::AUDIO_PATH
            : QDir::toNativeSeparators(outputDirectoryPath.trimmed());
    QStringList commandLineArguments;
    commandLineArguments
        << QStringLiteral("-B")
        << pythonScriptPath
        << temporaryFloatFilePath
        << effectiveOutputDirectoryPath
        << QString::number(sampleRate);
    if (effectiveChannelCount > 1 || !outputBaseName.trimmed().isEmpty()) {
        commandLineArguments << QString::number(effectiveChannelCount);
    }
    if (!outputBaseName.trimmed().isEmpty()) {
        commandLineArguments << outputBaseName.trimmed();
    }

    QProcess process;
    process.setWorkingDirectory(QCoreApplication::applicationDirPath());
    process.start(pythonExecutablePath, commandLineArguments);

    if (!process.waitForFinished(kWavExportTimeoutMs)) {
        qCritical() << "Python script timed out or failed:" << process.errorString();
        return {
            false,
            {},
            QStringLiteral("WAV 导出超时或失败: %1").arg(process.errorString())
        };
    }

    if (process.exitCode() != 0) {
        qCritical() << "Python script returned an error:";
        const QString errorText =
            QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        qCritical() << errorText;
        return {
            false,
            {},
            errorText.isEmpty()
                ? QStringLiteral("WAV 导出失败")
                : QStringLiteral("WAV 导出失败: %1").arg(errorText)
        };
    }

    const QString processOutput =
        QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    const QString outputPath = parseExportedWavPath(processOutput);
    return {
        true,
        outputPath.isEmpty() ? DatabaseStoragePaths::AUDIO_PATH : outputPath,
        {}
    };
}
} // namespace

WAVHandle* WAVHandle::m_instance = nullptr;

WAVHandle::WAVHandle(QObject* parent)
    : QObject(parent)
    , m_importSampleRate(DataManager::DEFAULT_SAMPLE_RATE)
{
}

WAVHandle::~WAVHandle() = default;

void WAVHandle::setReferenceNoiseChannelSaveEnabled(bool enabled)
{
    if (m_referenceNoiseChannelSaveEnabled == enabled) {
        return;
    }

    m_referenceNoiseChannelSaveEnabled = enabled;
    emit referenceNoiseChannelSaveEnabledChanged();
}

/**
 * @brief 启动实时多通道数据导出为WAV文件，在后台线程中对选定通道应用降噪后处理后写入。
 */
void WAVHandle::startSaveAsWav()
{
    startSaveAsWav(QString(), QString());
}

void WAVHandle::startSaveAsWav(
    const QString& outputDirectoryPath,
    const QString& outputBaseName)
{
    DataManager* realtimeDataManager = DataManager::instance();
    DaqDeviceManager* realtimeDaqManager = DaqDeviceManager::instance();

    if (realtimeDaqManager->isReading()) {
        emit saveFailed(QStringLiteral("请先停止采集后再保存数据"));
        return;
    }

    if (!realtimeDataManager->realtimeCollectionAvailable()) {
        emit saveFailed(QStringLiteral("当前没有已完成的实时采集数据"));
        return;
    }

    QVector<QString> channelTemporaryFilePaths;
    channelTemporaryFilePaths.reserve(kRealtimeVisibleChannelCount + 1);

    int realtimeSampleRate = realtimeDataManager->configuredSampleRate();
    for (int channel = 0; channel < kRealtimeVisibleChannelCount; ++channel) {
        if (!realtimeDaqManager->isChannelActive(channel)) {
            continue;
        }

        channelTemporaryFilePaths.append(
            realtimeDataManager->realtimeChannelTemporaryFilePath(channel));
        const int channelSampleRate =
            realtimeDataManager->realtimeWaveformSampleRateForChannel(channel);
        if (channelSampleRate > 0) {
            realtimeSampleRate = channelSampleRate;
        }
    }

    if (channelTemporaryFilePaths.isEmpty()) {
        qWarning() << "WAVHandle: no active realtime channels available for export";
        emit saveFailed(QStringLiteral("当前没有可导出的激活通道数据"));
        return;
    }

    const int postProcessedChannelCount = channelTemporaryFilePaths.size();
    if (m_referenceNoiseChannelSaveEnabled) {
        const QString referenceNoiseTemporaryFilePath =
            realtimeDataManager->realtimeChannelTemporaryFilePath(
                kRealtimeReferenceNoiseChannel);
        const QFileInfo referenceNoiseFile(referenceNoiseTemporaryFilePath);
        if (referenceNoiseTemporaryFilePath.isEmpty() ||
            !referenceNoiseFile.exists() ||
            referenceNoiseFile.size() <= static_cast<qint64>(sizeof(float))) {
            qWarning() << "WAVHandle: reference noise channel cache is unavailable";
            emit saveFailed(QStringLiteral("Reference noise channel has no cached samples"));
            return;
        }

        channelTemporaryFilePaths.append(referenceNoiseTemporaryFilePath);
        const int referenceNoiseSampleRate =
            realtimeDataManager->realtimeWaveformSampleRateForChannel(
                kRealtimeReferenceNoiseChannel);
        if (referenceNoiseSampleRate > 0) {
            realtimeSampleRate = referenceNoiseSampleRate;
        }
    }

    SignalPreprocessing* realtimeSignalPreprocessing =
        SignalPreprocessing::instance();
    const bool waveletDenoisingEnabled =
        realtimeSignalPreprocessing->realtimeWaveletDenoisingEnabled();
    const bool transientNoiseSuppressionEnabled =
        realtimeSignalPreprocessing->realtimeTransientNoiseSuppressionEnabled();
    const bool motionArtifactReductionEnabled =
        realtimeSignalPreprocessing->realtimeMotionArtifactReductionEnabled();

    QThread* realtimeExportThread = new QThread;
    WAVHandleWorker* realtimeExportWorker = new WAVHandleWorker();
    realtimeExportWorker->moveToThread(realtimeExportThread);

    m_thread = realtimeExportThread;
    m_worker = realtimeExportWorker;

    connect(
        realtimeExportThread,
        &QThread::started,
        realtimeExportWorker,
        [realtimeExportWorker,
         channelTemporaryFilePaths,
         postProcessedChannelCount,
         realtimeSampleRate,
         waveletDenoisingEnabled,
         transientNoiseSuppressionEnabled,
         motionArtifactReductionEnabled,
         outputDirectoryPath,
         outputBaseName]() {
            realtimeExportWorker->exportRealtimeChannelsToWav(
                channelTemporaryFilePaths,
                postProcessedChannelCount,
                realtimeSampleRate,
                waveletDenoisingEnabled,
                transientNoiseSuppressionEnabled,
                motionArtifactReductionEnabled,
                resolvePythonScriptPath(QStringLiteral("WAVExport.py")),
                outputDirectoryPath,
                outputBaseName);
        });
    connect(
        realtimeExportWorker,
        &WAVHandleWorker::exportSucceeded,
        this,
        &WAVHandle::handleExportSucceeded);
    connect(
        realtimeExportWorker,
        &WAVHandleWorker::exportFailed,
        this,
        &WAVHandle::handleExportFailed);
    connect(
        realtimeExportWorker,
        &WAVHandleWorker::operationCompleted,
        realtimeExportThread,
        &QThread::quit);
    connect(
        realtimeExportThread,
        &QThread::finished,
        realtimeExportWorker,
        &WAVHandleWorker::deleteLater);
    connect(
        realtimeExportThread,
        &QThread::finished,
        realtimeExportThread,
        &QThread::deleteLater);
    connect(
        realtimeExportThread,
        &QThread::finished,
        this,
        [this, realtimeExportThread, realtimeExportWorker]() {
            if (m_thread == realtimeExportThread) {
                m_thread = nullptr;
            }
            if (m_worker == realtimeExportWorker) {
                m_worker = nullptr;
            }
        });

    realtimeExportThread->start();
#if 0
    return;

    DataManager* dataManager = DataManager::instance();
    const QString temporaryFloatFilePath = dataManager->rawTemporaryFilePath();
    if (temporaryFloatFilePath.isEmpty() ||
        !QFileInfo::exists(temporaryFloatFilePath)) {
        qWarning() << "WAVHandle: no cached temporary file available for export";
        emit saveFailed(QStringLiteral("当前没有可导出的采样数据"));
        return;
    }

    const int sampleRate = resolveExportSampleRate(
        dataManager->realtimeWaveformSampleRate(),
        dataManager->configuredSampleRate());

    startSaveOperation(temporaryFloatFilePath, sampleRate, false);
#endif
}

void WAVHandle::startSaveImportedAsWav()
{
    DataManager* dataManager = DataManager::instance();
    const QString temporaryFloatFilePath = dataManager->importedRawTemporaryFilePath();
    if (temporaryFloatFilePath.isEmpty() ||
        !QFileInfo::exists(temporaryFloatFilePath)) {
        qWarning() << "WAVHandle: no cached imported temporary file available for export";
        emit importedSaveFailed(QStringLiteral("当前没有可保存的导入数据"));
        return;
    }

    const int sampleRate = resolveExportSampleRate(
        dataManager->importedWaveformSampleRate(),
        m_importSampleRate);

    startSaveOperation(temporaryFloatFilePath, sampleRate, true);
}

void WAVHandle::startSaveOperation(
    const QString& temporaryFloatFilePath,
    int sampleRate,
    bool importedSave,
    const QString& outputDirectoryPath,
    const QString& outputBaseName)
{
    QThread* thread = new QThread;
    WAVHandleWorker* worker = new WAVHandleWorker();
    worker->moveToThread(thread);

    m_thread = thread;
    m_worker = worker;

    connect(
        thread,
        &QThread::started,
        worker,
        [worker, temporaryFloatFilePath, sampleRate, outputDirectoryPath, outputBaseName]() {
        worker->exportToWav(
            temporaryFloatFilePath,
            sampleRate,
            resolvePythonScriptPath(QStringLiteral("WAVExport.py")),
            1,
            outputDirectoryPath,
            outputBaseName);
    });

    if (importedSave) {
        connect(worker, &WAVHandleWorker::exportSucceeded, this, [this](const QString& outputPath) {
            emit importedSaveCompleted(outputPath);
        });
        connect(worker, &WAVHandleWorker::exportFailed, this, [this](const QString& errorMessage) {
            qWarning() << "WAVHandle:" << errorMessage;
            emit importedSaveFailed(errorMessage);
        });
    } else {
        connect(worker, &WAVHandleWorker::exportSucceeded, this, &WAVHandle::handleExportSucceeded);
        connect(worker, &WAVHandleWorker::exportFailed, this, &WAVHandle::handleExportFailed);
    }

    connect(worker, &WAVHandleWorker::operationCompleted, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &WAVHandleWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread, worker]() {
        if (m_thread == thread) {
            m_thread = nullptr;
        }
        if (m_worker == worker) {
            m_worker = nullptr;
        }
    });

    thread->start();
}

void WAVHandle::handleExportSucceeded(const QString& outputPath)
{
    emit saveCompleted(outputPath);
}

void WAVHandle::handleExportFailed(const QString& errorMessage)
{
    qWarning() << "WAVHandle:" << errorMessage;
    emit saveFailed(errorMessage);
}

/**
 * @brief 从WAV文件导入数据，通过Python脚本解析为float32电压，再经过预处理管道后存储。
 * @param wavFilePATH WAV文件的本地路径或file:// URL。
 */
void WAVHandle::startReadFromWav(const QString& wavFilePATH)
{
    DataManager::instance()->clearImportedData();
    m_timeDomainDataImport.clear();
    m_importSampleRate = DataManager::DEFAULT_SAMPLE_RATE;

    const QString wavFilePath = normalizeLocalFilePath(wavFilePATH);
    const QFileInfo fileInfo(wavFilePath);
    DataManager::instance()->updateImportedAnalysisSummary({
        {QStringLiteral("sourceFilePath"),
         fileInfo.exists() ? fileInfo.absoluteFilePath() : wavFilePath},
        {QStringLiteral("sourceFileName"), fileInfo.fileName()},
        {QStringLiteral("completed"), false},
        {QStringLiteral("success"), false},
        {QStringLiteral("errorMessage"), QString()}
    });

    if (wavFilePath.isEmpty() || !fileInfo.exists() || !fileInfo.isFile()) {
        handleImportFailed(
            QStringLiteral("\u8bf7\u9009\u62e9\u6709\u6548\u7684 WAV \u6587\u4ef6\uff1a%1")
                .arg(QDir::toNativeSeparators(wavFilePath)));
        return;
    }

    QThread* thread = new QThread;
    WAVHandleWorker* worker = new WAVHandleWorker();
    worker->moveToThread(thread);

    m_thread = thread;
    m_worker = worker;

    connect(thread, &QThread::started, worker, [worker, wavFilePath]() {
        worker->importFromWav(
            resolvePythonScriptPath(QStringLiteral("WAVImport.py")),
            wavFilePath);
    });
    connect(worker, &WAVHandleWorker::importDataReady, this, &WAVHandle::updateTimeDomainDataImport);
    connect(worker, &WAVHandleWorker::importFailed, this, &WAVHandle::handleImportFailed);
    connect(worker, &WAVHandleWorker::operationCompleted, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &WAVHandleWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread, worker]() {
        if (m_thread == thread) {
            m_thread = nullptr;
        }
        if (m_worker == worker) {
            m_worker = nullptr;
        }
    });

    thread->start();
}

void WAVHandle::updateTimeDomainDataImport(
    int dataImportSamplingRate,
    const QVector<float>& dataImport)
{
    m_importSampleRate =
        dataImportSamplingRate > 0
            ? dataImportSamplingRate
            : DataManager::DEFAULT_SAMPLE_RATE;
    m_timeDomainDataImport = dataImport;
    emit importDataReady();
}

void WAVHandle::handleImportFailed(const QString& errorMessage)
{
    DataManager::instance()->updateImportedAnalysisSummary({
        {QStringLiteral("completed"), true},
        {QStringLiteral("success"), false},
        {QStringLiteral("errorMessage"), errorMessage}
    });
    qWarning() << "WAVHandle:" << errorMessage;
    emit importFailed(errorMessage);
}

WAVHandleWorker::WAVHandleWorker(QObject* parent)
    : QObject(parent)
{
}

WAVHandleWorker::~WAVHandleWorker() = default;

void WAVHandleWorker::exportToWav(
    const QString& temporaryFloatFilePath,
    int sampleRate,
    const QString& pythonScriptPath,
    int channelCount,
    const QString& outputDirectoryPath,
    const QString& outputBaseName)
{
    const WavExportResult result =
        runWavExport(
            temporaryFloatFilePath,
            sampleRate,
            channelCount,
            pythonScriptPath,
            outputDirectoryPath,
            outputBaseName);
    if (result.succeeded) {
        qDebug() << "WAV file saved to:" << result.outputPath;
        emit exportSucceeded(result.outputPath);
    } else {
        emit exportFailed(result.errorMessage);
    }
    emit operationCompleted();
    return;
#if 0

    if (temporaryFloatFilePath.isEmpty() || !QFile::exists(temporaryFloatFilePath)) {
        qCritical() << "WAVHandle: temporary float cache file does not exist:" << temporaryFloatFilePath;
        emit exportFailed(QStringLiteral("临时缓存文件不存在，无法导出 WAV"));
        emit operationCompleted();
        return;
    }

    if (pythonScriptPath.isEmpty() || !QFileInfo::exists(pythonScriptPath)) {
        qCritical() << "WAVHandle: python export script does not exist:" << pythonScriptPath;
        emit exportFailed(QStringLiteral("WAV 导出脚本不存在: %1").arg(pythonScriptPath));
        emit operationCompleted();
        return;
    }

    QProcess process;
    const QString pythonExecutablePath = resolvePythonExecutable();
    if (pythonExecutablePath.isEmpty() || !QFileInfo::exists(pythonExecutablePath)) {
        emit exportFailed(
            QStringLiteral(
                "未找到内置 Python 运行时，请检查 PythonModules/Runtime 目录"));
        emit operationCompleted();
        return;
    }

    QStringList commandLineArguments;
    commandLineArguments
        << QStringLiteral("-B")
        << pythonScriptPath
        << temporaryFloatFilePath
        << DatabaseStoragePaths::AUDIO_PATH
        << QString::number(sampleRate);

    process.setWorkingDirectory(QCoreApplication::applicationDirPath());
    process.start(pythonExecutablePath, commandLineArguments);

    if (!process.waitForFinished(kWavExportTimeoutMs)) {
        qCritical() << "Python script timed out or failed:" << process.errorString();
        emit exportFailed(QStringLiteral("WAV 导出超时或失败: %1").arg(process.errorString()));
        emit operationCompleted();
        return;
    }

    if (process.exitCode() != 0) {
        qCritical() << "Python script returned an error:";
        const QString errorText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        qCritical() << errorText;
        emit exportFailed(
            errorText.isEmpty()
                ? QStringLiteral("WAV 导出失败")
                : QStringLiteral("WAV 导出失败: %1").arg(errorText));
        emit operationCompleted();
        return;
    }

    const QString processOutput =
        QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    const QString outputPath = parseExportedWavPath(processOutput);
    const QString resolvedOutputPath =
        outputPath.isEmpty() ? DatabaseStoragePaths::AUDIO_PATH : outputPath;

    qDebug() << "WAV file saved to:" << resolvedOutputPath;
    emit exportSucceeded(resolvedOutputPath);
    emit operationCompleted();
#endif
}

/**
 * @brief 将实时多通道临时缓存文件拼合后导出为多通道WAV。
 * @param channelTemporaryFilePaths 各通道临时缓存文件路径列表。
 * @param postProcessedChannelCount 需要后处理的通道数（前N个通道）。
 * @param sampleRate 采样率（Hz）。
 * @param waveletDenoisingEnabled 是否启用小波去噪。
 * @param transientNoiseSuppressionEnabled 是否启用瞬态噪声抑制。
 * @param motionArtifactReductionEnabled 是否启用运动伪迹削减。
 * @param pythonScriptPath WAV导出Python脚本路径。
 */
void WAVHandleWorker::exportRealtimeChannelsToWav(
    const QVector<QString>& channelTemporaryFilePaths,
    int postProcessedChannelCount,
    int sampleRate,
    bool waveletDenoisingEnabled,
    bool transientNoiseSuppressionEnabled,
    bool motionArtifactReductionEnabled,
    const QString& pythonScriptPath,
    const QString& outputDirectoryPath,
    const QString& outputBaseName)
{
    if (channelTemporaryFilePaths.isEmpty()) {
        emit exportFailed(QStringLiteral("当前没有可导出的激活通道数据"));
        emit operationCompleted();
        return;
    }

    QVector<QVector<float>> processedChannelSamples;
    processedChannelSamples.reserve(channelTemporaryFilePaths.size());

    qsizetype maximumSampleCount = 0;
    const int effectivePostProcessedChannelCount =
        std::clamp(
            postProcessedChannelCount,
            0,
            static_cast<int>(channelTemporaryFilePaths.size()));
    for (int channelIndex = 0; channelIndex < channelTemporaryFilePaths.size(); ++channelIndex) {
        const QString& channelTemporaryFilePath =
            channelTemporaryFilePaths[channelIndex];
        QVector<float> samples = readFloatCacheSamples(channelTemporaryFilePath);
        if (!samples.isEmpty() &&
            channelIndex < effectivePostProcessedChannelCount) {
            samples = applyRealtimeSavePostProcessing(
                std::move(samples),
                sampleRate,
                waveletDenoisingEnabled,
                transientNoiseSuppressionEnabled,
                motionArtifactReductionEnabled);
        }
        maximumSampleCount = std::max(maximumSampleCount, samples.size());
        processedChannelSamples.append(std::move(samples));
    }

    if (maximumSampleCount <= 0) {
        emit exportFailed(QStringLiteral("当前没有可导出的采样数据"));
        emit operationCompleted();
        return;
    }

    if (!QDir().mkpath(DatabaseStoragePaths::TEMPORARY_FILE_PATH)) {
        emit exportFailed(QStringLiteral("无法创建临时导出目录"));
        emit operationCompleted();
        return;
    }

    QTemporaryFile temporaryFloatFile(
        QDir(DatabaseStoragePaths::TEMPORARY_FILE_PATH)
            .filePath(QStringLiteral("realtime_multichannel_export_XXXXXX.float")));
    temporaryFloatFile.setAutoRemove(true);
    if (!temporaryFloatFile.open()) {
        emit exportFailed(QStringLiteral("无法创建多通道临时缓存文件"));
        emit operationCompleted();
        return;
    }

    if (!writeMultichannelFloatCache(
            processedChannelSamples,
            sampleRate,
            temporaryFloatFile)) {
        emit exportFailed(QStringLiteral("写入多通道临时缓存文件失败"));
        emit operationCompleted();
        return;
    }

    const QString temporaryFloatFilePath = temporaryFloatFile.fileName();
    temporaryFloatFile.close();

    const WavExportResult result = runWavExport(
        temporaryFloatFilePath,
        sampleRate,
        processedChannelSamples.size(),
        pythonScriptPath,
        outputDirectoryPath,
        outputBaseName);
    if (result.succeeded) {
        qDebug() << "Realtime multichannel WAV file saved to:" << result.outputPath
                 << "channels:" << processedChannelSamples.size();
        emit exportSucceeded(result.outputPath);
    } else {
        emit exportFailed(result.errorMessage);
    }
    emit operationCompleted();
}

/**
 * @brief 通过Python脚本将WAV文件解析为PCM int16，转换为float32电压，再经预处理管道后通过信号通知主线程。
 * @param pythonScriptPath WAV导入Python脚本路径。
 * @param wavFilePATH WAV文件路径。
 */
void WAVHandleWorker::importFromWav(const QString& pythonScriptPath, const QString& wavFilePATH)
{
    if (pythonScriptPath.isEmpty() || !QFileInfo::exists(pythonScriptPath)) {
        emit importFailed(
            QStringLiteral("WAV import script not found: %1")
                .arg(pythonScriptPath));
        emit operationCompleted();
        return;
    }

    QProcess process;
    const QString pythonExecutablePath = resolvePythonExecutable();
    if (pythonExecutablePath.isEmpty() || !QFileInfo::exists(pythonExecutablePath)) {
        emit importFailed(
            QStringLiteral(
                "未找到内置 Python 运行时，请检查 PythonModules/Runtime 目录"));
        emit operationCompleted();
        return;
    }

    QVector<float> voltageData;
    int sampleRate = DataManager::DEFAULT_SAMPLE_RATE;
    QElapsedTimer importTimer;
    importTimer.start();

    process.setWorkingDirectory(QCoreApplication::applicationDirPath());
    process.start(
        pythonExecutablePath,
        QStringList()
            << QStringLiteral("-B")
            << pythonScriptPath
            << wavFilePATH);
    if (!process.waitForFinished(kWavImportTimeoutMs)) {
        emit importFailed(
            QStringLiteral("WAV 导入脚本执行超时或失败: %1")
                .arg(process.errorString()));
        emit operationCompleted();
        return;
    }

    if (process.exitCode() != 0) {
        emit importFailed(
            QStringLiteral("WAV 导入脚本执行失败: %1")
                .arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed()));
        emit operationCompleted();
        return;
    }

    const QString output = process.readAllStandardOutput().trimmed();
    const QStringList parts = output.split(",");
    if (parts.size() == 3) {
        const QString temporaryPcmPath = parts[0];
        sampleRate = parts[1].toInt();
        const int sampleWidth = parts[2].toInt();

        if (sampleWidth != static_cast<int>(sizeof(int16_t))) {
            emit importFailed(
                QStringLiteral("WAV 导入返回了不支持的 PCM 采样宽度: %1")
                    .arg(sampleWidth));
            emit operationCompleted();
            return;
        }

        QVector<int16_t> pcmData;
        QFile file(temporaryPcmPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray rawData = file.readAll();
            file.close();
            QFile::remove(temporaryPcmPath);

            if ((rawData.size() % static_cast<int>(sizeof(int16_t))) != 0) {
                emit importFailed(
                    QStringLiteral("导入得到的 PCM 临时文件不是有效的 16-bit 数据: %1")
                        .arg(temporaryPcmPath));
                emit operationCompleted();
                return;
            }

            pcmData.resize(rawData.size() / static_cast<int>(sizeof(int16_t)));
            memcpy(pcmData.data(), rawData.constData(), static_cast<size_t>(rawData.size()));
        } else {
            emit importFailed(
                QStringLiteral("无法读取导入脚本生成的 PCM 临时文件: %1")
                    .arg(temporaryPcmPath));
            emit operationCompleted();
            return;
        }

        voltageData.reserve(pcmData.size());
        for (const int16_t& sample : pcmData) {
            float voltage;
            if (sample == -32768) {
                voltage = MIN_VOLTAGE;
            } else {
                voltage = (sample / INT16_MAX_VAL) * MAX_VOLTAGE;
            }
            voltageData.append(voltage);
        }
    } else {
        emit importFailed(
            QStringLiteral("WAV 导入脚本返回格式无效: %1").arg(output));
        emit operationCompleted();
        return;
    }

    if (voltageData.isEmpty()) {
        emit importFailed(QStringLiteral("导入的 WAV 数据为空"));
        emit operationCompleted();
        return;
    }

    float minAmplitude = voltageData.constFirst();
    float maxAmplitude = voltageData.constFirst();
    float peakAmplitude = std::abs(voltageData.constFirst());
    for (float sample : voltageData) {
        minAmplitude = std::min(minAmplitude, sample);
        maxAmplitude = std::max(maxAmplitude, sample);
        peakAmplitude = std::max(peakAmplitude, std::abs(sample));
    }

    const double durationSeconds =
        static_cast<double>(voltageData.size()) /
        static_cast<double>(std::max(1, sampleRate));
    DataManager::instance()->updateImportedAnalysisSummary({
        {QStringLiteral("sampleRate"), sampleRate},
        {QStringLiteral("sampleCount"), static_cast<qlonglong>(voltageData.size())},
        {QStringLiteral("durationSeconds"), durationSeconds},
        {QStringLiteral("durationMs"), durationSeconds * 1000.0},
        {QStringLiteral("minAmplitude"), static_cast<double>(minAmplitude)},
        {QStringLiteral("maxAmplitude"), static_cast<double>(maxAmplitude)},
        {QStringLiteral("peakAmplitude"), static_cast<double>(peakAmplitude)},
        {QStringLiteral("timings"), QVariantMap{
             {QStringLiteral("importReadMs"), elapsedMilliseconds(importTimer)}
         }}
    });

    const QVector<float> preprocessedData =
        SignalPreprocessing::instance()->filterDataImport(sampleRate, voltageData);
    if (preprocessedData.isEmpty()) {
        emit importFailed(QStringLiteral("导入后的预处理结果为空"));
        emit operationCompleted();
        return;
    }

    emit importDataReady(sampleRate, preprocessedData);
    emit operationCompleted();
}
