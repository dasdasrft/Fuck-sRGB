#pragma once
#include <mutex>
#include <sddl.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>

// Per-user persistence for the tray.
//
// The last selected gamma mode lives in %LOCALAPPDATA%\Fuck-sRGB\settings.ini.
// Keeping it there instead of next to the executable means the preference
// survives an executable in Program Files, an elevated working directory and
// DWM restarts, and that a later run can reapply the selection at logon.
//
// Start-at-logon uses a Windows Task Scheduler logon task at the highest run
// level. This allows the tray to start at user sign-in with administrative
// rights without triggering a UAC prompt. A 3-second delay ensures Explorer
// and the notification area are initialized. The preference is also mirrored
// in settings.ini for self-healing and instant status checks.

constexpr wchar_t settingsSection[] = L"tray";
constexpr wchar_t settingsModeKey[] = L"lastMode";
constexpr wchar_t settingsAutoStartKey[] = L"autoStart";
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

inline int loadAutoStartSetting() {
    const auto& file = settingsFile();
    if (file.empty()) return -1;
    return GetPrivateProfileIntW(settingsSection, settingsAutoStartKey, -1, file.c_str());
}

inline void saveAutoStartSetting(bool enabled) {
    const auto& file = settingsFile();
    if (file.empty()) return;
    WritePrivateProfileStringW(settingsSection, settingsAutoStartKey, enabled ? L"1" : L"0", file.c_str());
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

inline std::wstring currentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::wstring result;
    if (size) {
        std::vector<Byte> buffer(size);
        if (GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
            const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
            wchar_t* text = nullptr;
            if (ConvertSidToStringSidW(user->User.Sid, &text)) {
                result = text;
                LocalFree(text);
            }
        }
    }
    CloseHandle(token);
    return result;
}

inline std::wstring xmlText(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t character : text) {
        switch (character) {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'"': out += L"&quot;"; break;
        default: out += character; break;
        }
    }
    return out;
}

inline std::wstring logonTaskXml(const std::wstring& executable, const std::wstring& sid) {
    const std::wstring path = xmlText(executable);
    const std::wstring user = xmlText(sid);
    return L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
           L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
           L"  <RegistrationInfo>\n"
           L"    <Description>Starts Fuck sRGB at logon without a UAC prompt.</Description>\n"
           L"  </RegistrationInfo>\n"
           L"  <Triggers>\n"
           L"    <LogonTrigger>\n"
           L"      <Enabled>true</Enabled>\n"
           L"      <UserId>" + user + L"</UserId>\n"
           L"      <Delay>PT3S</Delay>\n"
           L"    </LogonTrigger>\n"
           L"  </Triggers>\n"
           L"  <Principals>\n"
           L"    <Principal id=\"Author\">\n"
           L"      <UserId>" + user + L"</UserId>\n"
           L"      <LogonType>InteractiveToken</LogonType>\n"
           L"      <RunLevel>HighestAvailable</RunLevel>\n"
           L"    </Principal>\n"
           L"  </Principals>\n"
           L"  <Settings>\n"
           L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
           L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
           L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
           L"    <AllowHardTerminate>true</AllowHardTerminate>\n"
           L"    <StartWhenAvailable>true</StartWhenAvailable>\n"
           L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\n"
           L"    <IdleSettings>\n"
           L"      <StopOnIdleEnd>false</StopOnIdleEnd>\n"
           L"      <RestartOnIdle>false</RestartOnIdle>\n"
           L"    </IdleSettings>\n"
           L"    <AllowStartOnDemand>true</AllowStartOnDemand>\n"
           L"    <Enabled>true</Enabled>\n"
           L"    <Hidden>false</Hidden>\n"
           L"    <RunOnlyIfIdle>false</RunOnlyIfIdle>\n"
           L"    <WakeToRun>false</WakeToRun>\n"
           L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\n"
           L"    <Priority>7</Priority>\n"
           L"  </Settings>\n"
           L"  <Actions Context=\"Author\">\n"
           L"    <Exec>\n"
           L"      <Command>" + path + L"</Command>\n"
           L"      <Arguments>" + autoStartArgument + L"</Arguments>\n"
           L"    </Exec>\n"
           L"  </Actions>\n"
           L"</Task>\n";
}

inline bool createLogonTask(const std::wstring& executable) {
    const auto sid = currentUserSid();
    if (sid.empty()) {
        writeStartupLog(L"createLogonTask: sid is empty");
        return false;
    }
    wchar_t folder[MAX_PATH * 2] = {};
    if (!GetTempPathW(ARRAYSIZE(folder), folder)) return false;
    const std::wstring file = std::wstring(folder) + L"Fuck-sRGB-task-" +
                              std::to_wstring(GetCurrentProcessId()) + L".xml";
    const std::wstring xml = logonTaskXml(executable, sid);
    {
        std::ofstream out(std::filesystem::path{file}, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        const unsigned char bom[] = {0xFF, 0xFE};
        out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        out.write(reinterpret_cast<const char*>(xml.data()),
                  static_cast<std::streamsize>(xml.size() * sizeof(wchar_t)));
        if (!out) { out.close(); std::error_code ignored; std::filesystem::remove(file, ignored); return false; }
    }
    DWORD code = 1;
    const std::wstring create = std::wstring(L"schtasks.exe /Create /TN \"") + autoTaskName +
                                L"\" /XML \"" + file + L"\" /F";
    const bool created = runAndWait(create, code) && code == 0;
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
    writeStartupLog(L"createLogonTask: created=" + std::to_wstring(created ? 1 : 0) +
                    L" code=" + std::to_wstring(code));
    return created;
}

inline bool scheduledTaskExists() {
    wchar_t winDir[MAX_PATH] = {};
    const UINT len = GetSystemWindowsDirectoryW(winDir, ARRAYSIZE(winDir));
    if (len && len < ARRAYSIZE(winDir)) {
        const std::wstring taskFile = std::wstring(winDir) + L"\\System32\\Tasks\\" + autoTaskName;
        const DWORD attr = GetFileAttributesW(taskFile.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
            return true;
    }
    return false;
}

inline void deleteScheduledTask() {
    DWORD code = 1;
    runAndWait(std::wstring(L"schtasks.exe /Delete /TN \"") + autoTaskName + L"\" /F", code);
}

// 辅助函数：标准化路径并对比
inline bool isPathMatching(const std::wstring& registered, const std::wstring& expected) {
    if (registered.empty() || expected.empty()) return false;
    auto trimQuotes = [](std::wstring s) {
        size_t start = 0;
        while (start < s.size() && (s[start] == L' ' || s[start] == L'"')) ++start;
        size_t end = s.size();
        while (end > start && (s[end - 1] == L' ' || s[end - 1] == L'"')) --end;
        return s.substr(start, end - start);
    };
    std::wstring cleanReg = trimQuotes(registered);
    const size_t argPos = cleanReg.find(L" /");
    if (argPos != std::wstring::npos) {
        cleanReg = trimQuotes(cleanReg.substr(0, argPos));
    }
    std::wstring cleanExp = trimQuotes(expected);
    return _wcsicmp(cleanReg.c_str(), cleanExp.c_str()) == 0;
}

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

inline void deleteRunEntry() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, autoRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    RegDeleteValueW(key, autoRunValue);
    RegCloseKey(key);
}

// 综合自启判断：优先读取配置持久化与任务计划，兼顾注册表兼容
inline bool autoStartEnabled() {
    const int iniSetting = loadAutoStartSetting();
    if (iniSetting == 1) return true;
    if (iniSetting == 0) return false;
    return scheduledTaskExists() || runEntryExists(false);
}

// 启用开机自启：采用最高权限计划任务保障免 UAC 且兼容管理员提权，同时写入配置
inline bool enableAutoStart() {
    const auto executable = executablePath();
    if (executable.empty()) return false;
    saveAutoStartSetting(true);
    const bool taskOk = createLogonTask(executable);
    deleteRunEntry();
    return taskOk;
}

inline void disableAutoStart() {
    saveAutoStartSetting(false);
    deleteScheduledTask();
    deleteRunEntry();
}
