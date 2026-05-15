/**
 * @file SystemStatusMonitor.cpp
 * @brief 系统状态监控模块，定时刷新存储、内存、CPU使用率及采集设备连接/采集状态，变化超过阈值时触发状态变更通知。
 */

#include "SystemStatusMonitor.h"

#include "DaqDeviceManager.h"

#include <QStorageInfo>
#include <QTimer>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr int kRefreshIntervalMs = 1000;
constexpr qreal kPercentChangeThreshold = 0.1;

qreal boundedPercent(qreal value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }

    return std::clamp(value, 0.0, 100.0);
}

bool percentChanged(qreal previous, qreal next)
{
    return std::abs(previous - next) >= kPercentChangeThreshold;
}

#ifdef Q_OS_WIN
quint64 fileTimeToUInt64(const FILETIME& fileTime)
{
    ULARGE_INTEGER value;
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return value.QuadPart;
}
#endif
}

SystemStatusMonitor::SystemStatusMonitor(DaqDeviceManager* daqManager, QObject* parent)
    : QObject(parent)
    , m_daqManager(daqManager)
    , m_refreshTimer(new QTimer(this))
    , m_deviceStatusText(QStringLiteral("\u672a\u8fde\u63a5"))
{
    connect(m_refreshTimer, &QTimer::timeout, this, &SystemStatusMonitor::refresh);
    m_refreshTimer->start(kRefreshIntervalMs);

    if (m_daqManager) {
        connect(m_daqManager, &QObject::destroyed, this, [this]() {
            m_daqManager = nullptr;
        });
        connect(m_daqManager, &DaqDeviceManager::statusTextChanged, this, [this]() {
            if (refreshDeviceState()) {
                emit statusChanged();
            }
        });
        connect(m_daqManager, &DaqDeviceManager::connectionChanged, this, [this]() {
            if (refreshDeviceState()) {
                emit statusChanged();
            }
        });
        connect(m_daqManager, &DaqDeviceManager::collectionChanged, this, [this]() {
            if (refreshDeviceState()) {
                emit statusChanged();
            }
        });
        connect(m_daqManager, &DaqDeviceManager::readingChanged, this, [this]() {
            if (refreshDeviceState()) {
                emit statusChanged();
            }
        });
    }

    refresh();
}

qreal SystemStatusMonitor::storageUsagePercent() const
{
    return m_storageUsagePercent;
}

qreal SystemStatusMonitor::memoryUsagePercent() const
{
    return m_memoryUsagePercent;
}

qreal SystemStatusMonitor::cpuUsagePercent() const
{
    return m_cpuUsagePercent;
}

QString SystemStatusMonitor::storageName() const
{
    return m_storageName;
}

QString SystemStatusMonitor::deviceStatusText() const
{
    return m_deviceStatusText;
}

bool SystemStatusMonitor::deviceConnected() const
{
    return m_deviceConnected;
}

/**
 * @brief 周期刷新存储、内存、CPU使用率及设备状态，变化超过阈值时发出通知。
 */
void SystemStatusMonitor::refresh()
{
    bool changed = false;

    QString nextStorageName;
    const qreal nextStorageUsagePercent = readStorageUsagePercent(&nextStorageName);
    if (percentChanged(m_storageUsagePercent, nextStorageUsagePercent)) {
        m_storageUsagePercent = nextStorageUsagePercent;
        changed = true;
    }
    if (m_storageName != nextStorageName) {
        m_storageName = nextStorageName;
        changed = true;
    }

    const qreal nextMemoryUsagePercent = readMemoryUsagePercent();
    if (percentChanged(m_memoryUsagePercent, nextMemoryUsagePercent)) {
        m_memoryUsagePercent = nextMemoryUsagePercent;
        changed = true;
    }

    const qreal nextCpuUsagePercent = readCpuUsagePercent();
    if (percentChanged(m_cpuUsagePercent, nextCpuUsagePercent)) {
        m_cpuUsagePercent = nextCpuUsagePercent;
        changed = true;
    }

    changed = refreshDeviceState() || changed;

    if (changed) {
        emit statusChanged();
    }
}

/**
 * @brief 刷新采集设备连接与采集状态。
 * @returns 状态是否发生变化。
 */
bool SystemStatusMonitor::refreshDeviceState()
{
    bool nextDeviceConnected = false;
    QString nextDeviceStatusText = QStringLiteral("\u672a\u8fde\u63a5");

    if (m_daqManager) {
        nextDeviceConnected = m_daqManager->isConnected();
        if (m_daqManager->isCollecting() || m_daqManager->isReading()) {
            nextDeviceStatusText = QStringLiteral("\u91c7\u96c6\u4e2d");
        } else if (nextDeviceConnected) {
            nextDeviceStatusText = QStringLiteral("\u6b63\u5e38");
        } else {
            const QString statusText = m_daqManager->statusText().trimmed();
            if (statusText.contains(QStringLiteral("\u672a\u68c0\u6d4b"))) {
                nextDeviceStatusText = QStringLiteral("\u672a\u68c0\u6d4b");
            } else if (!statusText.isEmpty()) {
                nextDeviceStatusText = QStringLiteral("\u672a\u8fde\u63a5");
            }
        }
    }

    const bool changed = m_deviceConnected != nextDeviceConnected ||
                         m_deviceStatusText != nextDeviceStatusText;
    if (changed) {
        m_deviceConnected = nextDeviceConnected;
        m_deviceStatusText = nextDeviceStatusText;
    }

    return changed;
}

qreal SystemStatusMonitor::readStorageUsagePercent(QString* storageName)
{
    QStorageInfo storage = QStorageInfo::root();
    storage.refresh();
    if (storageName) {
        *storageName = storage.rootPath();
    }

    if (!storage.isValid() || !storage.isReady() || storage.bytesTotal() <= 0) {
        return 0.0;
    }

    const qreal totalBytes = static_cast<qreal>(storage.bytesTotal());
    const qreal usedBytes = totalBytes - static_cast<qreal>(storage.bytesAvailable());
    return boundedPercent(usedBytes * 100.0 / totalBytes);
}

qreal SystemStatusMonitor::readMemoryUsagePercent()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memoryStatus;
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus)) {
        return boundedPercent(static_cast<qreal>(memoryStatus.dwMemoryLoad));
    }
#endif

    return 0.0;
}

qreal SystemStatusMonitor::readCpuUsagePercent()
{
#ifdef Q_OS_WIN
    FILETIME idleTime;
    FILETIME kernelTime;
    FILETIME userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return m_cpuUsagePercent;
    }

    const quint64 currentIdleTime = fileTimeToUInt64(idleTime);
    const quint64 currentKernelTime = fileTimeToUInt64(kernelTime);
    const quint64 currentUserTime = fileTimeToUInt64(userTime);

    if (!m_hasCpuSample) {
        m_previousIdleTime = currentIdleTime;
        m_previousKernelTime = currentKernelTime;
        m_previousUserTime = currentUserTime;
        m_hasCpuSample = true;
        return m_cpuUsagePercent;
    }

    const quint64 idleDelta = currentIdleTime - m_previousIdleTime;
    const quint64 kernelDelta = currentKernelTime - m_previousKernelTime;
    const quint64 userDelta = currentUserTime - m_previousUserTime;
    const quint64 totalDelta = kernelDelta + userDelta;

    m_previousIdleTime = currentIdleTime;
    m_previousKernelTime = currentKernelTime;
    m_previousUserTime = currentUserTime;

    if (totalDelta == 0 || idleDelta >= totalDelta) {
        return 0.0;
    }

    return boundedPercent(
        static_cast<qreal>(totalDelta - idleDelta) * 100.0 / static_cast<qreal>(totalDelta));
#else
    return m_cpuUsagePercent;
#endif
}
