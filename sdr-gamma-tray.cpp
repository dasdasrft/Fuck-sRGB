#include "edge-gamma-core.h"
#include "tray-settings.h"

constexpr wchar_t title[] = L"Fuck sRGB";
constexpr wchar_t windowClass[] = L"Codex.SdrGamma.Tray.1";
constexpr UINT trayId = 1;
constexpr UINT iconMessage = WM_APP + 1;
constexpr UINT doneMessage = WM_APP + 2;
constexpr UINT initMessage = WM_APP + 3;
constexpr UINT revealMessage = WM_APP + 4;
constexpr UINT firstMode = 100;
constexpr UINT exitCommand = 110;
constexpr UINT autoStartCommand = 111;
constexpr UINT iconResource = 101;
constexpr const wchar_t* modes[] = {
    L"Native", L"Gamma2.2", L"Gamma2.4", L"Linear"
};
static_assert(ARRAYSIZE(modes) == selectableCount);

enum class HdrState { Unavailable, Inactive, Active };

HdrState detectHdr() noexcept {
    try {
        return allActiveDisplaysHdr() ? HdrState::Active : HdrState::Inactive;
    } catch (...) {
        return HdrState::Unavailable;
    }
}

std::wstring errorText(Failure failure) {
    wchar_t code[32];
    swprintf_s(code, L"\nError code 0x%08lX", failure.code);
    return std::wstring(failure.operation) + code;
}

std::wstring startupContext() {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    wchar_t station[128] = L"?";
    if (HWINSTA handle = GetProcessWindowStation())
        GetUserObjectInformationW(handle, UOI_NAME, station, sizeof(station), nullptr);
    bool elevated = false;
    if (HANDLE token = nullptr; OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION info{};
        DWORD size = sizeof(info);
        if (GetTokenInformation(token, TokenElevation, &info, size, &size))
            elevated = info.TokenIsElevated != 0;
        CloseHandle(token);
    }
    return L"start session=" + std::to_wstring(session) + L" station=" + station +
           L" elevated=" + (elevated ? L"true" : L"false");
}

class Tray {
public:
    explicit Tray(HINSTANCE instance, bool silentMode = false)
        : icon_(LoadIconW(instance, MAKEINTRESOURCEW(iconResource))), silentMode_(silentMode) {
        if (!icon_) fail(L"Load tray icon");
    }

    int run(HINSTANCE instance) {
        // 在托盘全生命周期内激活调试特权，确保对 dwm.exe 的进程枚举与查询具有完整权限
        DebugPrivilege globalDebug;
        writeStartupLog(L"run: registering window class");
        taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
        if (!taskbarCreated_) fail(L"Register taskbar message");
        writeStartupLog(L"run: creating window");
        WNDCLASSW wc{};
        wc.hInstance = instance;
        wc.lpfnWndProc = procedure;
        wc.lpszClassName = windowClass;
        wc.hIcon = icon_;
        if (!RegisterClassW(&wc)) fail(L"Register tray window class");
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass, title, WS_POPUP,
                                0, 0, 0, 0, nullptr, nullptr, instance, this);
        if (!hwnd_) fail(L"Create tray window");

        writeStartupLog(L"run: filtering messages");
        // Explorer runs at medium integrity while this process runs elevated, so
        // the taskbar messages have to pass UIPI. A tray started by the Task
        // Scheduler rejects the per-window form with ERROR_ACCESS_DENIED, so fall
        // back to the process-wide filter, which only needs elevation, and never
        // treat the failure as fatal: the icon and the menu are still useful when
        // Explorer cannot reach this window.
        if (!ChangeWindowMessageFilterEx(hwnd_, iconMessage, MSGFLT_ALLOW, nullptr) ||
            !ChangeWindowMessageFilterEx(hwnd_, taskbarCreated_, MSGFLT_ALLOW, nullptr)) {
            writeStartupLog(L"filter ex failed code=" + std::to_wstring(GetLastError()));
            if (!ChangeWindowMessageFilter(iconMessage, MSGFLT_ADD) ||
                !ChangeWindowMessageFilter(taskbarCreated_, MSGFLT_ADD))
                writeStartupLog(L"filter global failed code=" + std::to_wstring(GetLastError()));
        }
        hdr_ = detectHdr();
        writeStartupLog(L"run: hdr=" + std::to_wstring(static_cast<int>(hdr_)) +
                   L" dwm=" + std::to_wstring(currentDwm(patcher_.session)) +
                   L" dwmProcs=" + std::to_wstring(processesNamed(L"dwm.exe").size()));
        // The inspection result is reported through the tooltip and the log. A
        // modal warning here would block the message loop when the tray is
        // started from the Run key, so the icon has to be added first.
        const bool inspected = refresh();
        writeStartupLog(L"run: refresh=" + std::to_wstring(inspected ? 1 : 0) + L" error=" + error_);
        writeStartupLog(L"run: adding icon");
        bool iconAdded = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (addIcon()) {
                iconAdded = true;
                break;
            }
            Sleep(500);
        }
        if (!iconAdded) {
            writeStartupLog(L"add icon deferred, will attach upon TaskbarCreated");
        }
        if (!SetTimer(hwnd_, 1, 2000, nullptr)) fail(L"Start display monitoring");
        persisted_ = loadSavedMode();
        restoreLastMode();
        PostMessageW(hwnd_, initMessage, 0, 0);
        writeStartupLog(L"run: message loop");

        MSG msg{};
        int status;
        while ((status = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (status < 0) fail(L"Read window messages");
        return 0;
    }

    ~Tray() {
        if (worker_.joinable()) worker_.join();
        if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
    }

private:
    HWND hwnd_{};
    HICON icon_{};
    bool silentMode_ = false;
    UINT taskbarCreated_{};
    Patcher patcher_;
    EdgeSync edge_{patcher_.session};
    Current current_{};
    Current result_{};
    HdrState hdr_ = HdrState::Unavailable;
    bool busy_ = false;
    bool autoStart_ = false;
    bool restored_ = false;
    bool changeDwm_ = false;
    Mode persisted_ = Mode::Srgb;
    bool switching_ = false;
    bool circuitBroken_ = false;
    std::vector<ULONGLONG> dwmRestarts_;
    EdgeSyncStatus edgeStatus_{};
    EdgeSyncStatus edgeResult_{};
    bool edgeError_ = false, edgeResultError_ = false;
    std::wstring error_, workerError_;
    std::thread worker_;

    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        if (message == WM_NCCREATE) {
            const auto creation = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* tray = static_cast<Tray*>(creation->lpCreateParams);
            tray->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tray));
        }
        auto* tray = reinterpret_cast<Tray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        return tray ? tray->onMessage(message, wp, lp) : DefWindowProcW(hwnd, message, wp, lp);
    }

    NOTIFYICONDATAW data() const {
        NOTIFYICONDATAW info{};
        info.cbSize = sizeof(info);
        info.hWnd = hwnd_;
        info.uID = trayId;
        return info;
    }

    void tooltip(NOTIFYICONDATAW& info) const {
        const wchar_t* status = L"Checking DWM";
        if (circuitBroken_) status = L"Safety breaker tripped: DWM unstable";
        else if (hdr_ == HdrState::Inactive) status = L"HDR inactive";
        else if (hdr_ == HdrState::Unavailable) status = L"HDR status unavailable";
        else if (switching_) status = L"Applying gamma";
        else if (!error_.empty()) status = L"DWM verification failed";
        else if (index(current_.mode) < selectableCount) status = modes[index(current_.mode)];
        else if (current_.mode == Mode::LegacyGamma26) status = L"Legacy gamma active";
        const wchar_t* edge = L"";
        if (hdr_ == HdrState::Active) {
            if (edgeError_ || edgeStatus_.skipped) edge = L" | Edge sync incomplete";
            else if (edgeStatus_.synced) edge = L" | Edge synced";
            else if (edgeStatus_.waiting) edge = L" | Waiting for Edge GPU";
        }
        swprintf_s(info.szTip, L"%ls · %ls%ls", title, status, edge);
    }

    bool addIcon() const {
        auto info = data();
        info.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        if (!silentMode_) {
            info.uFlags |= NIF_INFO;
            info.dwInfoFlags = NIIF_INFO;
            wcscpy_s(info.szInfoTitle, title);
            wcscpy_s(info.szInfo, L"Fuck sRGB is running. Right-click this icon to change the gamma mode.");
        }
        info.uCallbackMessage = iconMessage;
        info.hIcon = icon_;
        tooltip(info);
        if (!Shell_NotifyIconW(NIM_ADD, &info)) return false;
        info.uVersion = NOTIFYICON_VERSION_4;
        return Shell_NotifyIconW(NIM_SETVERSION, &info);
    }

    void updateTooltip() const {
        auto info = data();
        info.uFlags = NIF_TIP | NIF_SHOWTIP;
        tooltip(info);
        Shell_NotifyIconW(NIM_MODIFY, &info);
    }

    void refreshHdr() {
        const auto state = detectHdr();
        if (hdr_ == state) return;
        hdr_ = state;
        updateTooltip();
    }

    bool refresh() {
        try {
            current_ = patcher_.inspect();
            error_.clear();
            if (current_.mode == Mode::Unknown)
                error_ = L"DWM shaders do not match a supported gamma mode.";
        } catch (const Failure& error) {
            current_ = {};
            error_ = errorText(error);
        } catch (...) {
            current_ = {};
            error_ = L"Could not read DWM state.";
        }
        return error_.empty();
    }

    // Reapplies the mode stored by the previous session. A fresh DWM instance
    // always starts on the native curve, so a saved Gamma22/Gamma2.4/Linear
    // selection needs one patch cycle to come back after a logon or restart.
    void restoreLastMode() {
        writeStartupLog(L"restore: hdr=" + std::to_wstring(static_cast<int>(hdr_)) +
                        L" current=" + std::to_wstring(index(current_.mode)) +
                        L" persisted=" + std::to_wstring(index(persisted_)));
        if (hdr_ == HdrState::Active && current_.mode != Mode::Unknown) {
            restored_ = true;
            if (persisted_ != current_.mode) {
                startWork(persisted_, true);
                return;
            }
        }
        startWork(current_.mode, false);
    }

    void toggleAutoStart() {
        if (autoStart_) {
            disableAutoStart();
            autoStart_ = autoStartEnabled();
            if (autoStart_)
                MessageBoxW(nullptr, L"Could not remove the start-at-logon entry.", title,
                            MB_OK | MB_ICONERROR);
        } else {
            if (!enableAutoStart())
                MessageBoxW(nullptr, L"Could not create the start-at-logon entry.", title,
                            MB_OK | MB_ICONERROR);
            autoStart_ = autoStartEnabled();
        }
        updateTooltip();
    }

    void menu(POINT where) {
        refreshHdr();
        const bool active = hdr_ == HdrState::Active;
        if (active && !busy_ && current_.mode == Mode::Unknown) { refresh(); updateTooltip(); }
        HMENU popup = CreatePopupMenu();
        if (!popup) return;
        UINT flags = MF_STRING;
        if (!active || busy_ || current_.mode == Mode::Unknown) flags |= MF_GRAYED;
        for (UINT i = 0; i < selectableCount; ++i) AppendMenuW(popup, flags, firstMode + i, modes[i]);
        if (active && index(current_.mode) < selectableCount) {
            CheckMenuRadioItem(popup, firstMode, firstMode + selectableCount - 1,
                              firstMode + index(current_.mode), MF_BYCOMMAND);
        }
        if (active && (busy_ || !error_.empty() || current_.mode == Mode::LegacyGamma26)) {
            AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(popup, MF_STRING | MF_GRAYED, 0, busy_ ? L"Updating..." :
                        !error_.empty() ? (circuitBroken_ ? L"Safety circuit breaker tripped" : L"DWM verification failed") : L"Select a mode to replace legacy gamma");
        }
        AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(popup, MF_STRING | (autoStart_ ? MF_CHECKED : MF_UNCHECKED),
                    autoStartCommand, L"Start with Windows");
        AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(popup, MF_STRING, exitCommand, L"Exit");
        SetForegroundWindow(hwnd_);
        auto command = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                        where.x, where.y, hwnd_, nullptr);
        DestroyMenu(popup);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        if (command) PostMessageW(hwnd_, WM_COMMAND, command, 0);
    }

    void startWork(Mode mode, bool changeDwm) {
        if (busy_ || (changeDwm && circuitBroken_)) return;
        const auto before = current_;
        changeDwm_ = changeDwm;
        // The two-second monitor calls this with changeDwm=false; logging every
        // poll would fill the log with noise and rotate the useful lines away.
        if (changeDwm)
            writeStartupLog(L"startWork mode=" + std::to_wstring(index(mode)) + L" changeDwm=1");
        busy_ = true;
        switching_ = changeDwm;
        workerError_.clear();
        if (changeDwm) updateTooltip();
        try {
            worker_ = std::thread([this, mode, before, changeDwm] {
                result_ = before;
                edgeResult_ = {};
                edgeResultError_ = false;
                if (changeDwm && detectHdr() == HdrState::Active) {
                    try {
                        result_ = patcher_.set(mode);
                        writeStartupLog(L"patcher set ok mode=" + std::to_wstring(index(result_.mode)));
                    } catch (const Failure& error) {
                        workerError_ = errorText(error);
                        writeStartupLog(L"patcher set failed: " + std::wstring(error.operation) +
                                        L" code=" + std::to_wstring(error.code));
                    } catch (...) {
                        workerError_ = L"Could not apply the gamma mode.";
                        writeStartupLog(L"patcher set failed: unknown");
                    }
                }
                if (workerError_.empty()) {
                    try {
                        edgeResult_ = edge_.sync(detectHdr() == HdrState::Active ? result_.mode : Mode::Srgb);
                    } catch (...) {
                        edgeResultError_ = true;
                        writeStartupLog(L"edge sync threw");
                    }
                }
                PostMessageW(hwnd_, doneMessage, 0, 0);
            });
        } catch (...) {
            busy_ = switching_ = false;
            updateTooltip();
            writeStartupLog(L"startWork: could not start the worker thread");
            if (changeDwm) MessageBoxW(nullptr, L"Could not start the gamma change.", title, MB_OK | MB_ICONERROR);
        }
    }

    void select(Mode mode) {
        refreshHdr();
        if (hdr_ != HdrState::Active || mode == current_.mode || current_.mode == Mode::Unknown) return;
        startWork(mode, true);
    }

    LRESULT onMessage(UINT message, WPARAM wp, LPARAM lp) {
        if (message == taskbarCreated_ && taskbarCreated_) {
            addIcon();
            return 0;
        }
        switch (message) {
        case revealMessage: {
            // A second launch asked this instance to make itself visible.
            POINT where{};
            GetCursorPos(&where);
            menu(where);
            return 0;
        }
        case iconMessage:
            if (LOWORD(lp) == WM_CONTEXTMENU && HIWORD(lp) == trayId) {
                POINT where{GET_X_LPARAM(wp), GET_Y_LPARAM(wp)};
                menu(where);
            }
            return 0;
        case WM_COMMAND: {
            const auto command = LOWORD(wp);
            if (command >= firstMode && command < firstMode + selectableCount)
                select(static_cast<Mode>(command - firstMode));
            else if (command == autoStartCommand)
                toggleAutoStart();
            else if (command == exitCommand)
                DestroyWindow(hwnd_);
            return 0;
        }
        case initMessage:
            autoStart_ = autoStartEnabled();
            writeStartupLog(L"init: autoStart=" + std::to_wstring(autoStart_ ? 1 : 0) +
                            L" exe=" + executablePath());
            if (autoStart_) autoStart_ = enableAutoStart();
            writeStartupLog(L"init: autoStart after sync=" + std::to_wstring(autoStart_ ? 1 : 0));
            return 0;
        case doneMessage:
            if (!worker_.joinable()) return 0;
            worker_.join();
            busy_ = switching_ = false;
            edgeStatus_ = edgeResult_;
            edgeError_ = edgeResultError_;
            if (workerError_.empty()) {
                current_ = result_;
                if (current_.mode != Mode::Unknown) {
                    error_.clear();
                    // 仅当用户主动切换或者执行了实际的 DWM 状态变更(changeDwm_)时，才持久化记录偏好，
                    // 绝不因后台轮询被动同步而篡改抹除用户的持久化偏好
                    if (changeDwm_ && current_.mode != persisted_) {
                        saveMode(current_.mode);
                        persisted_ = current_.mode;
                    }
                }
                if (changeDwm_)
                    writeStartupLog(L"done: mode=" + std::to_wstring(index(current_.mode)) +
                                    L" edge synced=" + std::to_wstring(edgeStatus_.synced) +
                                    L" waiting=" + std::to_wstring(edgeStatus_.waiting) +
                                    L" skipped=" + std::to_wstring(edgeStatus_.skipped));
                updateTooltip();
            } else {
                refresh();
                updateTooltip();
                writeStartupLog(L"done: worker error " + workerError_);
                MessageBoxW(nullptr, workerError_.c_str(), title, MB_OK | MB_ICONERROR);
            }
            return 0;
        case WM_TIMER:
            refreshHdr();
            if (!busy_) {
                if (!restored_ && hdr_ == HdrState::Active) {
                    // HDR arrived after startup, for example when the display
                    // woke up later: apply the stored mode once.
                    restored_ = true;
                    if (current_.mode != Mode::Unknown && persisted_ != current_.mode) {
                        startWork(persisted_, true);
                        return 0;
                    }
                }
                const DWORD dwmPid = currentDwm(patcher_.session);
                // 仅当两次读取的 PID 均为非零有效值且真正发生变化时，才判定为 DWM 进程发生重启
                if (dwmPid != 0 && current_.pid != 0 && dwmPid != current_.pid) {
                    const ULONGLONG now = GetTickCount64();
                    dwmRestarts_.push_back(now);
                    // 清理 30 秒以前的历史重启记录
                    dwmRestarts_.erase(std::remove_if(dwmRestarts_.begin(), dwmRestarts_.end(),
                        [now](ULONGLONG t) { return now - t > 30000; }), dwmRestarts_.end());
                    if (dwmRestarts_.size() >= 3) {
                        circuitBroken_ = true;
                        writeStartupLog(L"CRITICAL: DWM restarted 3 times in 30s! Safety circuit breaker tripped, patching halted.");
                        error_ = L"Safety breaker tripped: DWM was unstable, patching suspended to prevent crash loop.";
                        updateTooltip();
                        return 0;
                    }

                    refresh();
                    updateTooltip();
                    // 核心修复：DWM 重启或崩溃重拉后，其自身恢复为原始 Mode::Srgb。
                    // 此时必须为新 DWM 重新打上用户记住的 gamma 模式，绝不能被动轮询覆盖持久化模式！
                    if (hdr_ == HdrState::Active && current_.mode != Mode::Unknown && persisted_ != current_.mode) {
                        startWork(persisted_, true);
                        return 0;
                    }
                } else if (current_.pid == 0 && dwmPid != 0) {
                    refresh();
                    updateTooltip();
                }
                startWork(current_.mode, false);
            }
            return 0;
        case WM_DISPLAYCHANGE:
            refreshHdr();
            if (!busy_) {
                // 显示状态或 HDR 开启状态改变时立即尝试恢复用户偏好，消除 2 秒轮询延迟
                if (hdr_ == HdrState::Active && current_.mode != Mode::Unknown && persisted_ != current_.mode) {
                    startWork(persisted_, true);
                }
            }
            return 0;
        case WM_QUERYENDSESSION:
            writeStartupLog(L"WM_QUERYENDSESSION");
            return TRUE;
        case WM_ENDSESSION:
            writeStartupLog(L"WM_ENDSESSION end=" + std::to_wstring(wp ? 1 : 0));
            return 0;
        case WM_CLOSE:
            writeStartupLog(L"WM_CLOSE");
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY: {
            writeStartupLog(L"WM_DESTROY");
            KillTimer(hwnd_, 1);
            auto info = data();
            Shell_NotifyIconW(NIM_DELETE, &info);
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd_, message, wp, lp);
        }
    }
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const bool isAutoStart = cmdLine && (wcsstr(cmdLine, L"/autostart") != nullptr || wcsstr(cmdLine, L"-autostart") != nullptr);
    writeStartupLog(startupContext() + (isAutoStart ? L" [autostart]" : L""));
    writeStartupLog(L"settingsFile=" + settingsFile());
    try {
        Handle singleton(CreateMutexW(nullptr, FALSE, L"Local\\Codex.SdrGamma.Tray.1"));
        if (!singleton.value) fail(L"Create tray instance lock");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            // The tray already runs in another process.
            writeStartupLog(L"exit: another instance is running");
            if (isAutoStart) {
                // 开机自启动实例静默退出，避免无故弹出右键菜单
                return 0;
            }
            HWND existing = nullptr;
            for (int attempt = 0; attempt < 10 && !existing; ++attempt) {
                if (attempt) Sleep(150);
                existing = FindWindowW(windowClass, nullptr);
            }
            if (existing) {
                writeStartupLog(L"exit: asked the running instance to show its menu");
                PostMessageW(existing, revealMessage, 0, 0);
            } else {
                writeStartupLog(L"exit: running window not found");
                MessageBoxW(nullptr, L"Fuck sRGB is already running in the notification area.\n"
                                      L"Right-click its icon to change the gamma mode.",
                            title, MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }
        writeStartupLog(L"instance lock acquired");
        Tray tray(instance, isAutoStart);
        writeStartupLog(L"tray constructed");
        const int code = tray.run(instance);
        writeStartupLog(L"run returned " + std::to_wstring(code));
        return code;
    } catch (const Failure& error) {
        writeStartupLog(L"failure: " + std::wstring(error.operation) + L" code=" + std::to_wstring(error.code));
        const int box = MessageBoxW(nullptr, errorText(error).c_str(), title, MB_OK | MB_ICONERROR);
        writeStartupLog(L"failure: message box returned " + std::to_wstring(box));
    } catch (...) {
        writeStartupLog(L"failure: unknown");
        const int box = MessageBoxW(nullptr, L"Could not start the tray application.", title, MB_OK | MB_ICONERROR);
        writeStartupLog(L"failure: message box returned " + std::to_wstring(box));
    }
    writeStartupLog(L"exit 1");
    return 1;
}




