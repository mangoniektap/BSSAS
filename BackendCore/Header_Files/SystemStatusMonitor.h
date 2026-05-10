#ifndef SYSTEMSTATUSMONITOR_H
#define SYSTEMSTATUSMONITOR_H

#include <QObject>
#include <QString>

class DaqDeviceManager;
class QTimer;

class SystemStatusMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal storageUsagePercent READ storageUsagePercent NOTIFY statusChanged)
    Q_PROPERTY(qreal memoryUsagePercent READ memoryUsagePercent NOTIFY statusChanged)
    Q_PROPERTY(qreal cpuUsagePercent READ cpuUsagePercent NOTIFY statusChanged)
    Q_PROPERTY(QString storageName READ storageName NOTIFY statusChanged)
    Q_PROPERTY(QString deviceStatusText READ deviceStatusText NOTIFY statusChanged)
    Q_PROPERTY(bool deviceConnected READ deviceConnected NOTIFY statusChanged)

public:
    explicit SystemStatusMonitor(QObject* parent = nullptr);

    qreal storageUsagePercent() const;
    qreal memoryUsagePercent() const;
    qreal cpuUsagePercent() const;
    QString storageName() const;
    QString deviceStatusText() const;
    bool deviceConnected() const;

    Q_INVOKABLE void refresh();

signals:
    void statusChanged();

private:
    bool refreshDeviceState();
    qreal readCpuUsagePercent();

    static qreal readStorageUsagePercent(QString* storageName);
    static qreal readMemoryUsagePercent();

    DaqDeviceManager* m_daqManager = nullptr;
    QTimer* m_refreshTimer = nullptr;
    qreal m_storageUsagePercent = 0.0;
    qreal m_memoryUsagePercent = 0.0;
    qreal m_cpuUsagePercent = 0.0;
    QString m_storageName;
    QString m_deviceStatusText;
    bool m_deviceConnected = false;

#ifdef Q_OS_WIN
    quint64 m_previousIdleTime = 0;
    quint64 m_previousKernelTime = 0;
    quint64 m_previousUserTime = 0;
    bool m_hasCpuSample = false;
#endif
};

#endif // SYSTEMSTATUSMONITOR_H
