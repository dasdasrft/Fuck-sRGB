#pragma once

// Per-user persistence for the tray.
//
// The last selected gamma mode lives in %LOCALAPPDATA%\Fuck-sRGB\settings.ini.
// Keeping it there instead of next to the executable means the preference
// survives an executable in Program Files, an elevated working directory and
// DWM restarts, and that a later run can reapply the selection at logon.
//
// Start-at-logon is owned by the operating system (a per-user logon task, or an
// HKCU Run value when the task cannot be created) rather than by the INI file,
// so the menu always reflects what the system will actually do at logon.

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
    return scheduledTaskExists() || runEntryExists();
}

// A logon task with RL HIGHEST starts the tray without a UAC prompt, which a
// Run entry cannot do because the executable is manifested requireAdministrator.
// The Run entry remains as a fallback for systems where task creation is blocked.
inline bool enableAutoStart() {
    const auto executable = executablePath();
    if (executable.empty()) return false;
    DWORD code = 1;
    const std::wstring create = std::wstring(L"schtasks.exe /Create /TN \"") + autoTaskName +
                                L"\" /TR \"\\\"" + executable + L"\\\"\" /SC ONLOGON /RL HIGHEST /F";
    if (runAndWait(create, code) && code == 0) {
        deleteRunEntry();
        return true;
    }
    return writeRunEntry(executable);
}

inline void disableAutoStart() {
    DWORD code = 1;
    runAndWait(std::wstring(L"schtasks.exe /Delete /TN \"") + autoTaskName + L"\" /F", code);
    deleteRunEntry();
}
