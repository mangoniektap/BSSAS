/** @file updater_main.cpp
 *  @brief Windows 平台自动更新器入口，负责等待主进程退出、静默安装新版本、清理临时文件并启动更新后的应用程序。
 */

#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cerrno>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

struct UpdaterArguments
{
    DWORD waitPid = 0;
    std::wstring installerPath;
    std::wstring targetPath;
};

/** @brief 去除字符串首尾空白。
 *  @param text 输入宽字符串
 *  @returns 去除首尾空白后的字符串副本
 */
std::wstring trimCopy(const std::wstring& text)
{
    std::size_t start = 0;
    while (start < text.size() && std::iswspace(text[start])) {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::iswspace(text[end - 1])) {
        --end;
    }

    return text.substr(start, end - start);
}

/** @brief 获取当前可执行文件的完整路径。
 *  @returns 当前进程的可执行文件路径，失败时返回空字符串
 */
std::wstring currentExecutablePath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD copiedLength = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (copiedLength == 0) {
            return {};
        }

        if (copiedLength < buffer.size() - 1) {
            buffer.resize(copiedLength);
            return buffer;
        }

        buffer.resize(buffer.size() * 2);
    }
}

/** @brief 将文件路径规范化为绝对路径并统一为小写，便于路径比较。
 *  @param path 输入文件路径
 *  @returns 规范化后的小写绝对路径
 */
std::wstring normalizeFilePath(const std::wstring& path)
{
    std::error_code errorCode;
    std::filesystem::path filesystemPath(path);
    std::filesystem::path absolutePath =
        std::filesystem::absolute(filesystemPath, errorCode);
    if (errorCode) {
        absolutePath = filesystemPath;
    }

    std::wstring normalizedPath = absolutePath.lexically_normal().wstring();
    std::transform(
        normalizedPath.begin(),
        normalizedPath.end(),
        normalizedPath.begin(),
        [](wchar_t character) {
            return static_cast<wchar_t>(std::towlower(character));
        });
    return normalizedPath;
}

/** @brief 判断两个路径是否指向同一个文件（通过规范化后比较）。
 *  @param leftPath 第一个路径
 *  @param rightPath 第二个路径
 *  @returns 若两路径指向同一文件则返回 true
 */
bool isSameFilePath(const std::wstring& leftPath, const std::wstring& rightPath)
{
    return normalizeFilePath(leftPath) == normalizeFilePath(rightPath);
}

/** @brief 将字符串解析为进程 ID (DWORD)。
 *  @param text 输入字符串
 *  @param processId 输出进程 ID
 *  @returns 解析成功返回 true，否则返回 false
 */
bool parseProcessId(const std::wstring& text, DWORD* processId)
{
    if (processId == nullptr || text.empty()) {
        return false;
    }

    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long long parsedValue = _wcstoui64(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != L'\0' || parsedValue == 0 ||
        parsedValue > MAXDWORD) {
        return false;
    }

    *processId = static_cast<DWORD>(parsedValue);
    return true;
}

/** @brief 解析命令行参数，提取更新器所需的等待 PID、安装器路径和目标路径。
 *  @param argc 参数个数
 *  @param argv 参数数组
 *  @param parsedArguments 输出解析后的参数结构
 *  @returns 解析成功返回 true，参数不完整或无效返回 false
 */
bool parseArguments(int argc, wchar_t* argv[], UpdaterArguments* parsedArguments)
{
    if (argc <= 1 || argv == nullptr || parsedArguments == nullptr) {
        return false;
    }

    UpdaterArguments result;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--wait-pid" && index + 1 < argc) {
            if (!parseProcessId(argv[++index], &result.waitPid)) {
                return false;
            }
            continue;
        }

        if (argument == L"--installer" && index + 1 < argc) {
            result.installerPath = argv[++index];
            continue;
        }

        if (argument == L"--target" && index + 1 < argc) {
            result.targetPath = argv[++index];
            continue;
        }
    }

    if (result.waitPid == 0 ||
        trimCopy(result.installerPath).empty() ||
        trimCopy(result.targetPath).empty()) {
        return false;
    }

    *parsedArguments = std::move(result);
    return true;
}

void showErrorMessage(const std::wstring& message)
{
    MessageBoxW(
        nullptr,
        message.c_str(),
        L"Updater",
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

bool fileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/** @brief 等待指定进程退出（最长等待 120 秒）。
 *  @param processId 目标进程 ID
 *  @param errorMessage 输出超时错误信息
 *  @returns 进程已退出或进程句柄无效时返回 true
 */
bool waitForProcessExit(DWORD processId, std::wstring* errorMessage)
{
    HANDLE processHandle = OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (processHandle == nullptr) {
        return true;
    }

    const DWORD waitResult = WaitForSingleObject(processHandle, 120000);
    CloseHandle(processHandle);

    if (waitResult == WAIT_OBJECT_0) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = L"Timed out while waiting for the main application to exit.";
    }
    return false;
}

/** @brief 将路径转义为 PowerShell 单引号字面量，防止特殊字符导致脚本注入。
 *  @param path 待转义的路径
 *  @returns PowerShell 安全引用的路径字符串
 */
std::wstring quoteForPowerShellLiteral(const std::wstring& path)
{
    std::wstring escapedPath = path;
    std::replace(escapedPath.begin(), escapedPath.end(), L'/', L'\\');

    std::size_t index = 0;
    while ((index = escapedPath.find(L'\'', index)) != std::wstring::npos) {
        escapedPath.insert(index, 1, L'\'');
        index += 2;
    }

    return L"'" + escapedPath + L"'";
}

/** @brief 通过 ShellExecuteEx 启动一个独立的子进程。
 *  @param filePath 可执行文件路径
 *  @param parameters 命令行参数
 *  @param workingDirectory 工作目录
 *  @param showCommand 窗口显示方式 (SW_HIDE / SW_SHOWNORMAL)
 *  @returns 启动成功返回 true
 */
bool startDetachedShellProcess(
    const std::wstring& filePath,
    const std::wstring& parameters,
    const std::wstring& workingDirectory,
    int showCommand)
{
    SHELLEXECUTEINFOW executeInfo {};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOASYNC;
    executeInfo.hwnd = nullptr;
    executeInfo.lpVerb = L"open";
    executeInfo.lpFile = filePath.c_str();
    executeInfo.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
    executeInfo.lpDirectory =
        workingDirectory.empty() ? nullptr : workingDirectory.c_str();
    executeInfo.nShow = showCommand;
    return ShellExecuteExW(&executeInfo) == TRUE;
}

/** @brief 通过后台 PowerShell 脚本延迟删除指定的文件/目录。
 *  @param paths 待清理的路径列表
 *  @returns 成功启动后台清理进程返回 true
 */
bool scheduleCleanup(const std::vector<std::wstring>& paths)
{
    std::wstring script = L"Start-Sleep -Seconds 2";
    bool hasCleanupTarget = false;
    for (const std::wstring& path : paths) {
        const std::wstring trimmedPath = trimCopy(path);
        if (trimmedPath.empty()) {
            continue;
        }

        hasCleanupTarget = true;
        const std::wstring quotedPath = quoteForPowerShellLiteral(trimmedPath);
        script +=
            L"; if (Test-Path -LiteralPath " + quotedPath +
            L") { Remove-Item -LiteralPath " + quotedPath +
            L" -Recurse -Force -ErrorAction SilentlyContinue }";
    }

    if (!hasCleanupTarget) {
        return true;
    }

    std::error_code errorCode;
    const std::wstring workingDirectory =
        std::filesystem::temp_directory_path(errorCode).wstring();
    const std::wstring parameters =
        L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden "
        L"-Command \"" +
        script + L"\"";

    return startDetachedShellProcess(
        L"powershell.exe",
        parameters,
        workingDirectory,
        SW_HIDE);
}

/** @brief 以管理员权限静默运行安装程序，等待完成。
 *  @param installerPath 安装程序路径
 *  @param errorMessage 输出失败原因
 *  @returns 安装成功返回 true
 */
bool runInstallerSilently(const std::wstring& installerPath, std::wstring* errorMessage)
{
    SHELLEXECUTEINFOW executeInfo {};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.hwnd = nullptr;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = installerPath.c_str();
    executeInfo.lpParameters =
        L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-";
    executeInfo.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&executeInfo) || executeInfo.hProcess == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage =
                L"Failed to start the installer. Please confirm installation "
                L"permissions are available.";
        }
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(executeInfo.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(executeInfo.hProcess, &exitCode);
    CloseHandle(executeInfo.hProcess);

    if (waitResult != WAIT_OBJECT_0 || (exitCode != 0 && exitCode != 3010)) {
        if (errorMessage != nullptr) {
            *errorMessage =
                L"Installer execution failed with exit code " +
                std::to_wstring(exitCode);
        }
        return false;
    }

    return true;
}

/** @brief 启动更新后的目标应用程序。
 *  @param targetPath 目标可执行文件路径
 *  @returns 启动成功返回 true
 */
bool startTargetApplication(const std::wstring& targetPath)
{
    const std::filesystem::path targetFilesystemPath(targetPath);
    return startDetachedShellProcess(
        targetPath,
        L"",
        targetFilesystemPath.parent_path().wstring(),
        SW_SHOWNORMAL);
}

} // namespace

/** @brief Windows 更新器主入口：解析参数 -> 等待主进程退出 -> 静默安装 -> 清理旧文件 -> 启动新版应用。 */
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        showErrorMessage(L"Failed to parse updater arguments.");
        return 1;
    }

    UpdaterArguments arguments;
    const bool argumentsValid = parseArguments(argc, argv, &arguments);
    LocalFree(argv);
    if (!argumentsValid) {
        showErrorMessage(L"Updater arguments are invalid.");
        return 1;
    }

    const std::wstring selfPath = currentExecutablePath();
    const std::filesystem::path targetFilesystemPath(arguments.targetPath);
    const std::wstring installedUpdaterPath =
        (targetFilesystemPath.parent_path() / L"Updater.exe").wstring();
    const bool cleanupSelfOnExit =
        !selfPath.empty() && !isSameFilePath(selfPath, installedUpdaterPath);
    const std::wstring selfDirectoryPath = selfPath.empty()
        ? std::wstring()
        : std::filesystem::path(selfPath).parent_path().wstring();

    const auto scheduleSelfCleanup = [&]() {
        if (cleanupSelfOnExit) {
            scheduleCleanup({ selfDirectoryPath });
        }
    };

    if (!fileExists(arguments.installerPath)) {
        scheduleSelfCleanup();
        showErrorMessage(L"Installer package not found: " + arguments.installerPath);
        return 1;
    }

    std::wstring errorMessage;
    if (!waitForProcessExit(arguments.waitPid, &errorMessage)) {
        scheduleSelfCleanup();
        showErrorMessage(errorMessage);
        return 1;
    }

    if (!runInstallerSilently(arguments.installerPath, &errorMessage)) {
        scheduleSelfCleanup();
        showErrorMessage(errorMessage);
        return 1;
    }

    std::vector<std::wstring> cleanupPaths { arguments.installerPath };
    if (cleanupSelfOnExit) {
        cleanupPaths.push_back(selfDirectoryPath);
    }
    scheduleCleanup(cleanupPaths);

    if (!startTargetApplication(arguments.targetPath)) {
        showErrorMessage(L"Installation finished, but launching the new version "
                         L"failed: " + arguments.targetPath);
        return 1;
    }

    return 0;
}

#else

int main()
{
    return 1;
}

#endif
