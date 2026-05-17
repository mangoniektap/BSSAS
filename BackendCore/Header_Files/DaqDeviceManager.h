/** @file DaqDeviceManager.h
 *  @brief DAQ 设备管理器 —— USB 数据采集卡驱动封装与实时数据分发
 *
 *  本模块封装了对 NI-DAQmx 兼容数据采集卡的底层操作，
 *  包括设备打开/关闭、参数配置、硬件采集启停和缓冲读取。
 *  采用 Worker-Thread 架构：DaqWorker 在独立线程中执行
 *  I/O 密集型任务，DaqDeviceManager 作为主线程单例门面
 *  负责状态管理、数据缓存和 QML 接口暴露。
 *
 *  支持 8 通道同步采集，默认采样率为 12 kS/s（每通道），
 *  采样间隔 50us，每 100ms 每通道采集 2000 个样本。
 */

#ifndef DAQDEVICEMANAGER_H
#define DAQDEVICEMANAGER_H

#include <QCoreApplication> // qApp 定义头文件
#include <QObject>
#include <QVector>
#include <QString>
#include <QMutex>
#include <QVariantList>

class QTimer;
class QThread;

/** @brief DAQ 工作线程对象
 *
 *  在独立线程中运行，负责直接操作采集卡硬件。
 *  包含设备生命周期管理、定时采集和缓冲读取等功能。
 */
class DaqWorker : public QObject
{
    Q_OBJECT
public:
    explicit DaqWorker(QObject* parent = nullptr);
    ~DaqWorker();

public slots:
    void initializeDevice();            /**< 初始化设备 */
    void startHardwareCollection();     /**< 启动 DAQ 硬件采集（不读取缓存） */
    void stopHardwareCollection();      /**< 停止 DAQ 硬件采集 */
    void startBufferReading();          /**< 启动缓存读取 */
    void stopBufferReading();           /**< 停止缓存读取 */
    void refreshDevice();               /**< 刷新设备 */

private slots:
    /** @brief 私有槽函数，定时器每到指定时间触发该函数，执行一次采样读取 */
    void onCollectionTimer();

signals:
    /** @brief 原始数据就绪，二维向量 [通道][样本] */
    void rawDataReady(const QVector<QVector<float>>& data);
    /** @brief 设备状态文本发生变化 */
    void statusTextChanged(const QString& status);
    /** @brief 设备错误信息发生变化 */
    void deviceErrorChanged(const QString& error);
    /** @brief 设备连接状态发生变化 */
    void connectStateChanged(bool isConnected);
    /** @brief 硬件采集状态发生变化 */
    void collectStateChanged(bool isCollecting);
    /** @brief 缓冲读取状态发生变化 */
    void readStateChanged(bool isReading);

private:
    bool openDevice();                                 /**< 打开设备 */
    void closeDevice();                                /**< 关闭设备 */
    bool configureDevice();                            /**< 配置设备参数：采样率、通道 */
    QVector<QVector<float>> collectRawData();          /**< 采集原始数据，多通道 float 类型 */

    /** @brief 每个通道缓存最近 SAMPLES_PER_CHANNEL 个样本 */
    QVector<QVector<float>> m_channelsDataCache;

    /** @brief 设备句柄，初始值 -1 表示未打开 */
    int m_deviceHandle = -1;
    /** @brief 设备互斥锁，用于保护对设备的访问，防止多线程同时操作设备 */
    QMutex m_deviceMutex;

    // 设备状态
    bool m_isConnected = false;
    bool m_isCollecting = false;
    bool m_isReading = false;
    /** @brief 交织流当前起始相位 (0~8)，用于修正非整帧读取造成的通道漂移 */
    int m_interleavedPhase = 0;
    QString m_statusText = "";
    QString m_deviceError = "";

    /** @brief 采集定时器 */
    QTimer* m_timer = nullptr;
};

/** @brief DAQ 设备管理器（单例）
 *
 *  定义 DAQ 设备管理器类，继承自 QObject。
 *  作为主线程与工作线程之间的桥梁，管理采集卡全生命周期，
 *  并通过 Q_PROPERTY 将设备状态暴露给 QML 界面。
 */
class DaqDeviceManager : public QObject
{
    Q_OBJECT    // Q_OBJECT 宏，启用 Qt 元对象系统（信号槽等）

    /** @brief 设备状态文本，QML 可直接读取并监听值变化 */
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    /** @brief 设备是否已成功连接 */
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionChanged)
    /** @brief 是否正在执行硬件采集 */
    Q_PROPERTY(bool isCollecting READ isCollecting NOTIFY collectionChanged)
    /** @brief 软件缓存读取状态 */
    Q_PROPERTY(bool isReading READ isReading NOTIFY readingChanged)
    /** @brief 通道激活状态列表 */
    Q_PROPERTY(QVariantList activeChannels READ activeChannels NOTIFY activeChannelsChanged)


public:
    /** @brief 获取全局单例实例（双重检查锁定线程安全）
     *
     *  将 DaqDeviceManager 注册为单例模式。
     *  动态分配，父对象绑定到 qApp，保证析构顺序正确。
     */
    static DaqDeviceManager* instance(){
        if (m_instance == nullptr) {
            static QMutex mutex;
            QMutexLocker locker(&mutex);
            if (m_instance == nullptr) {
                m_instance = new DaqDeviceManager(qApp);
            }
        }
        return m_instance;
    }

    /** @brief 初始化设备（通过信号通知工作线程执行） */
    Q_INVOKABLE void initializeDevice(){ emit processingInitialize(); }
    /** @brief 启动硬件采集（通过信号通知工作线程执行） */
    Q_INVOKABLE void startHardwareCollection(){ emit processingStart(); }
    /** @brief 停止硬件采集（通过信号通知工作线程执行） */
    Q_INVOKABLE void stopHardwareCollection(){ emit processingStop(); }
    /** @brief 启动缓冲读取 */
    Q_INVOKABLE void startBufferReading();
    /** @brief 停止缓冲读取 */
    Q_INVOKABLE void stopBufferReading(){ emit processingReadStop(); }
    /** @brief 刷新设备 */
    Q_INVOKABLE void refreshDevice(){ emit processingRefresh(); }
    /** @brief 查询指定通道是否激活
     *  @param channelIndex 通道索引
     *  @returns 激活返回 true
     */
    Q_INVOKABLE bool isChannelActive(int channelIndex) const;
    /** @brief 设置指定通道的激活状态
     *  @param channelIndex 通道索引
     *  @param active 是否激活
     */
    Q_INVOKABLE void setChannelActive(int channelIndex, bool active);
    /** @brief 设置所有通道的激活状态
     *  @param active 是否全部激活
     */
    Q_INVOKABLE void setAllChannelsActive(bool active);

    // Getters
    QString statusText() const { return m_statusText; }
    bool isConnected() const { return m_isConnected; }
    bool isCollecting() const { return m_isCollecting; }
    bool isReading() const { return m_isReading; }
    QVariantList activeChannels() const;

    /** @brief 获取指定通道的数据缓存副本
     *  @param currentChannel 通道索引
     *  @returns 该通道的最近样本数据
     */
    QVector<float> getDataCache(int currentChannel) const;

signals:
    /** @brief 设备连接状态变更 */
    void connectionChanged();
    /** @brief 采集状态变更 */
    void collectionChanged();
    /** @brief 读取状态变更 */
    void readingChanged();
    /** @brief 状态文本变更 */
    void statusTextChanged();
    /** @brief 设备错误变更 */
    void deviceErrorChanged();
    /** @brief 通道激活状态变更 */
    void activeChannelsChanged();
    /** @brief 实时原始数据更新，供监听等实时消费者订阅 */
    void realtimeDataUpdated(const QVector<QVector<float>>& data);

    // 通知 Worker 的内部信号
    /** @brief 通知 Worker 执行初始化 */
    void processingInitialize();
    /** @brief 通知 Worker 开始采集 */
    void processingStart();
    /** @brief 通知 Worker 停止采集 */
    void processingStop();
    /** @brief 通知 Worker 开始读取 */
    void processingReadStart();
    /** @brief 通知 Worker 停止读取 */
    void processingReadStop();
    /** @brief 通知 Worker 刷新设备 */
    void processingRefresh();

private:
    // 单例模式下构造函数和析构函数必须放在 private
    /** @brief 构造函数，explicit 防止隐式类型转换 */
    explicit DaqDeviceManager(QObject* parent = nullptr);
    /** @brief 析构函数，释放资源 */
    ~DaqDeviceManager() override;

    void updateDataCache(const QVector<QVector<float>>& data);
    void clearDataCache();
    void updateStatusText(const QString& status);
    void updateDeviceError(const QString& error);
    void updateConnectState(bool isConnected);
    void updateCollectState(bool isCollecting);
    void updateReadState(bool isReading);


    QString m_statusText = "";
    QString m_deviceError = "";
    bool m_isConnected = false;
    bool m_isCollecting = false;
    bool m_isReading = false;

    /** @brief 每个通道缓存最近 SAMPLES_PER_CHANNEL 个样本 */
    QVector<QVector<float>> m_channelsDataCache;
    /** @brief 通道激活状态，默认全部激活 */
    QVector<bool> m_channelActiveStates;
    mutable QMutex m_dataCacheMutex;
    mutable QMutex m_channelStateMutex;
    /** @brief 存储单例实例 */
    static DaqDeviceManager* m_instance;
    QThread* m_thread = nullptr;
    DaqWorker* m_worker = nullptr;
};

#endif // DAQDEVICEMANAGER_H

/*
* 默认采样率下，采样时间间隔为 50us，100ms 每个通道采集 2000 个数据，共产生 16000 个数据
* 当数据量小于 2000 时，在下一次读取使用与默认采样率同规模的数组存储数据，确保能把数据全部读取
* 不会导致 FIFO 内数据越来越多
*/
