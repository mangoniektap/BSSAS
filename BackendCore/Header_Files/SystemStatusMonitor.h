/** @file SystemStatusMonitor.h
 *  @brief 系统状态监控模块，实时采集存储、内存、CPU 使用率及采集设备连接状态。
 */

#ifndef SYSTEMSTATUSMONITOR_H
#define SYSTEMSTATUSMONITOR_H

#include <QObject>
#include <QString>

class DaqDeviceManager;
class QTimer;

/** @brief 系统状态监控器，定时轮询并对外暴露系统资源与设备状态。 */
class SystemStatusMonitor : public QObject
{
    Q_OBJECT
    /** @brief 存储使用百分比 */
    Q_PROPERTY(qreal storageUsagePercent READ storageUsagePercent NOTIFY statusChanged)
    /** @brief 内存使用百分比 */
    Q_PROPERTY(qreal memoryUsagePercent READ memoryUsagePercent NOTIFY statusChanged)
    /** @brief CPU 使用百分比 */
    Q_PROPERTY(qreal cpuUsagePercent READ cpuUsagePercent NOTIFY statusChanged)
    /** @brief 存储设备名称 */
    Q_PROPERTY(QString storageName READ storageName NOTIFY statusChanged)
    /** @brief 采集设备状态文字描述 */
    Q_PROPERTY(QString deviceStatusText READ deviceStatusText NOTIFY statusChanged)
    /** @brief 采集设备是否已连接 */
    Q_PROPERTY(bool deviceConnected READ deviceConnected NOTIFY statusChanged)

public:
    explicit SystemStatusMonitor(QObject* parent = nullptr);

    /** @brief 获取存储使用百分比 @returns 0.0 ~ 100.0 */
    qreal storageUsagePercent() const;
    /** @brief 获取内存使用百分比 @returns 0.0 ~ 100.0 */
    qreal memoryUsagePercent() const;
    /** @brief 获取 CPU 使用百分比 @returns 0.0 ~ 100.0 */
    qreal cpuUsagePercent() const;
    /** @brief 获取存储设备名称 @returns 设备名称字符串 */
    QString storageName() const;
    /** @brief 获取采集设备状态文字 @returns 状态描述 */
    QString deviceStatusText() const;
    /** @brief 采集设备是否已连接 @returns 连接状态 */
    bool deviceConnected() const;

    /** @brief 立即刷新所有状态读数 */
    Q_INVOKABLE void refresh();

signals:
    /** @brief 系统状态发生变化信号 */
    void statusChanged();

private:
    /** @brief 刷新采集设备连接状态 @returns 设备是否已连接 */
    bool refreshDeviceState();
    /** @brief 读取 CPU 使用率 @returns 0.0 ~ 100.0 */
    qreal readCpuUsagePercent();

    /** @brief 读取存储使用百分比 @param storageName 输出: 存储设备名称 @returns 使用百分比 */
    static qreal readStorageUsagePercent(QString* storageName);
    /** @brief 读取内存使用百分比 @returns 使用百分比 */
    static qreal readMemoryUsagePercent();

    DaqDeviceManager* m_daqManager = nullptr;     /**< 采集设备管理器 */
    QTimer* m_refreshTimer = nullptr;             /**< 定时刷新计时器 */
    qreal m_storageUsagePercent = 0.0;            /**< 存储使用百分比 */
    qreal m_memoryUsagePercent = 0.0;             /**< 内存使用百分比 */
    qreal m_cpuUsagePercent = 0.0;                /**< CPU 使用百分比 */
    QString m_storageName;                         /**< 存储设备名称 */
    QString m_deviceStatusText;                    /**< 设备状态文字 */
    bool m_deviceConnected = false;                /**< 设备连接状态 */

#ifdef Q_OS_WIN
    quint64 m_previousIdleTime = 0;               /**< Windows: 上次空闲时间 */
    quint64 m_previousKernelTime = 0;             /**< Windows: 上次内核时间 */
    quint64 m_previousUserTime = 0;               /**< Windows: 上次用户时间 */
    bool m_hasCpuSample = false;                  /**< Windows: 是否已有 CPU 采样 */
#endif
};

#endif // SYSTEMSTATUSMONITOR_H
