/** @file Multi_featureJointDetection.h
 *  @brief 多特征联合检测模块，基于短时能量、频域分布、过零率等多维度特征进行信号端点检测与分析。
 */

#ifndef MULTI_FEATUREJOINTDETECTION_H
#define MULTI_FEATUREJOINTDETECTION_H

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVariantMap>
#include <QVector>

class QThread;

/** @brief 特征检测算法类，提供信号分帧、多特征提取及三阶段端点检测功能。 */
class FeatureDetectionAlgorithm
{
public:
    FeatureDetectionAlgorithm() = default;

    /**
     * @brief 构造特征检测算法并配置基本参数。
     * @param sampleRate     输入信号的采样率 (Hz)
     * @param frameLengthMs  帧长 (ms)，默认 30.0
     * @param frameShiftMs   帧移 (ms)，默认 15.0
     * @param maxSilenceMs   最大静音段 (ms)，默认 200.0
     * @param thresholdT     ZCR 阈值参数，默认 0.01
     */
    FeatureDetectionAlgorithm(
        int sampleRate,
        double frameLengthMs = 30.0,
        double frameShiftMs = 15.0,
        double maxSilenceMs = 200.0,
        double thresholdT = 0.01);

    /** @brief 手动设置检测阈值。 @param steTh 短时能量阈值 @param zcrTh 过零率阈值 @param fdTh 频域分布阈值 */
    void setThresholds(double steTh, double zcrTh, double fdTh);

    /** @brief 对原始信号执行端点检测。 @param rawSignal 原始信号数据 @returns 检测到的语音段起止索引对列表 */
    QVector<QPair<int, int>> detect(const QVector<double>& rawSignal);

    /** @brief 对原始信号进行多特征分析。 @param rawSignal 原始信号数据 @returns 包含各维度特征值的 QVariantMap */
    QVariantMap analyze(const QVector<double>& rawSignal);

private:
    QVector<QVector<double>> frameSignal(const QVector<double>& signal);
    void extractFeatures(
        const QVector<QVector<double>>& frames,
        QVector<double>& steVec,
        QVector<double>& fdVec,
        QVector<double>& zcrVec);
    double computeSTE(const QVector<double>& frame);
    double computeFD(const QVector<double>& frame);
    double computeZCR(const QVector<double>& frame, double thresholdT);
    void estimateAdaptiveThresholds(
        const QVector<double>& ste,
        const QVector<double>& zcr,
        const QVector<double>& fd);
    QVector<QPair<int, int>> detectByThreeStage(
        const QVector<double>& ste,
        const QVector<double>& zcr,
        const QVector<double>& fd,
        int frameShiftSamples);

    int m_sampleRate = 0;
    double m_frameLengthMs = 30.0;
    double m_frameShiftMs = 15.0;
    double m_maxSilenceMs = 200.0;
    double m_thresholdT = 0.01;

    int m_frameLengthSamples = 0;
    int m_frameShiftSamples = 0;
    int m_maxSilenceFrames = 0;

    double m_steThresh = 0.0;
    double m_zcrThresh = 0.0;
    double m_fdThresh = 0.0;
    double m_logEnergyThresh = 0.0;
    double m_envPeakThresh = 0.0;
    double m_transientThresh = 0.0;
    double m_entropyUpperBound = 0.0;
    bool m_thresholdsSet = false;

    QVector<double> m_logSteVec;
    QVector<double> m_envPeakVec;
    QVector<double> m_envSlopeVec;
    QVector<double> m_kurtosisVec;
    QVector<double> m_subbandLowVec;
    QVector<double> m_subbandMidVec;
    QVector<double> m_subbandHighVec;
    QVector<double> m_dominantFreqVec;
    QVector<double> m_spectralCentroidVec;
    QVector<double> m_spectralBandwidthVec;
    QVector<double> m_spectralRolloffVec;
    QVector<double> m_spectralEntropyVec;
    QVector<double> m_multiscaleEnergy1Vec;
    QVector<double> m_multiscaleEnergy2Vec;
    QVector<double> m_multiscaleEnergy3Vec;
    QVector<double> m_waveletEntropyVec;
    QVector<double> m_transientStrengthVec;
    QVector<double> m_logMel1Vec;
    QVector<double> m_logMel2Vec;
    QVector<double> m_logMel3Vec;
    QVector<double> m_candidateScoreVec;
    QVector<double> m_frameRmsVec;
    QVector<double> m_frameDurationMsVec;
    QVector<bool> m_candidateMask;
};

/** @brief 多特征联合检测工作线程，读取临时文件执行单次分析并回传结果。 */
class MultiFeatureJointDetectionWorker : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief 构造工作线程。
     * @param thresholdsConfigured 是否已配置阈值
     * @param steThreshold        短时能量阈值
     * @param zcrThreshold        过零率阈值
     * @param fdThreshold         频域分布阈值
     * @param parent              父对象
     */
    explicit MultiFeatureJointDetectionWorker(
        bool thresholdsConfigured,
        double steThreshold,
        double zcrThreshold,
        double fdThreshold,
        QObject* parent = nullptr);
    ~MultiFeatureJointDetectionWorker() override;

public slots:
    /**
     * @brief 一次性工作流程：读取临时文件，执行多特征分析，写回特征结果。
     * @param temporaryFilePath 临时文件路径
     */
    void analyzeImportedTemporaryFile(const QString& temporaryFilePath);

signals:
    /** @brief 分析完成信号 @param featureValues 特征值映射 */
    void analysisCompleted(const QVariantMap& featureValues);
    /** @brief 分析失败信号 @param errorMessage 错误消息 */
    void analysisFailed(const QString& errorMessage);
    /** @brief 工作线程结束信号 */
    void finished();

private:
    bool m_thresholdsConfigured = false;
    double m_steThreshold = 0.0;
    double m_zcrThreshold = 0.0;
    double m_fdThreshold = 0.0;
};

/** @brief 多特征联合检测管理器 (单例)，协调工作线程生命周期并向 QML 暴露接口。 */
class Multi_featureJointDetection : public QObject
{
    Q_OBJECT
    /** @brief 是否正处于分析忙碌状态 */
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /** @brief 导入数据的特征分析结果 */
    Q_PROPERTY(QVariantMap importedFeatureValues READ importedFeatureValues NOTIFY importedFeatureValuesChanged)

public:
    static Multi_featureJointDetection* instance()
    {
        if (m_instance == nullptr) {
            static QMutex mutex;
            QMutexLocker locker(&mutex);
            if (m_instance == nullptr) {
                m_instance = new Multi_featureJointDetection(qApp);
            }
        }
        return m_instance;
    }

    /** @brief 启动对导入数据的多特征分析 */
    Q_INVOKABLE void startImportedAnalysis();
    /**
     * @brief 设置检测阈值。
     * @param steTh 短时能量阈值
     * @param zcrTh 过零率阈值
     * @param fdTh  频域分布阈值
     */
    Q_INVOKABLE void setThresholds(double steTh, double zcrTh, double fdTh);

    /** @brief 是否忙碌 @returns 忙碌状态 */
    bool busy() const;
    /** @brief 获取导入特征值 @returns 特征值映射 */
    QVariantMap importedFeatureValues() const;

signals:
    /** @brief 导入分析完成信号 @param featureValues 特征值映射 */
    void importedAnalysisCompleted(const QVariantMap& featureValues);
    /** @brief 导入分析失败信号 @param errorMessage 错误消息 */
    void importedAnalysisFailed(const QString& errorMessage);
    /** @brief 忙碌状态变化信号 */
    void busyChanged();
    /** @brief 导入特征值变化信号 */
    void importedFeatureValuesChanged();

private slots:
    /** @brief 处理导入数据就绪 */
    void handleImportedDataReady();
    /** @brief 处理工作线程分析完成 @param featureValues 特征值映射 */
    void handleWorkerCompleted(const QVariantMap& featureValues);
    /** @brief 处理工作线程分析失败 @param errorMessage 错误消息 */
    void handleWorkerFailed(const QString& errorMessage);
    /** @brief 处理工作线程结束 */
    void handleWorkerFinished();

private:
    explicit Multi_featureJointDetection(QObject* parent = nullptr);
    ~Multi_featureJointDetection() override;

    /** @brief 尝试启动导入分析，为每次运行创建新的工作线程 */
    void tryStartImportedAnalysis();
    /** @brief 设置忙碌状态 @param busy 是否忙碌 */
    void setBusy(bool busy);

    static Multi_featureJointDetection* m_instance;

    mutable QMutex m_stateMutex;
    bool m_busy = false;
    bool m_thresholdsConfigured = false;
    bool m_analysisRequested = false;
    double m_steThreshold = 0.0;
    double m_zcrThreshold = 0.0;
    double m_fdThreshold = 0.0;
    QThread* m_workerThread = nullptr;  /**< 当前活跃的工作线程 */
};

#endif // MULTI_FEATUREJOINTDETECTION_H

