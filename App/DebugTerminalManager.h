#pragma once

#include <QObject>
#include <QMutex>
#include <QStringList>
#include <QtMessageHandler>

#include <atomic>
#include <thread>

class DebugTerminalManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(int maximumEntries READ maximumEntries WRITE setMaximumEntries NOTIFY maximumEntriesChanged)
    Q_PROPERTY(bool nativeConsoleVisible READ nativeConsoleVisible WRITE setNativeConsoleVisible NOTIFY nativeConsoleVisibleChanged)

public:
    explicit DebugTerminalManager(QObject* parent = nullptr);
    ~DebugTerminalManager() override;

    QStringList entries() const;

    int maximumEntries() const;
    void setMaximumEntries(int maximumEntries);

    bool nativeConsoleVisible() const;

    Q_INVOKABLE void appendInfo(const QString& message);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setNativeConsoleVisible(bool visible);
    Q_INVOKABLE void submit(const QString& command);
    void shutdown();

signals:
    void entriesChanged();
    void maximumEntriesChanged();
    void nativeConsoleVisibleChanged();

private:
    void appendEntry(const QString& level, const QString& message);
    void clearNativeConsole();
    bool ensureNativeConsole();
    void showNativeConsole(bool visible);
    void startConsoleInputThread();
    void stopConsoleInputThread();
    void writeNativeConsoleLine(const QString& line);
    void writeNativeConsolePrompt();
    void writeNativeConsoleText(const QString& text);

#ifdef Q_OS_WIN
    void consoleInputLoop();
#endif

    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);
    static QString levelName(QtMsgType type);

    mutable QMutex m_mutex;
    QStringList m_entries;
    int m_maximumEntries = 400;
    bool m_nativeConsoleAllocated = false;
    bool m_nativeConsoleVisible = false;
    bool m_ownsNativeConsole = false;
    std::atomic_bool m_shuttingDown = false;
    std::atomic_bool m_inputThreadRunning = false;
    std::thread m_inputThread;

    static std::atomic<DebugTerminalManager*> s_instance;
    static QtMessageHandler s_previousHandler;
};
