#include "DebugTerminalManager.h"

#include "ActiveNoiseCancellation.h"
#include "AdaptiveNoiseReduction.h"
#include "MotionArtifactReduction.h"
#include "TransientNoiseSuppressor.h"
#include "WAVHandle.h"
#include "WaveletTransform.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QDateTime>
#include <QGuiApplication>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QSysInfo>
#include <QWindow>
#include <QtGlobal>
#include <algorithm>
#include <cwchar>
#include <cstdio>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace {
bool parseOnOffToken(const QString& token, bool& enabled)
{
    if (token.compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0) {
        enabled = true;
        return true;
    }

    if (token.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0) {
        enabled = false;
        return true;
    }

    return false;
}

#ifdef Q_OS_WIN
constexpr WORD kCmdConsoleAttributes =
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

constexpr COLORREF kCmdBackgroundColor = RGB(12, 12, 12);
constexpr COLORREF kCmdForegroundColor = RGB(204, 204, 204);
constexpr COLORREF kCmdBrightForegroundColor = RGB(242, 242, 242);
constexpr COLORREF kCmdCaptionColor = RGB(3, 3, 3);
constexpr COLORREF kCmdCaptionTextColor = RGB(255, 255, 255);
constexpr SHORT kConsoleScrollbackLines = 9999;
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmUseImmersiveDarkModeBefore20H1 = 19;
constexpr DWORD kDwmBorderColor = 34;
constexpr DWORD kDwmCaptionColor = 35;
constexpr DWORD kDwmTextColor = 36;

using DwmSetWindowAttributeFunc = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
using ExtractIconExWFunc = UINT(WINAPI*)(LPCWSTR, int, HICON*, HICON*, UINT);

HICON g_consoleLargeIcon = nullptr;
HICON g_consoleSmallIcon = nullptr;

bool hasUsableArea(const RECT& rect)
{
    return rect.right > rect.left && rect.bottom > rect.top;
}

bool isApplicationWindowCandidate(QWindow* window)
{
    if (window == nullptr || !window->isVisible() || window->transientParent() != nullptr) {
        return false;
    }

    const Qt::WindowType windowType = window->type();
    if (windowType == Qt::Popup
        || windowType == Qt::Tool
        || windowType == Qt::ToolTip
        || windowType == Qt::SplashScreen) {
        return false;
    }

    HWND windowHandle = reinterpret_cast<HWND>(window->winId());
    return windowHandle != nullptr && windowHandle != GetConsoleWindow();
}

bool qtWindowRect(QWindow* window, RECT* windowRect)
{
    if (!isApplicationWindowCandidate(window) || windowRect == nullptr) {
        return false;
    }

    HWND windowHandle = reinterpret_cast<HWND>(window->winId());
    RECT nativeRect = {};
    if (GetWindowRect(windowHandle, &nativeRect) != FALSE && hasUsableArea(nativeRect)) {
        *windowRect = nativeRect;
        return true;
    }

    const QRect geometry = window->geometry();
    const qreal scale = window->devicePixelRatio();
    const RECT scaledRect = {
        qRound(geometry.x() * scale),
        qRound(geometry.y() * scale),
        qRound((geometry.x() + geometry.width()) * scale),
        qRound((geometry.y() + geometry.height()) * scale)};

    if (!hasUsableArea(scaledRect)) {
        return false;
    }

    *windowRect = scaledRect;
    return true;
}

RECT applicationWindowRect()
{
    RECT focusedRect = {};
    if (qtWindowRect(QGuiApplication::focusWindow(), &focusedRect)) {
        return focusedRect;
    }

    RECT bestRect = {};
    LONG bestArea = 0;
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow* window : windows) {
        RECT windowRect = {};
        if (!qtWindowRect(window, &windowRect)) {
            continue;
        }

        const LONG width = std::max<LONG>(0, windowRect.right - windowRect.left);
        const LONG height = std::max<LONG>(0, windowRect.bottom - windowRect.top);
        const LONG area = width * height;
        if (area > bestArea) {
            bestArea = area;
            bestRect = windowRect;
        }
    }

    return bestRect;
}

void forceNormalWindowPlacement(HWND window, const RECT& targetRect)
{
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(window, &placement) == FALSE) {
        return;
    }

    placement.flags = 0;
    placement.showCmd = SW_SHOWNORMAL;
    placement.rcNormalPosition = targetRect;
    SetWindowPlacement(window, &placement);
}

bool matchConsoleWindowToApplicationWindow(HWND consoleWindow)
{
    const RECT applicationRect = applicationWindowRect();
    const LONG width = applicationRect.right - applicationRect.left;
    const LONG height = applicationRect.bottom - applicationRect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    forceNormalWindowPlacement(consoleWindow, applicationRect);
    return SetWindowPos(
        consoleWindow,
        nullptr,
        applicationRect.left,
        applicationRect.top,
        width,
        height,
        SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE) != FALSE;
}

void fitConsoleBufferToWindow()
{
    const HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (outputHandle == INVALID_HANDLE_VALUE || outputHandle == nullptr) {
        return;
    }

    CONSOLE_SCREEN_BUFFER_INFO screenBufferInfo = {};
    if (!GetConsoleScreenBufferInfo(outputHandle, &screenBufferInfo)) {
        return;
    }

    const SHORT visibleHeight = static_cast<SHORT>(
        screenBufferInfo.srWindow.Bottom - screenBufferInfo.srWindow.Top + 1);
    const COORD size = {
        static_cast<SHORT>(screenBufferInfo.srWindow.Right - screenBufferInfo.srWindow.Left + 1),
        std::max(visibleHeight, kConsoleScrollbackLines)};
    if (screenBufferInfo.dwSize.X == size.X && screenBufferInfo.dwSize.Y == size.Y) {
        return;
    }

    if (screenBufferInfo.srWindow.Bottom >= size.Y || screenBufferInfo.srWindow.Right >= size.X) {
        SMALL_RECT visibleRect = {0, 0, static_cast<SHORT>(size.X - 1), static_cast<SHORT>(size.Y - 1)};
        SetConsoleWindowInfo(outputHandle, TRUE, &visibleRect);
    }
    SetConsoleScreenBufferSize(outputHandle, size);
}

DwmSetWindowAttributeFunc resolveDwmSetWindowAttribute()
{
    static HMODULE dwmLibrary = LoadLibraryW(L"dwmapi.dll");
    if (dwmLibrary == nullptr) {
        return nullptr;
    }

    static auto function = reinterpret_cast<DwmSetWindowAttributeFunc>(
        GetProcAddress(dwmLibrary, "DwmSetWindowAttribute"));
    return function;
}

ExtractIconExWFunc resolveExtractIconExW()
{
    static HMODULE shellLibrary = LoadLibraryW(L"shell32.dll");
    if (shellLibrary == nullptr) {
        return nullptr;
    }

    static auto function = reinterpret_cast<ExtractIconExWFunc>(
        GetProcAddress(shellLibrary, "ExtractIconExW"));
    return function;
}

void setDwmWindowAttribute(HWND window, DWORD attribute, const void* value, DWORD valueSize)
{
    DwmSetWindowAttributeFunc function = resolveDwmSetWindowAttribute();
    if (function != nullptr) {
        function(window, attribute, value, valueSize);
    }
}

void applyDarkConsoleFrame(HWND window)
{
    const BOOL darkMode = TRUE;
    setDwmWindowAttribute(
        window,
        kDwmUseImmersiveDarkMode,
        &darkMode,
        sizeof(darkMode));
    setDwmWindowAttribute(
        window,
        kDwmUseImmersiveDarkModeBefore20H1,
        &darkMode,
        sizeof(darkMode));

    const COLORREF captionColor = kCmdCaptionColor;
    const COLORREF captionTextColor = kCmdCaptionTextColor;
    setDwmWindowAttribute(window, kDwmCaptionColor, &captionColor, sizeof(captionColor));
    setDwmWindowAttribute(window, kDwmBorderColor, &captionColor, sizeof(captionColor));
    setDwmWindowAttribute(window, kDwmTextColor, &captionTextColor, sizeof(captionTextColor));
}

void applyConsoleWindowButtons(HWND window)
{
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    SetWindowLongPtrW(
        window,
        GWL_STYLE,
        (style | WS_SYSMENU | WS_MINIMIZEBOX) & ~(WS_MAXIMIZEBOX | WS_THICKFRAME));

    if (HMENU systemMenu = GetSystemMenu(window, FALSE)) {
        EnableMenuItem(systemMenu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
        DeleteMenu(systemMenu, SC_SIZE, MF_BYCOMMAND);
        DeleteMenu(systemMenu, SC_MAXIMIZE, MF_BYCOMMAND);
    }

    SetWindowPos(
        window,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    DrawMenuBar(window);
}

void applyConsolePalette(HANDLE outputHandle)
{
    CONSOLE_SCREEN_BUFFER_INFOEX screenBufferInfo = {};
    screenBufferInfo.cbSize = sizeof(screenBufferInfo);
    if (!GetConsoleScreenBufferInfoEx(outputHandle, &screenBufferInfo)) {
        return;
    }

    screenBufferInfo.wAttributes = kCmdConsoleAttributes;
    for (COLORREF& color : screenBufferInfo.ColorTable) {
        color = kCmdBackgroundColor;
    }

    screenBufferInfo.ColorTable[7] = kCmdForegroundColor;
    screenBufferInfo.ColorTable[15] = kCmdBrightForegroundColor;
    SetConsoleScreenBufferInfoEx(outputHandle, &screenBufferInfo);
}

void resizeNativeConsoleToApplicationWindow(HWND consoleWindow)
{
    if (consoleWindow == nullptr) {
        return;
    }

    const HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (outputHandle != INVALID_HANDLE_VALUE && outputHandle != nullptr) {
        applyConsolePalette(outputHandle);
        SetConsoleTextAttribute(outputHandle, kCmdConsoleAttributes);
    }

    matchConsoleWindowToApplicationWindow(consoleWindow);
    fitConsoleBufferToWindow();
}

bool extractApplicationIcons(HICON* largeIcon, HICON* smallIcon)
{
    *largeIcon = nullptr;
    *smallIcon = nullptr;

    ExtractIconExWFunc extractIconExW = resolveExtractIconExW();
    if (extractIconExW == nullptr) {
        return false;
    }

    wchar_t applicationPath[MAX_PATH] = {};
    const DWORD applicationPathLength = GetModuleFileNameW(nullptr, applicationPath, MAX_PATH);
    if (applicationPathLength == 0 || applicationPathLength >= MAX_PATH) {
        return false;
    }

    const UINT iconCount = extractIconExW(applicationPath, 0, largeIcon, smallIcon, 1);
    return iconCount != 0
        && iconCount != static_cast<UINT>(-1)
        && (*largeIcon != nullptr || *smallIcon != nullptr);
}
#endif

QString fallbackLevelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }

    return QStringLiteral("LOG");
}

void writeToFallbackHandler(
    QtMsgType type,
    const QMessageLogContext& context,
    const QString& message)
{
    QString line = QStringLiteral("%1: %2").arg(fallbackLevelName(type), message);
    if (context.file != nullptr && context.line > 0) {
        line += QStringLiteral(" (%1:%2)").arg(QString::fromLatin1(context.file)).arg(context.line);
    }
    line += QLatin1Char('\n');

#ifdef Q_OS_WIN
    OutputDebugStringW(reinterpret_cast<const wchar_t*>(line.utf16()));
#else
    const QByteArray bytes = line.toLocal8Bit();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stderr);
    std::fflush(stderr);
#endif
}
}

#ifdef Q_OS_WIN
void applyCmdConsoleStyle();
void applyNativeConsoleIcon(HWND consoleWindow);
void releaseNativeConsoleIcons();
#endif

std::atomic<DebugTerminalManager*> DebugTerminalManager::s_instance = nullptr;
QtMessageHandler DebugTerminalManager::s_previousHandler = nullptr;

DebugTerminalManager::DebugTerminalManager(QObject* parent)
    : QObject(parent)
{
    s_instance.store(this, std::memory_order_release);
    s_previousHandler = qInstallMessageHandler(DebugTerminalManager::messageHandler);
    appendEntry(QStringLiteral("INFO"), QStringLiteral("调试终端已就绪"));
}

DebugTerminalManager::~DebugTerminalManager()
{
    shutdown();
}

void DebugTerminalManager::shutdown()
{
    bool expected = false;
    if (!m_shuttingDown.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    DebugTerminalManager* expectedInstance = this;
    if (s_instance.compare_exchange_strong(
            expectedInstance,
            nullptr,
            std::memory_order_acq_rel)) {
        qInstallMessageHandler(s_previousHandler);
        s_previousHandler = nullptr;
    }

    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::removePostedEvents(this);
    }

    stopConsoleInputThread();

    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::removePostedEvents(this);
    }
}

QStringList DebugTerminalManager::entries() const
{
    QMutexLocker locker(&m_mutex);
    return m_entries;
}

int DebugTerminalManager::maximumEntries() const
{
    QMutexLocker locker(&m_mutex);
    return m_maximumEntries;
}

void DebugTerminalManager::setMaximumEntries(int maximumEntries)
{
    const int boundedMaximumEntries = qBound(50, maximumEntries, 2000);

    {
        QMutexLocker locker(&m_mutex);
        if (m_maximumEntries == boundedMaximumEntries) {
            return;
        }

        m_maximumEntries = boundedMaximumEntries;
        while (m_entries.size() > m_maximumEntries) {
            m_entries.removeFirst();
        }
    }

    emit maximumEntriesChanged();
    emit entriesChanged();
}

bool DebugTerminalManager::nativeConsoleVisible() const
{
    return m_nativeConsoleVisible;
}

void DebugTerminalManager::appendInfo(const QString& message)
{
    if (m_shuttingDown.load(std::memory_order_acquire)) {
        return;
    }

    appendEntry(QStringLiteral("INFO"), message);
}

void DebugTerminalManager::clear()
{
    if (m_shuttingDown.load(std::memory_order_acquire)) {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        m_entries.clear();
    }

    clearNativeConsole();
    emit entriesChanged();
}

void DebugTerminalManager::setNativeConsoleVisible(bool visible)
{
    if (m_shuttingDown.load(std::memory_order_acquire)) {
        return;
    }

#ifdef Q_OS_WIN
    if (visible) {
        if (!ensureNativeConsole()) {
            appendEntry(QStringLiteral("ERROR"), QStringLiteral("无法打开 Windows 原生调试终端"));
            return;
        }
    }

    if (m_nativeConsoleVisible == visible) {
        if (visible) {
            showNativeConsole(true);
        }
        return;
    }

    m_nativeConsoleVisible = visible;
    showNativeConsole(visible);

    if (visible) {
        writeNativeConsolePrompt();
    }

    emit nativeConsoleVisibleChanged();
#else
    if (visible) {
        appendEntry(QStringLiteral("WARN"), QStringLiteral("原生调试终端仅在 Windows 上可用"));
    }
#endif
}

void DebugTerminalManager::submit(const QString& command)
{
    if (m_shuttingDown.load(std::memory_order_acquire)) {
        return;
    }

    const QString trimmedCommand = command.trimmed();
    if (trimmedCommand.isEmpty()) {
        writeNativeConsolePrompt();
        return;
    }

    if (trimmedCommand.compare(QStringLiteral("clear"), Qt::CaseInsensitive) == 0) {
        clear();
        writeNativeConsolePrompt();
        return;
    }

    if (trimmedCommand.compare(QStringLiteral("debug"), Qt::CaseInsensitive) == 0) {
        appendEntry(
            QStringLiteral("DEBUG"),
            QStringLiteral("%1 %2 | Qt %3 | %4 | %5")
                .arg(
                    QCoreApplication::applicationName(),
                    QCoreApplication::applicationVersion(),
                    QString::fromLatin1(qVersion()),
                    QSysInfo::prettyProductName(),
                    QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));
        writeNativeConsolePrompt();
        return;
    }

    if (trimmedCommand.compare(QStringLiteral("time"), Qt::CaseInsensitive) == 0) {
        appendEntry(
            QStringLiteral("INFO"),
            QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
        writeNativeConsolePrompt();
        return;
    }

    const QStringList commandParts =
        trimmedCommand.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (commandParts.size() >= 3 &&
        commandParts[0].compare(QStringLiteral("config"), Qt::CaseInsensitive) == 0 &&
        commandParts[1].compare(QStringLiteral("save"), Qt::CaseInsensitive) == 0 &&
        commandParts[2].compare(QStringLiteral("channel"), Qt::CaseInsensitive) == 0) {
        if (commandParts.size() != 5) {
            appendEntry(
                QStringLiteral("WARN"),
                QStringLiteral("Usage: config save channel 8 on|off"));
            writeNativeConsolePrompt();
            return;
        }

        bool channelParsed = false;
        const int channelNumber = commandParts[3].toInt(&channelParsed);
        if (!channelParsed || channelNumber != 8) {
            appendEntry(
                QStringLiteral("WARN"),
                QStringLiteral("Only channel 8 reference noise save config is supported"));
            writeNativeConsolePrompt();
            return;
        }

        const bool enableSave =
            commandParts[4].compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0;
        const bool disableSave =
            commandParts[4].compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0;
        if (!enableSave && !disableSave) {
            appendEntry(
                QStringLiteral("WARN"),
                QStringLiteral("Expected on or off for channel 8 save config"));
            writeNativeConsolePrompt();
            return;
        }

        WAVHandle::instance()->setReferenceNoiseChannelSaveEnabled(enableSave);
        appendEntry(
            QStringLiteral("INFO"),
            QStringLiteral("Reference noise channel save is %1")
                .arg(enableSave ? QStringLiteral("on") : QStringLiteral("off")));
        writeNativeConsolePrompt();
        return;
    }

    if (commandParts.size() >= 2 &&
        commandParts[1].compare(QStringLiteral("log"), Qt::CaseInsensitive) == 0 &&
        (commandParts[0].compare(QStringLiteral("anc"), Qt::CaseInsensitive) == 0 ||
         commandParts[0].compare(QStringLiteral("anr"), Qt::CaseInsensitive) == 0 ||
         commandParts[0].compare(QStringLiteral("wavelet"), Qt::CaseInsensitive) == 0 ||
         commandParts[0].compare(QStringLiteral("tns"), Qt::CaseInsensitive) == 0 ||
         commandParts[0].compare(QStringLiteral("mar"), Qt::CaseInsensitive) == 0)) {
        if (commandParts.size() != 3) {
            appendEntry(
                QStringLiteral("WARN"),
                QStringLiteral("Usage: anc log on|off / anr log on|off / wavelet log on|off / tns log on|off / mar log on|off"));
            writeNativeConsolePrompt();
            return;
        }

        bool enableLog = false;
        if (!parseOnOffToken(commandParts[2], enableLog)) {
            appendEntry(
                QStringLiteral("WARN"),
                QStringLiteral("Expected on or off for algorithm log command"));
            writeNativeConsolePrompt();
            return;
        }

        if (commandParts[0].compare(QStringLiteral("anc"), Qt::CaseInsensitive) == 0) {
            ActiveNoiseCancellation::setDebugLoggingEnabled(enableLog);
            appendEntry(
                QStringLiteral("INFO"),
                QStringLiteral("ANC debug log is %1")
                    .arg(enableLog ? QStringLiteral("on") : QStringLiteral("off")));
        } else if (commandParts[0].compare(QStringLiteral("anr"), Qt::CaseInsensitive) == 0) {
            AdaptiveNoiseReduction::setDebugLoggingEnabled(enableLog);
            appendEntry(
                QStringLiteral("INFO"),
                QStringLiteral("ANR debug log is %1")
                    .arg(enableLog ? QStringLiteral("on") : QStringLiteral("off")));
        } else if (commandParts[0].compare(QStringLiteral("wavelet"), Qt::CaseInsensitive) == 0) {
            WaveletTransform::setDebugLoggingEnabled(enableLog);
            appendEntry(
                QStringLiteral("INFO"),
                QStringLiteral("Wavelet debug log is %1")
                    .arg(enableLog ? QStringLiteral("on") : QStringLiteral("off")));
        } else if (commandParts[0].compare(QStringLiteral("tns"), Qt::CaseInsensitive) == 0) {
            TransientNoiseSuppressor::setDebugLoggingEnabled(enableLog);
            appendEntry(
                QStringLiteral("INFO"),
                QStringLiteral("TNS debug log is %1")
                    .arg(enableLog ? QStringLiteral("on") : QStringLiteral("off")));
        } else if (commandParts[0].compare(QStringLiteral("mar"), Qt::CaseInsensitive) == 0) {
            MotionArtifactReduction::setDebugLoggingEnabled(enableLog);
            appendEntry(
                QStringLiteral("INFO"),
                QStringLiteral("MAR debug log is %1")
                    .arg(enableLog ? QStringLiteral("on") : QStringLiteral("off")));
        }

        writeNativeConsolePrompt();
        return;
    }

    if (trimmedCommand.compare(QStringLiteral("help"), Qt::CaseInsensitive) == 0) {
        appendEntry(
            QStringLiteral("INFO"),
            QStringLiteral("Config: config save channel 8 on|off"));
        appendEntry(
            QStringLiteral("INFO"),
            QStringLiteral("Logs: anc log on|off / anr log on|off / wavelet log on|off / tns log on|off / mar log on|off"));
        appendEntry(QStringLiteral("INFO"), QStringLiteral("命令: debug / time / clear / help"));
        writeNativeConsolePrompt();
        return;
    }

    appendEntry(QStringLiteral("INFO"), trimmedCommand);
    writeNativeConsolePrompt();
}

void DebugTerminalManager::appendEntry(const QString& level, const QString& message)
{
    if (m_shuttingDown.load(std::memory_order_acquire)) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString entry = level == QStringLiteral("INFO")
        ? QStringLiteral("[%1] %2").arg(timestamp, message)
        : QStringLiteral("[%1] %2 %3").arg(timestamp, level, message);

    {
        QMutexLocker locker(&m_mutex);
        m_entries.append(entry);
        while (m_entries.size() > m_maximumEntries) {
            m_entries.removeFirst();
        }
    }

    writeNativeConsoleLine(entry);
    emit entriesChanged();
}

void DebugTerminalManager::clearNativeConsole()
{
#ifdef Q_OS_WIN
    if (!m_nativeConsoleAllocated) {
        return;
    }

    const HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (outputHandle == INVALID_HANDLE_VALUE || outputHandle == nullptr) {
        return;
    }

    CONSOLE_SCREEN_BUFFER_INFO screenBufferInfo = {};
    if (!GetConsoleScreenBufferInfo(outputHandle, &screenBufferInfo)) {
        return;
    }

    const DWORD cellCount = static_cast<DWORD>(
        screenBufferInfo.dwSize.X * screenBufferInfo.dwSize.Y);
    const COORD homeCoordinates = {0, 0};
    DWORD written = 0;

    FillConsoleOutputCharacterW(outputHandle, L' ', cellCount, homeCoordinates, &written);
    FillConsoleOutputAttribute(
        outputHandle,
        kCmdConsoleAttributes,
        cellCount,
        homeCoordinates,
        &written);
    SetConsoleCursorPosition(outputHandle, homeCoordinates);
    SetConsoleTextAttribute(outputHandle, kCmdConsoleAttributes);
#endif
}

bool DebugTerminalManager::ensureNativeConsole()
{
#ifdef Q_OS_WIN
    if (m_nativeConsoleAllocated) {
        return true;
    }

    m_ownsNativeConsole = AllocConsole();
    if (!m_ownsNativeConsole && GetLastError() != ERROR_ACCESS_DENIED) {
        return false;
    }

    m_nativeConsoleAllocated = true;
    if (m_ownsNativeConsole) {
        if (HWND consoleWindow = GetConsoleWindow()) {
            ShowWindow(consoleWindow, SW_HIDE);
        }
    }
    SetConsoleTitleW(L"BSSAS 调试终端");
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    FILE* stream = nullptr;
    freopen_s(&stream, "CONIN$", "r", stdin);
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    _setmode(_fileno(stdin), _O_U16TEXT);

    applyCmdConsoleStyle();
    if (HWND consoleWindow = GetConsoleWindow()) {
        applyDarkConsoleFrame(consoleWindow);
        applyConsoleWindowButtons(consoleWindow);
        applyNativeConsoleIcon(consoleWindow);
        resizeNativeConsoleToApplicationWindow(consoleWindow);
    }
    clearNativeConsole();
    for (const QString& entry : entries()) {
        writeNativeConsoleLine(entry);
    }
    writeNativeConsoleLine(QStringLiteral("命令: debug / time / clear / help"));
    startConsoleInputThread();
    return true;
#else
    return false;
#endif
}

void DebugTerminalManager::showNativeConsole(bool visible)
{
#ifdef Q_OS_WIN
    if (!m_nativeConsoleAllocated) {
        return;
    }

    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow == nullptr) {
        return;
    }

    if (visible) {
        resizeNativeConsoleToApplicationWindow(consoleWindow);
        applyDarkConsoleFrame(consoleWindow);
        applyConsoleWindowButtons(consoleWindow);
        ShowWindow(consoleWindow, SW_SHOW);
        SetForegroundWindow(consoleWindow);
    } else {
        ShowWindow(consoleWindow, SW_HIDE);
    }
#else
    Q_UNUSED(visible)
#endif
}

void DebugTerminalManager::startConsoleInputThread()
{
#ifdef Q_OS_WIN
    if (m_inputThreadRunning) {
        return;
    }

    m_inputThreadRunning = true;
    m_inputThread = std::thread(&DebugTerminalManager::consoleInputLoop, this);
#endif
}

void DebugTerminalManager::stopConsoleInputThread()
{
#ifdef Q_OS_WIN
    m_inputThreadRunning = false;

    if (m_inputThread.joinable()) {
        CancelSynchronousIo(static_cast<HANDLE>(m_inputThread.native_handle()));

        const HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
        if (inputHandle != INVALID_HANDLE_VALUE && inputHandle != nullptr) {
            INPUT_RECORD inputRecords[2] = {};
            inputRecords[0].EventType = KEY_EVENT;
            inputRecords[0].Event.KeyEvent.bKeyDown = TRUE;
            inputRecords[0].Event.KeyEvent.wRepeatCount = 1;
            inputRecords[0].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
            inputRecords[0].Event.KeyEvent.uChar.UnicodeChar = L'\r';

            inputRecords[1] = inputRecords[0];
            inputRecords[1].Event.KeyEvent.bKeyDown = FALSE;

            DWORD written = 0;
            WriteConsoleInputW(inputHandle, inputRecords, 2, &written);
        }

        m_inputThread.join();
    }

    releaseNativeConsoleIcons();

    if (m_nativeConsoleAllocated && m_ownsNativeConsole) {
        FreeConsole();
    }

    m_nativeConsoleAllocated = false;
    m_nativeConsoleVisible = false;
    m_ownsNativeConsole = false;
#endif
}

void DebugTerminalManager::writeNativeConsoleLine(const QString& line)
{
    writeNativeConsoleText(line + QLatin1Char('\n'));
}

void DebugTerminalManager::writeNativeConsolePrompt()
{
    if (m_nativeConsoleVisible) {
        writeNativeConsoleText(QStringLiteral("> "));
    }
}

void DebugTerminalManager::writeNativeConsoleText(const QString& text)
{
#ifdef Q_OS_WIN
    if (m_shuttingDown.load(std::memory_order_acquire) || !m_nativeConsoleAllocated) {
        return;
    }

    const HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (outputHandle == INVALID_HANDLE_VALUE || outputHandle == nullptr) {
        return;
    }

    DWORD written = 0;
    const BOOL writeSucceeded = WriteConsoleW(
        outputHandle,
        reinterpret_cast<const wchar_t*>(text.utf16()),
        static_cast<DWORD>(text.size()),
        &written,
        nullptr);

    if (!writeSucceeded) {
        const QByteArray bytes = text.toLocal8Bit();
        std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
        std::fflush(stdout);
    }
#else
    Q_UNUSED(text)
#endif
}

#ifdef Q_OS_WIN
void applyCmdConsoleStyle()
{
    const HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (outputHandle == INVALID_HANDLE_VALUE || outputHandle == nullptr) {
        return;
    }

    SetConsoleTextAttribute(outputHandle, kCmdConsoleAttributes);
    applyConsolePalette(outputHandle);

    const HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
    if (inputHandle != INVALID_HANDLE_VALUE && inputHandle != nullptr) {
        DWORD inputMode = 0;
        if (GetConsoleMode(inputHandle, &inputMode)) {
            inputMode |= ENABLE_EXTENDED_FLAGS;
            inputMode &= ~(ENABLE_QUICK_EDIT_MODE | ENABLE_MOUSE_INPUT);
            SetConsoleMode(inputHandle, inputMode);
        }
    }

    CONSOLE_FONT_INFOEX fontInfo = {};
    fontInfo.cbSize = sizeof(fontInfo);
    if (GetCurrentConsoleFontEx(outputHandle, FALSE, &fontInfo)) {
        fontInfo.dwFontSize.X = 0;
        fontInfo.dwFontSize.Y = 18;
        fontInfo.FontFamily = FF_DONTCARE;
        fontInfo.FontWeight = FW_NORMAL;
        wcscpy_s(fontInfo.FaceName, L"Consolas");
        SetCurrentConsoleFontEx(outputHandle, FALSE, &fontInfo);
    }

    CONSOLE_CURSOR_INFO cursorInfo = {};
    if (GetConsoleCursorInfo(outputHandle, &cursorInfo)) {
        cursorInfo.bVisible = TRUE;
        cursorInfo.dwSize = 25;
        SetConsoleCursorInfo(outputHandle, &cursorInfo);
    }

    applyConsolePalette(outputHandle);
}

void applyNativeConsoleIcon(HWND consoleWindow)
{
    if (consoleWindow == nullptr) {
        return;
    }

    HICON largeIcon = nullptr;
    HICON smallIcon = nullptr;
    if (!extractApplicationIcons(&largeIcon, &smallIcon)) {
        return;
    }

    releaseNativeConsoleIcons();
    g_consoleLargeIcon = largeIcon;
    g_consoleSmallIcon = smallIcon;

    if (g_consoleLargeIcon != nullptr) {
        SendMessageW(
            consoleWindow,
            WM_SETICON,
            ICON_BIG,
            reinterpret_cast<LPARAM>(g_consoleLargeIcon));
    }

    if (g_consoleSmallIcon != nullptr) {
        SendMessageW(
            consoleWindow,
            WM_SETICON,
            ICON_SMALL,
            reinterpret_cast<LPARAM>(g_consoleSmallIcon));
        SendMessageW(
            consoleWindow,
            WM_SETICON,
            2,
            reinterpret_cast<LPARAM>(g_consoleSmallIcon));
    }
}

void releaseNativeConsoleIcons()
{
    if (g_consoleLargeIcon == nullptr && g_consoleSmallIcon == nullptr) {
        return;
    }

    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow != nullptr) {
        SendMessageW(consoleWindow, WM_SETICON, ICON_BIG, 0);
        SendMessageW(consoleWindow, WM_SETICON, ICON_SMALL, 0);
        SendMessageW(consoleWindow, WM_SETICON, 2, 0);
    }

    if (g_consoleLargeIcon != nullptr) {
        DestroyIcon(g_consoleLargeIcon);
        g_consoleLargeIcon = nullptr;
    }

    if (g_consoleSmallIcon != nullptr) {
        DestroyIcon(g_consoleSmallIcon);
        g_consoleSmallIcon = nullptr;
    }
}

void DebugTerminalManager::consoleInputLoop()
{
    const HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
    if (inputHandle == INVALID_HANDLE_VALUE || inputHandle == nullptr) {
        return;
    }

    wchar_t buffer[512] = {};
    while (m_inputThreadRunning) {
        DWORD readCount = 0;
        const BOOL readSucceeded = ReadConsoleW(
            inputHandle,
            buffer,
            static_cast<DWORD>(std::size(buffer) - 1),
            &readCount,
            nullptr);

        if (!readSucceeded) {
            if (!m_inputThreadRunning) {
                break;
            }

            Sleep(50);
            continue;
        }

        if (readCount == 0) {
            continue;
        }

        const QString command = QString::fromWCharArray(buffer, static_cast<int>(readCount)).trimmed();
        std::fill(std::begin(buffer), std::end(buffer), L'\0');

        if (!m_inputThreadRunning || command.isEmpty()) {
            continue;
        }

        QMetaObject::invokeMethod(
            this,
            [this, command]() {
                if (m_shuttingDown.load(std::memory_order_acquire)) {
                    return;
                }

                submit(command);
            },
            Qt::QueuedConnection);
    }
}
#endif

void DebugTerminalManager::messageHandler(
    QtMsgType type,
    const QMessageLogContext& context,
    const QString& message)
{
    DebugTerminalManager* instance = s_instance.load(std::memory_order_acquire);
    const bool hasManagedNativeConsole =
#ifdef Q_OS_WIN
        instance != nullptr && instance->m_nativeConsoleAllocated;
#else
        false;
#endif

    if (!hasManagedNativeConsole) {
        if (s_previousHandler) {
            s_previousHandler(type, context, message);
        } else {
            writeToFallbackHandler(type, context, message);
        }
    }

    if (instance == nullptr) {
        return;
    }

    if (instance->m_shuttingDown.load(std::memory_order_acquire)) {
        return;
    }

    QString decoratedMessage = message;
    if (context.category != nullptr) {
        const QString category = QString::fromLatin1(context.category);
        if (!category.isEmpty() && category != QStringLiteral("default")) {
            decoratedMessage = QStringLiteral("[%1] %2").arg(category, message);
        }
    }

    const QPointer<DebugTerminalManager> guardedInstance(instance);
    QMetaObject::invokeMethod(
        instance,
        [guardedInstance, type, decoratedMessage]() {
            if (guardedInstance == nullptr ||
                guardedInstance->m_shuttingDown.load(std::memory_order_acquire)) {
                return;
            }

            guardedInstance->appendEntry(levelName(type), decoratedMessage);
        },
        Qt::QueuedConnection);
}

QString DebugTerminalManager::levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }

    return QStringLiteral("LOG");
}
