#ifndef DAQDEVICEMANAGER_H
#define DAQDEVICEMANAGER_H

#include <QCoreApplication> // qApp定义头文件
#include <QObject>
#include <QVector>
#include <QString>
#include <QMutex>
#include <QVariantList>

class QTimer;
class QThread;
class DaqWorker : public QObject
{
    Q_OBJECT
public:
    explicit DaqWorker(QObject* parent = nullptr);
    ~DaqWorker();

public slots:
    void initializeDevice();    // 初始化设备
    void startHardwareCollection(); // 启动DAQ硬件采集(不读取缓存)
    void stopHardwareCollection();  // 停止DAQ硬件采集
    void startBufferReading();      // 启动缓存读取
    void stopBufferReading();       // 停止缓存读取
    void refreshDevice();   //刷新设备
   
private slots:
    void onCollectionTimer();   // 私有槽函数 定时器每到指定时间 触发该函数

signals:
    void rawDataReady(const QVector<QVector<float>>& data);
    void statusTextChanged(const QString& status);
    void deviceErrorChanged(const QString& error);
    void connectStateChanged(bool isConnected);
    void collectStateChanged(bool isCollecting);
    void readStateChanged(bool isReading);

private:
    bool openDevice();  // 打开设备
    void closeDevice(); // 关闭设备
    bool configureDevice(); // 配置设备参数:采样率 通道
    QVector<QVector<float>> collectRawData();  // 采集原始数据 多通道float类型
    QVector<QVector<float>> m_channelsDataCache;    // 每个通道缓存最近SAMPLES_PER_CHANNEL个样本
    // 设备句柄和互斥锁
    int m_deviceHandle = -1;    // 设备句柄 初始值-1 表示未打开
    QMutex m_deviceMutex;    // 设备互斥锁 用于保护对设备的访问 防止多线程同时操作设备
    // 设备状态
    bool m_isConnected = false;
    bool m_isCollecting = false;
    bool m_isReading = false;
    int m_interleavedPhase = 0;   // 交织流当前起始相位(0~8)，用于修正非整帧读取造成的通道漂移
    QString m_statusText = "";
    QString m_deviceError = "";
    // 采集定时器
    QTimer* m_timer = nullptr;
};

class DaqDeviceManager : public QObject // 定义DAQ设备管理器类 继承自QObject
{
    Q_OBJECT    // Q_OBJECT宏 启用Qt元对象系统（信号槽等）

    // C++暴露给qml界面的接口 qml可直接读取 修改 并监听属性变化
    /* bool类型属性isConnected READ isConnected（读取函数）外部读取时会调用isConnected()成员函数获取值 NOTIFY connectionChanged（通知信号）值改变时会发送
    connectionChanged() qml会监听该信号 自动更新绑定的UI */
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged) // 字符串类型属性
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionChanged) // 字符串类型属性
    Q_PROPERTY(bool isCollecting READ isCollecting NOTIFY collectionChanged) // 字符串类型属性
    Q_PROPERTY(bool isReading READ isReading NOTIFY readingChanged) // 软件缓存读取状态
    Q_PROPERTY(QVariantList activeChannels READ activeChannels NOTIFY activeChannelsChanged) // 通道激活状态

    
public:
    static DaqDeviceManager* instance(){    // 将DaqDeviceManager注册为单例模式
        if (m_instance == nullptr) {
            static QMutex mutex;
            QMutexLocker locker(&mutex);
            if (m_instance == nullptr) {
                // 动态分配，父对象绑定到qApp，保证析构顺序正确
                m_instance = new DaqDeviceManager(qApp);
            }
        }
        return m_instance;
    }
    // Q_INVOKABLE宏将成员函数标记为“可被Qt元对象（MOC）识别并调用”的函数
    Q_INVOKABLE void initializeDevice(){ emit processingInitialize(); }
    Q_INVOKABLE void startHardwareCollection(){ emit processingStart(); }
    Q_INVOKABLE void stopHardwareCollection(){ emit processingStop(); }
    Q_INVOKABLE void startBufferReading();
    Q_INVOKABLE void stopBufferReading(){ emit processingReadStop(); }
    Q_INVOKABLE void refreshDevice(){ emit processingRefresh(); } 
    Q_INVOKABLE bool isChannelActive(int channelIndex) const;
    Q_INVOKABLE void setChannelActive(int channelIndex, bool active);
    Q_INVOKABLE void setAllChannelsActive(bool active);
    
    // Getters
    QString statusText() const { return m_statusText; }
    bool isConnected() const { return m_isConnected; }
    bool isCollecting() const { return m_isCollecting; }
    bool isReading() const { return m_isReading; }
    QVariantList activeChannels() const;

    QVector<float> getDataCache(int currentChannel) const;

signals:
    void connectionChanged();
    void collectionChanged();
    void readingChanged();
    void statusTextChanged();
    void deviceErrorChanged();
    void activeChannelsChanged();

     // 通知Worker
    void processingInitialize();
    void processingStart();
    void processingStop();
    void processingReadStart();
    void processingReadStop();
    void processingRefresh();
private:
    // 内部方法
    // 单例模式下构造函数和析构函数必须放在private
    explicit DaqDeviceManager(QObject* parent = nullptr);   // 构造函数 explicit 防止隐式类型转换
    ~DaqDeviceManager() override;   // 析构函数 释放资源

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
    // 数据缓存
    QVector<QVector<float>> m_channelsDataCache;    // 每个通道缓存最近SAMPLES_PER_CHANNEL个样本
    QVector<bool> m_channelActiveStates;    // 通道激活状态，默认全部激活
    mutable QMutex m_dataCacheMutex;
    mutable QMutex m_channelStateMutex;
    static DaqDeviceManager* m_instance;    // 存储单例实例
    QThread* m_thread = nullptr;
    DaqWorker* m_worker = nullptr;
};

#endif // DAQDEVICEMANAGER_H

/*
* 默认采样率下，采样时间间隔为 50us，100ms 每个通道采集 2000 个数据，共产生 16000 个数据
* 当数据量小于 2000 时，在下一次读取使用与默认采样率同规模的数组存储数据，确保能把数据全部读取
* 不会导致FIFO内数据越来越多
*/
