/** @file DebugTerminalManager.h
 *  @brief 调试终端管理器 —— 统一的运行时日志输出与控制台窗口管理
 *
 *  本模块捕获 Qt 日志消息 (qDebug/qWarning/qCritical/qFatal)，并通过信号暴露
 *  格式化的日志条目列表供 QML 界面渲染；同时支持显示或隐藏原生控制台窗口，
 *  以及向控制台提交文本命令（用于调试交互）。
 */

#pragma once

#include <QObject>
#include <QMutex>
#include <QStringList>
#include <QtMessageHandler>

#include <atomic>
#include <thread>

/** @brief 调试终端管理器
 *
 *  以单例模式工作的调试日志管理器。接管 Qt 全局消息处理器
 *  (qInstallMessageHandler)，将所有级别（Debug / Info / Warning / Critical）
 *  的消息收集到受保护的最大条目数列表中。列表变更通过 entriesChanged() 信号
 *  通知 QML 前端。另外提供原生控制台的分配、显示、隐藏和文本写入能力。
 */
class DebugTerminalManager : public QObject
{
    Q_OBJECT
    /** @brief 日志条目列表，每项格式 "级别::消息" */
    Q_PROPERTY(QStringList entries READ entries NOTIFY entriesChanged)
    /** @brief 可保留的最大条目数，超出时自动丢弃旧条目 */
    Q_PROPERTY(int maximumEntries READ maximumEntries WRITE setMaximumEntries NOTIFY maximumEntriesChanged)
    /** @brief 原生控制台窗口当前是否可见 */
    Q_PROPERTY(bool nativeConsoleVisible READ nativeConsoleVisible WRITE setNativeConsoleVisible NOTIFY nativeConsoleVisibleChanged)

public:
    explicit DebugTerminalManager(QObject* parent = nullptr);
    ~DebugTerminalManager() override;

    /** @brief 获取当前日志条目列表 */
    QStringList entries() const;

    /** @brief 获取最大保留条目数 */
    int maximumEntries() const;
    /** @brief 设置最大保留条目数 */
    void setMaximumEntries(int maximumEntries);

    /** @brief 获取原生控制台是否可见 */
    bool nativeConsoleVisible() const;

    /** @brief 追加一条 Info 级别日志 */
    Q_INVOKABLE void appendInfo(const QString& message);
    /** @brief 清空所有日志条目 */
    Q_INVOKABLE void clear();
    /** @brief 设置原生控制台窗口的显示/隐藏状态 */
    Q_INVOKABLE void setNativeConsoleVisible(bool visible);
    /** @brief 向调试终端提交一条交互命令 */
    Q_INVOKABLE void submit(const QString& command);
    /** @brief 关闭终端，停止控制台输入线程并释放资源 */
    void shutdown();

signals:
    /** @brief 日志条目列表发生变化时发出 */
    void entriesChanged();
    /** @brief 最大条目数发生变化时发出 */
    void maximumEntriesChanged();
    /** @brief 原生控制台可见性发生变化时发出 */
    void nativeConsoleVisibleChanged();

private:
    /** @brief 按级别追加一条格式化日志 */
    void appendEntry(const QString& level, const QString& message);
    /** @brief 清空原生控制台屏幕 */
    void clearNativeConsole();
    /** @brief 确保分配了原生控制台窗口；已分配则直接返回 true */
    bool ensureNativeConsole();
    /** @brief 显示或隐藏原生控制台窗口 */
    void showNativeConsole(bool visible);
    /** @brief 启动标准输入监听线程 */
    void startConsoleInputThread();
    /** @brief 停止标准输入监听线程 */
    void stopConsoleInputThread();
    /** @brief 向原生控制台写入一行文本 */
    void writeNativeConsoleLine(const QString& line);
    /** @brief 向原生控制台写入交互提示符 */
    void writeNativeConsolePrompt();
    /** @brief 向原生控制台写入文本（不追加换行） */
    void writeNativeConsoleText(const QString& text);

#ifdef Q_OS_WIN
    /** @brief 控制台输入监听循环 (Windows 平台) */
    void consoleInputLoop();
#endif

    /** @brief 全局 Qt 消息处理器回调 */
    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);
    /** @brief 将 QtMsgType 转为可读的日志级别字符串 */
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

    /** @brief 当前实例指针（仅允许一个实例） */
    static std::atomic<DebugTerminalManager*> s_instance;
    /** @brief 接管之前注册的 Qt 消息处理器 */
    static QtMessageHandler s_previousHandler;
};
