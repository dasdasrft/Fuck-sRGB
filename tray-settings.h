#pragma once

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
// Tasks written by the earlier 1.2.x builds are removed on sight.

constexpr wchar_t settingsSection[] = L"tray";
constexpr wchar_t settingsModeKey[] = L"lastMode";
constexpr wchar_t settingsFolder[] = L"Fuck-sRGB";
constexpr wchar_t settingsFileName[] = L"settings.ini";

constexpr wchar_t autoRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t autoRunValue[] = L"Fuck sRGB";
constexpr wchar_t autoTaskName[] = L"Fuck sRGB";

inline std::wstring executablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}

inline std::wstring settingsFile() {
    wchar_t base[MAX_PATH * 2] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
    std::filesystem::path folder;
    if (length && length < ARRAYSIZE(base)) {
        folder = std::filesystem::path(std::wstring(base, length)) / settingsFolder;
    } else {
        const auto executable = executablePath();
        if (executable.empty()) return {};
        folder = std::filesystem::path(executable).parent_path() / settingsFolder;
    }
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error) return {};
    return (folder / settingsFileName).wstring();
}

// Falls back to the native curve when the stored value is missing or stale.
inline Mode loadSavedMode() {
    const auto file = settingsFile();
    if (file.empty()) return Mode::Srgb;
    const UINT stored = GetPrivateProfileIntW(settingsSection, settingsModeKey,
                                              static_cast<int>(index(Mode::Srgb)), file.c_str());
    return stored < selectableCount ? static_cast<Mode>(stored) : Mode::Srgb;
}

// Appends to %LOCALAPPDATA%\Fuck-sRGB\startup.log with plain Win32 file calls.
// A tray started from the Run key cannot always show a modal error, so the
// reason for a failed start has to reach a file instead.
inline void writeStartupLog(const std::wstring& text) {
    const auto ini = settingsFile();
    if (ini.empty()) return;
    const std::wstring path = (std::filesystem::path(ini).parent_path() / L"startup.log").wstring();
    HANDLE probe = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(probe, &size) && size.QuadPart > 256 * 1024) {
            CloseHandle(probe);
            DeleteFileW(path.c_str());
        } else {
            CloseHandle(probe);
        }
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[80] = {};
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] ",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
               now.wMilliseconds, GetCurrentProcessId());
    const std::wstring line = std::wstring(stamp) + text + L"\r\n";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, line.c_str(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
}

inline void saveMode(Mode mode) {
    if (index(mode) >= selectableCount) return;
    const auto file = settingsFile();
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
    const DWORD wait = WaitForSingleObject(process.hProcess, 20000);
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
    if (!scheduledTaskExists()) return;
    DWORD code = 1;
    runAndWait(std::wstring(L"schtasks.exe /Delete /TN \"") + autoTaskName + L"\" /F", code);
}

inline bool runEntryExists() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, autoRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    wchar_t value[32768] = {};
    DWORD bytes = static_cast<DWORD>(sizeof(value));
    DWORD type = 0;
    const LONG status = RegQueryValueExW(key, autoRunValue, nullptr, &type,
                                         reinterpret_cast<LPBYTE>(value), &bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && bytes > sizeof(wchar_t);
}

inline bool writeRunEntry(const std::wstring& path) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, autoRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return false;
    const LONG status = RegSetValueExW(key, autoRunValue, 0, REG_SZ,
                                       reinterpret_cast<const BYTE*>(path.c_str()),
                                       static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

inline void deleteRunEntry() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, autoRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    RegDeleteValueW(key, autoRunValue);
    RegCloseKey(key);
}

inline bool autoStartEnabled() {
    // A leftover task also counts as enabled so the upgrade path can remove it.
    return runEntryExists() || scheduledTaskExists();
}

// Rewrites the Run value on every call, which repairs a stale path after the
// executable moves, and drops the unusable logon task from older builds.
inline bool enableAutoStart() {
    const auto executable = executablePath();
    if (executable.empty()) return false;
    deleteScheduledTask();
    return writeRunEntry(L"\"" + executable + L"\"");
}

inline void disableAutoStart() {
    deleteRunEntry();
    deleteScheduledTask();
}
