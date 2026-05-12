/** @file DaqDeviceManager.cpp
 *  @brief DAQ 数据采集设备管理器实现。封装 USB-DAQ4203 硬件驱动，负责设备初始化/配置/启停采集、
 *         多通道交织数据解交织、缓冲区定时读取、线程安全的数据缓存及通道激活状态管理。
 */

#include "DaqDeviceManager.h"

#include "DataManager.h"
#include "Usb_Daq4203.h"
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <QDebug>
#include <algorithm>
/** 
*   格式化DAQ错误信息 
*   输入:api 调用的API名称;code 错误码
*   输出:可读的格式化后的错误信息字符串 
*/
namespace { // 匿名命名空间 使其中内容仅在当前.cpp文件中可见
    // 共享常量定义 - 避免在DaqWorker和DaqDeviceManager中重复
    static constexpr int VISIBLE_CHANNEL_COUNT = 7;      // UI与业务可见通道数
    static constexpr int PHYSICAL_CHANNEL_COUNT = 8;     // 硬件采集通道数(含固定开启的第8参考通道)
    static constexpr int SAMPLES_PER_CHANNEL = 2048;     // 每个通道预分配内存容量
    static constexpr int BUFFER_SIZE = PHYSICAL_CHANNEL_COUNT * SAMPLES_PER_CHANNEL;    // 总缓存大小
    static constexpr int COLLECTION_INTERVAL_MS = 100;   // 从DAQ缓存读取时间间隔
    static constexpr int WAIT_TIMEOUT = 5000;

    static QString formatDaqError(const QString& api, int code)
    {
        return QString("[%1] failed, code=%2").arg(api).arg(code);
    }

    static QString statusLine(const QString& status)
    {
        return status;
    }

    static int currentCollectionSampleRate()
    {
        return DataManager::instance()->configuredSampleRate();
    }

    static int expectedInterleavedSampleCount(int sampleRate, int channelCount, int intervalMs)
    {
        const qint64 expectedPerChannel =
            std::max<qint64>(1, (static_cast<qint64>(sampleRate) * intervalMs + 999) / 1000);
        return static_cast<int>(expectedPerChannel * channelCount);
    }
}

DaqDeviceManager* DaqDeviceManager::m_instance = nullptr;
DaqDeviceManager::DaqDeviceManager(QObject* parent)
    : QObject(parent)
{
    // 初始化多通道数据缓存
    m_channelsDataCache.resize(PHYSICAL_CHANNEL_COUNT);   // 包含固定开启的第8参考通道
    for (int ch = 0; ch < PHYSICAL_CHANNEL_COUNT; ++ch) {
        m_channelsDataCache[ch].reserve(SAMPLES_PER_CHANNEL);   // reserve()将内层QVector<double>预分配内存空间 但不创建实际元素
    }
    m_channelActiveStates.resize(VISIBLE_CHANNEL_COUNT);
    std::fill(m_channelActiveStates.begin(), m_channelActiveStates.end(), true);
    m_statusText = statusLine("设备未连接");
    m_thread = new QThread(this);
    m_worker = new DaqWorker();
    m_worker->moveToThread(m_thread);

    connect(this, &DaqDeviceManager::processingInitialize, m_worker, &DaqWorker::initializeDevice);
    connect(this, &DaqDeviceManager::processingStart, m_worker, &DaqWorker::startHardwareCollection);
    connect(this, &DaqDeviceManager::processingStop, m_worker, &DaqWorker::stopHardwareCollection);
    connect(this, &DaqDeviceManager::processingReadStart, m_worker, &DaqWorker::startBufferReading);
    connect(this, &DaqDeviceManager::processingReadStop, m_worker, &DaqWorker::stopBufferReading);
    connect(this, &DaqDeviceManager::processingRefresh, m_worker, &DaqWorker::refreshDevice);
    connect(m_worker, &DaqWorker::rawDataReady, this, &DaqDeviceManager::updateDataCache);
    connect(m_worker, &DaqWorker::statusTextChanged, this, &DaqDeviceManager::updateStatusText);
    connect(m_worker, &DaqWorker::deviceErrorChanged, this, &DaqDeviceManager::updateDeviceError);
    connect(m_worker, &DaqWorker::connectStateChanged, this, &DaqDeviceManager::updateConnectState);
    connect(m_worker, &DaqWorker::collectStateChanged, this, &DaqDeviceManager::updateCollectState);
    connect(m_worker, &DaqWorker::readStateChanged, this, &DaqDeviceManager::updateReadState);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    
    m_thread->start();
}
DaqDeviceManager::~DaqDeviceManager()
{
    if(m_thread->isRunning()){
        m_thread->quit();
        m_thread->wait();
    }
}
void DaqDeviceManager::updateDataCache(const QVector<QVector<float>>& data)
{
    QMutexLocker locker(&m_dataCacheMutex);
    m_channelsDataCache = data;
}

void DaqDeviceManager::clearDataCache()
{
    QMutexLocker locker(&m_dataCacheMutex);
    for (QVector<float>& channelData : m_channelsDataCache) {
        channelData.clear();
    }
}

QVector<float> DaqDeviceManager::getDataCache(int currentChannel) const
{
    QMutexLocker locker(&m_dataCacheMutex);
    if (currentChannel < 0 || currentChannel >= m_channelsDataCache.size()) {
        return {};
    }

    return m_channelsDataCache[currentChannel];
}
void DaqDeviceManager::updateStatusText(const QString& status)
{
    m_statusText = status;
    emit statusTextChanged();
}
void DaqDeviceManager::updateDeviceError(const QString& error)
{
    m_deviceError = error;
    emit deviceErrorChanged();
}
void DaqDeviceManager::updateConnectState(bool connection)
{
    m_isConnected = connection;
    emit connectionChanged();
}
void DaqDeviceManager::updateCollectState(bool collection)
{
    m_isCollecting = collection;
    emit collectionChanged();
}
void DaqDeviceManager::updateReadState(bool reading)
{
    m_isReading = reading;
    emit readingChanged();
}

bool DaqDeviceManager::isChannelActive(int channelIndex) const
{
    QMutexLocker locker(&m_channelStateMutex);
    if (channelIndex < 0 || channelIndex >= m_channelActiveStates.size()) {
        return false;
    }
    return m_channelActiveStates[channelIndex];
}

void DaqDeviceManager::setChannelActive(int channelIndex, bool active)
{
    {
        QMutexLocker locker(&m_channelStateMutex);
        if (channelIndex < 0 || channelIndex >= m_channelActiveStates.size()) {
            return;
        }
        if (m_channelActiveStates[channelIndex] == active) {
            return;
        }
        m_channelActiveStates[channelIndex] = active;
    }
    emit activeChannelsChanged();
}

void DaqDeviceManager::setAllChannelsActive(bool active)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_channelStateMutex);
        for (int channelIndex = 0; channelIndex < m_channelActiveStates.size(); ++channelIndex) {
            if (m_channelActiveStates[channelIndex] != active) {
                m_channelActiveStates[channelIndex] = active;
                changed = true;
            }
        }
    }
    if (changed) {
        emit activeChannelsChanged();
    }
}

QVariantList DaqDeviceManager::activeChannels() const
{
    QVariantList result;
    QMutexLocker locker(&m_channelStateMutex);
    result.reserve(m_channelActiveStates.size());
    for (int channelIndex = 0; channelIndex < m_channelActiveStates.size(); ++channelIndex) {
        result.append(m_channelActiveStates[channelIndex]);
    }
    return result;
}

void DaqDeviceManager::startBufferReading()
{
    clearDataCache();
    emit processingReadStart();
}

DaqWorker::DaqWorker(QObject* parent) : QObject(parent)
{
    // 初始化多通道数据缓存
    m_channelsDataCache.resize(PHYSICAL_CHANNEL_COUNT);   // 包含固定开启的第8参考通道
    for (int ch = 0; ch < PHYSICAL_CHANNEL_COUNT; ++ch) {
        m_channelsDataCache[ch].reserve(SAMPLES_PER_CHANNEL);   // reserve()将内层QVector<double>预分配内存空间 但不创建实际元素
    }
}
DaqWorker::~DaqWorker()
{
    // 析构时确保停止采集并关闭设备
    stopHardwareCollection();
    closeDevice();
    if(m_timer){
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
}
/** @brief 初始化 DAQ 硬件设备：打开 USB 连接、配置采集参数、更新连接状态。 */
void DaqWorker::initializeDevice()
{
    QMutexLocker locker(&m_deviceMutex);    // 自动加锁 保护共享资源 确保线程安全

    if (m_isConnected) {    // 前置检查 避免重复初始化
        m_statusText = statusLine("设备已就绪");
        emit statusTextChanged(m_statusText);   // emit显示标记”发射信号“ 提升代码可读性
        emit connectStateChanged(m_isConnected);
        return;
    }

    if (!openDevice()) {    // 打开设备 若失败 返回false
        m_isConnected = false;
        emit connectStateChanged(false);
        return;
    }

    if (!configureDevice()) {   // 配置设备 若失败 关闭设备 返回false
        closeDevice();
        m_isConnected = false;
        emit connectStateChanged(false);
        return;
    }
    m_isConnected = true;   // 初始化成功 更新状态并发送信号
    m_statusText = statusLine("设备已就绪");
    emit statusTextChanged(m_statusText);
    emit connectStateChanged(m_isConnected);
}
void DaqWorker::refreshDevice()
{
    stopHardwareCollection();   // 刷新前先停止采集

    {   // 缩小锁的作用域,减少锁竞争
        QMutexLocker locker(&m_deviceMutex);
        closeDevice();
        m_isConnected = false;
        m_statusText = statusLine("正在刷新设备...");
    }
    emit statusTextChanged(m_statusText);
    emit connectStateChanged(false);
    // 停止采集 关闭设备 重置连接状态 重新初始化设备
    initializeDevice();
}
void DaqWorker::startHardwareCollection()
{
    // 创建缓存读取定时器，但硬件启动阶段不开始读取。
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, &DaqWorker::onCollectionTimer);
    }

    QMutexLocker locker(&m_deviceMutex);

    if (!m_isConnected || m_deviceHandle < 0) {
        const QString err = QStringLiteral("设备未连接，无法启动DAQ采集");
        m_statusText = statusLine("请先初始化设备");
        emit statusTextChanged(m_statusText);
        emit deviceErrorChanged(err);
        return;
    }

    if (m_isCollecting) {
        m_statusText = statusLine("DAQ采集已启动");
        emit statusTextChanged(m_statusText);
        return;
    }

    m_interleavedPhase = 0;
    const int sampleRate = currentCollectionSampleRate();

    // 开始连续采集
    const int rc = ad_continu_conf(
        m_deviceHandle,
        0,          
        0,            
        sampleRate,
        0,            
        0,             
        0,              
        0               
    );
    if (rc < 0) {
        const QString err = formatDaqError("startHardwareCollection", rc);
        m_statusText = statusLine("DAQ启动失败");
        emit statusTextChanged(m_statusText);
        emit deviceErrorChanged(err);
        return;
    }

    if (m_timer && m_timer->isActive()) {
        m_timer->stop();
    }

    m_isCollecting = true;
    m_isReading = false;
    m_statusText = statusLine("DAQ采集已启动，等待读取");
    emit statusTextChanged(m_statusText);
    emit collectStateChanged(m_isCollecting);
    emit readStateChanged(m_isReading);
}

void DaqWorker::startBufferReading()
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, &DaqWorker::onCollectionTimer);
    }

    QMutexLocker locker(&m_deviceMutex);

    if (!m_isCollecting) {
        const QString err = QStringLiteral("DAQ未启动，无法开始读取缓存");
        m_statusText = statusLine("请先启动DAQ采集");
        emit statusTextChanged(m_statusText);
        emit deviceErrorChanged(err);
        return;
    }

    if (m_isReading) {
        return;
    }

    m_interleavedPhase = 0;
    for (QVector<float>& channelData : m_channelsDataCache) {
        channelData.clear();
    }
    m_timer->start(COLLECTION_INTERVAL_MS);
    m_isReading = true;
    m_statusText = statusLine("缓存读取已启动");
    emit statusTextChanged(m_statusText);
    emit readStateChanged(m_isReading);
}

void DaqWorker::stopBufferReading()
{
    QMutexLocker locker(&m_deviceMutex);
    if (!m_isReading) {
        return;
    }

    if (m_timer) {
        m_timer->stop();
    }

    m_isReading = false;
    m_interleavedPhase = 0;
    m_statusText = statusLine("缓存读取已停止");
    emit statusTextChanged(m_statusText);
    emit readStateChanged(m_isReading);
}

void DaqWorker::stopHardwareCollection()
{
    QMutexLocker locker(&m_deviceMutex);

    if (!m_isCollecting) {  // 不在采集状态 无需停止
        return;
    }

    if (m_timer) {
        m_timer->stop();  // 停止采集定时器
    }

    // DAQ硬件停止采集 释放缓冲区
    if (m_deviceHandle >= 0) {  // 判断设备句柄有效 <0表示无效/未打开
        const int rc = AD_continu_stop(m_deviceHandle);
        if (rc < 0) {
            // 硬件停止失败 发送错误信号 不终止程序 （强行终止会导致程序崩溃）
            emit deviceErrorChanged(formatDaqError("AD_continu_stop", rc));
        }
    }

    m_isReading = false;
    m_isCollecting = false;
    m_interleavedPhase = 0;
    m_statusText = statusLine("DAQ采集已停止");
    emit statusTextChanged(m_statusText);
    emit readStateChanged(m_isReading);
    emit collectStateChanged(m_isCollecting);
}
void DaqWorker::onCollectionTimer()
{
    {
        QMutexLocker locker(&m_deviceMutex);
        // 未连接/未采集/设备句柄无效 无需采集
        if (!m_isConnected || !m_isCollecting || !m_isReading || m_deviceHandle < 0) {
            return;
        }
        // 调用collectRwaData()
        m_channelsDataCache = collectRawData();    // 读取硬件原始采集数据
        if (m_channelsDataCache.isEmpty()) {   // 读取失败/数据不足 直接返回
            return; // collectRawData()内部已发出错误信号
        }
        emit rawDataReady(m_channelsDataCache);
    }
}
/** @brief 从 DAQ 硬件缓冲区读取原始数据并进行通道解交织。
 *  @returns 按物理通道索引组织的原始采集数据，每个通道为一个 QVector<float>
 */
QVector<QVector<float>> DaqWorker::collectRawData()
{   
    QElapsedTimer timer;
    timer.start();  // 启动计时器
    // 查询缓冲区可读数据量
    const int sampleRate = currentCollectionSampleRate();
    const int minimumInterleavedSamples = expectedInterleavedSampleCount(
        sampleRate,
        PHYSICAL_CHANNEL_COUNT,
        COLLECTION_INTERVAL_MS);
    int available = Get_AdBuf_Size(m_deviceHandle);
    int alignedAvailable = available - (available % PHYSICAL_CHANNEL_COUNT);
    while (alignedAvailable < minimumInterleavedSamples) {
        if (timer.elapsed() > WAIT_TIMEOUT) {
            emit deviceErrorChanged(formatDaqError("查询缓冲区可读数据量超时", available));
            return {};
        }
        QThread::msleep(10);
        available = Get_AdBuf_Size(m_deviceHandle);
        alignedAvailable = available - (available % PHYSICAL_CHANNEL_COUNT);
    }

    // 初始化交织数据缓冲区 存储底层读取的原始交织数据
    QVector<float> interleavedData;
    interleavedData.resize(alignedAvailable);

    // 只读取完整的8通道整帧，避免余数样本导致下次读取通道相位错位。
    const int rawInterleavedData = Read_AdBuf(m_deviceHandle, interleavedData.data(), alignedAvailable); // 调用底层驱动 从设备缓冲区读取交织数据
    // 返回的数据是已经根据选择的量程自动计算为输入 
    if (rawInterleavedData < 0) {   // 读取失败
        emit deviceErrorChanged(formatDaqError("读取缓冲区数据", rawInterleavedData));
        return {};
    }

    // 数据为通道交织:(ch0[0], ch1[0], ..., ch7[0], ch0[1], ch1[1], ...)
    // 数组内现在有rawInterleavedData个数据 且一定为8的倍数
    // 临时变量 存储本次采集的原始数据
    QVector<QVector<float>> rawData;

    const int alignedRawInterleavedData =
        rawInterleavedData - (rawInterleavedData % PHYSICAL_CHANNEL_COUNT);
    if (alignedRawInterleavedData <= 0) {
        m_interleavedPhase =
            (m_interleavedPhase + rawInterleavedData) % PHYSICAL_CHANNEL_COUNT;
        return {};
    }

    rawData.resize(PHYSICAL_CHANNEL_COUNT);
    for (int channel = 0; channel < PHYSICAL_CHANNEL_COUNT; ++channel) {
        rawData[channel].resize(alignedRawInterleavedData / PHYSICAL_CHANNEL_COUNT);    // 初始化存储结构
    }

    int index = 0;
    for (int i = 0; i < alignedRawInterleavedData / PHYSICAL_CHANNEL_COUNT; ++i) {
        for (int channel = 0; channel < PHYSICAL_CHANNEL_COUNT; ++channel) {
            const int physicalChannel =
                (m_interleavedPhase + channel) % PHYSICAL_CHANNEL_COUNT;
            rawData[physicalChannel][i] = interleavedData[index++];
        }
    }

    m_interleavedPhase = (m_interleavedPhase + rawInterleavedData) % PHYSICAL_CHANNEL_COUNT;
    return rawData;
}
/** @brief 打开 USB 连接并检测 DAQ 设备，获取有效设备句柄。
 *  @returns 设备打开成功返回 true
 */
bool DaqWorker::openDevice()
{
    // 不同厂商 DLL 的 openUSB 返回语义可能不同
    // - 有的返回 0 成功，<0 失败
    // - 有的返回设备句柄/索引
    // 采用兼容策略

    const int rcOpen = openUSB();   // 调用底层驱动 打开USB连接
    if (rcOpen != 0) {   // 打开失败:格式化错误 + 更新状态 + 发送信号
        const QString error = formatDaqError("打开USB", rcOpen);
        m_statusText = statusLine("未检测到设备");
        emit statusTextChanged(m_statusText);
        emit deviceErrorChanged(error);
        return false;
    }
    const int devNum = get_device_num();    // 调用底层驱动 查询已连接设备数量
    if (devNum <= 0) {  // 无设备连接:格式化错误 + 更新状态 + 发送信号 + 关闭已打开的USB连接
        const QString error = QString("[获取设备数量] 无设备连接, 设备数量 = %1").arg(devNum);
        m_statusText = statusLine("未检测到设备");
        emit statusTextChanged(m_statusText);
        emit deviceErrorChanged(error);

        closeUSB();
        return false;
    }
    // 选择设备句柄
    // - 如果 openUSB 返回值看起来像索引（0..devNum-1），就用它
    // - 否则默认用 0
    // - DAQ4203 设备句柄为0
    if (rcOpen >= 0 && rcOpen < devNum) {
        m_deviceHandle = rcOpen;
    } else {
        m_deviceHandle = 0;
    }

    // 注意：设备复位在configureDevice()中会导致ad_continu_conf()卡死
    return true;
}
void DaqWorker::closeDevice()
{
    if (m_deviceHandle >= 0) {  // 当句柄有效时 先停止采集 避免关闭设备时仍在采集
        AD_continu_stop(m_deviceHandle);
    }

    closeUSB(); // 调用底层驱动 关闭USB连接
    m_deviceHandle = -1;    //重置设备句柄为无效值
}
bool DaqWorker::configureDevice()
{
    const int sampleRate = currentCollectionSampleRate();
    const int rc = ad_continu_conf(
        m_deviceHandle,
        0,          
        0,            
        sampleRate,
        0,            
        0,             
        0,              
        0               
    );
    if (rc < 0) {   // 配置失败
        const QString err = formatDaqError("配置设备", rc);
        m_statusText = statusLine("设备配置失败");
        emit statusTextChanged(m_statusText);
        emit deviceErrorChanged(err);
        return false;
    }
    AD_continu_stop(m_deviceHandle);  // 能够正常配置 停止采样过程 复位硬件采样电路 清空缓存区
    return true;
}


