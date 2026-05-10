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

bool isSameFilePath(const std::wstring& leftPath, const std::wstring& rightPath)
{
    return normalizeFilePath(leftPath) == normalizeFilePath(rightPath);
}

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
