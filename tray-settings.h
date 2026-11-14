#pragma once
#include <mutex>

// Per-user persistence for the tray.
//
// The last selected gamma mode lives in %LOCALAPPDATA%\Fuck-sRGB\settings.ini.
// Keeping it there instead of next to the executable means the preference
// survives an executable in Program Files, an elevated working directory and
// DWM restarts, and that a later run can reapply the selection at logon.
//
// Start-at-logon uses the HKCU Run value, the same mechanism a non-elevated
// application would use. A logon task is deliberately not used: a task at the
// highest run level creates its process with a window station the shell cannot
// reach, so Shell_NotifyIcon, ChangeWindowMessageFilter and process enumeration
// all fail with ERROR_ACCESS_DENIED and the tray never appears. The executable
// is manifested highestAvailable, so Windows asks for elevation once at logon
// and the tray then runs with the same rights as a manually started instance.
//
// Tasks written by the earlier 1.2.x builds are cleaned up once in the background.

constexpr wchar_t settingsSection[] = L"tray";
constexpr wchar_t settingsModeKey[] = L"lastMode";
constexpr wchar_t settingsFolder[] = L"Fuck-sRGB";
constexpr wchar_t settingsFileName[] = L"settings.ini";

constexpr wchar_t autoRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t autoRunValue[] = L"Fuck sRGB";
constexpr wchar_t autoTaskName[] = L"Fuck sRGB";
constexpr wchar_t autoStartArgument[] = L"/autostart";

inline std::wstring executablePath() {
    std::vector<wchar_t> buffer(MAX_PATH * 2, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}

// 缓存配置文件夹路径，消除每次打日志或读配置时频繁的环境变量读取与目录探测 I/O
inline const std::wstring& settingsFolderDirectory() {
    static const std::wstring folder = []() -> std::wstring {
        wchar_t base[MAX_PATH * 2] = {};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
        std::filesystem::path p;
        if (length && length < ARRAYSIZE(base)) {
            p = std::filesystem::path(std::wstring(base, length)) / settingsFolder;
        } else {
            const auto executable = executablePath();
            if (!executable.empty()) {
                p = std::filesystem::path(executable).parent_path() / settingsFolder;
            } else {
                p = std::filesystem::path(L".") / settingsFolder;
            }
        }
        std::error_code error;
        std::filesystem::create_directories(p, error);
        return p.wstring();
    }();
    return folder;
}

// 缓存 settings.ini 完整路径
inline const std::wstring& settingsFile() {
    static const std::wstring file = []() -> std::wstring {
        const auto& folder = settingsFolderDirectory();
        if (folder.empty()) return std::wstring();
        return (std::filesystem::path(folder) / settingsFileName).wstring();
    }();
    return file;
}

// 缓存 startup.log 完整路径
inline const std::wstring& startupLogFile() {
    static const std::wstring log = []() -> std::wstring {
        const auto& folder = settingsFolderDirectory();
        if (folder.empty()) return std::wstring();
        return (std::filesystem::path(folder) / L"startup.log").wstring();
    }();
    return log;
}

// Falls back to the native curve when the stored value is missing or stale.
inline Mode loadSavedMode() {
    const auto& file = settingsFile();
    if (file.empty()) return Mode::Srgb;
    const UINT stored = GetPrivateProfileIntW(settingsSection, settingsModeKey,
                                              static_cast<int>(index(Mode::Srgb)), file.c_str());
    return stored < selectableCount ? static_cast<Mode>(stored) : Mode::Srgb;
}

// 线程安全且高性能的启动与生命周期日志记录
inline void writeStartupLog(const std::wstring& text) {
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lock(logMutex);

    const auto& path = startupLogFile();
    if (path.empty()) return;

    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA | GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 256 * 1024) {
        // 文件超限时在句柄内直接截断，避免关闭重开与竞态
        LARGE_INTEGER zero{};
        SetFilePointerEx(file, zero, nullptr, FILE_BEGIN);
        SetEndOfFile(file);
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[80] = {};
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] ",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
               now.wMilliseconds, GetCurrentProcessId());
    const std::wstring line = std::wstring(stamp) + text + L"\r\n";
    DWORD written = 0;
    WriteFile(file, line.c_str(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
}

inline void saveMode(Mode mode) {
    if (index(mode) >= selectableCount) return;
    const auto& file = settingsFile();
    if (file.empty()) return;
    wchar_t value[16] = {};
    swprintf_s(value, L"%u", index(mode));
    WritePrivateProfileStringW(settingsSection, settingsModeKey, value, file.c_str());
}

// Runs a console helper without showing a window and without inheriting the
// tray's (absent) console. Returns false when the process could not be started.
inline bool runAndWait(const std::wstring& command, DWORD& exitCode) {
    std::vector<wchar_t> line(command.begin(), command.end());
    line.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process))
        return false;
    const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
    DWORD code = static_cast<DWORD>(-1);
    const BOOL read = GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (wait != WAIT_OBJECT_0 || !read) return false;
    exitCode = code;
    return true;
}

inline bool scheduledTaskExists() {
    DWORD code = 1;
    return runAndWait(std::wstring(L"schtasks.exe /Query /TN \"") + autoTaskName + L"\"", code) && code == 0;
}

inline void deleteScheduledTask() {
    DWORD code = 1;
    runAndWait(std::wstring(L"schtasks.exe /Delete /TN \"") + autoTaskName + L"\" /F", code);
}

// 针对 1.2.x 早期版本遗留的任务计划只做单次后台自愈清理，绝不阻塞托盘菜单与主循环
inline void cleanupLegacyTaskOnce() {
    static bool cleaned = false;
    if (cleaned) return;
    cleaned = true;
    std::thread([] {
        try {
            if (scheduledTaskExists()) {
                deleteScheduledTask();
            }
        } catch (...) {}
    }).detach();
}

// 辅助函数：标准化路径并对比
inline bool isPathMatching(const std::wstring& registered, const std::wstring& expected) {
    if (registered.empty() || expected.empty()) return false;
    // 剥离可能存在的双引号与空格
    auto trimQuotes = [](std::wstring s) {
        size_t start = 0;
        while (start < s.size() && (s[start] == L' ' || s[start] == L'"')) ++start;
        size_t end = s.size();
        while (end > start && (s[end - 1] == L' ' || s[end - 1] == L'"')) --end;
        return s.substr(start, end - start);
    };
    // 截取参数前的主程序路径
    std::wstring cleanReg = trimQuotes(registered);
    const size_t argPos = cleanReg.find(L" /");
    if (argPos != std::wstring::npos) {
        cleanReg = trimQuotes(cleanReg.substr(0, argPos));
    }
    std::wstring cleanExp = trimQuotes(expected);
    return _wcsicmp(cleanReg.c_str(), cleanExp.c_str()) == 0;
}

// 高性能注册表检查：避免 64KB 栈内存滥用，并校验路径有效性支持移动自愈
inline bool runEntryExists(bool verifyPath = true) {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, autoRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    wchar_t value[MAX_PATH * 2] = {};
    DWORD bytes = static_cast<DWORD>(sizeof(value));
    DWORD type = 0;
    const LONG status = RegQueryValueExW(key, autoRunValue, nullptr, &type,
                                         reinterpret_cast<LPBYTE>(value), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes <= sizeof(wchar_t))
        return false;

    if (!verifyPath) return true;
    const auto currentExe = executablePath();
    if (currentExe.empty()) return true;
    return isPathMatching(value, currentExe);
}

inline bool writeRunEntry(const std::wstring& path) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, autoRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return false;
    // 添加双引号包裹路径与 /autostart 静默启动标记
    const std::wstring command = L"\"" + path + L"\" " + autoStartArgument;
    const LONG status = RegSetValueExW(key, autoRunValue, 0, REG_SZ,
                                       reinterpret_cast<const BYTE*>(command.c_str()),
                                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

inline void deleteRunEntry() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, autoRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    RegDeleteValueW(key, autoRunValue);
    RegCloseKey(key);
}

// 核心自启判断：纯注册表查询，响应时间小于 0.1ms，杜绝调用 schtasks.exe 引起界面卡死
inline bool autoStartEnabled() {
    return runEntryExists(true);
}

// 启用开机自启：自动更新为当前最新可执行文件路径
inline bool enableAutoStart() {
    const auto executable = executablePath();
    if (executable.empty()) return false;
    cleanupLegacyTaskOnce();
    return writeRunEntry(executable);
}

inline void disableAutoStart() {
    deleteRunEntry();
    cleanupLegacyTaskOnce();
}
