#pragma once

#include <sddl.h>

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

// Appends to %LOCALAPPDATA%\Fuck-sRGB\startup.log. The tray can be started by
// the Task Scheduler, where a MessageBox either blocks invisibly or fails
// outright, so startup diagnostics have to go to the file.
inline void writeStartupLog(const std::wstring& text) {
    const auto ini = settingsFile();
    if (ini.empty()) return;
    const std::filesystem::path path = std::filesystem::path(ini).parent_path() / L"startup.log";
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[80] = {};
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] ",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
               now.wMilliseconds, GetCurrentProcessId());
    const std::wstring line = std::wstring(stamp) + text + L"\n";
    const int size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return;
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                        utf8.data(), size, nullptr, nullptr);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (out) out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
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

// A task definition is used instead of /TR because the command line form either
// stores the path with literal quotes or truncates it at the first space, and
// because it can also lift the default battery and run-time limits.
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
           L"    </Exec>\n"
           L"  </Actions>\n"
           L"</Task>\n";
}

inline bool createLogonTask(const std::wstring& executable) {
    const auto sid = currentUserSid();
    if (sid.empty()) return false;
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
    return created;
}

// Re-registers the task on every call, which also repairs an entry written by an
// older build that stored the executable path with literal quotes. A Run entry
// is only added when no other start-at-logon entry exists, because it cannot
// start a requireAdministrator executable without a UAC prompt.
inline bool enableAutoStart() {
    const auto executable = executablePath();
    if (executable.empty()) return false;
    if (createLogonTask(executable)) {
        deleteRunEntry();
        return true;
    }
    if (scheduledTaskExists() || runEntryExists()) return true;
    return writeRunEntry(executable);
}

inline void disableAutoStart() {
    DWORD code = 1;
    runAndWait(std::wstring(L"schtasks.exe /Delete /TN \"") + autoTaskName + L"\" /F", code);
    deleteRunEntry();
}
