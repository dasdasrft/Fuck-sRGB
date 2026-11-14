#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <objbase.h>


#pragma warning(push)
#pragma warning(disable: 4458)
#include <gdiplus.h>
#pragma warning(pop)

#include <cmath>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

namespace ModernUI {

using namespace Gdiplus;

// ============================================================================
// KIN Design System (v3.0.4) Design Tokens & Color Space
// Principles: quiet-intelligence, content-priority, structure-felt-not-seen, dense-but-legible
// ============================================================================
namespace Kin {
    // Canvas & Layered Surfaces
    static const Color Canvas(255, 8, 9, 10);          // #08090a (deepest canvas)
    static const Color Sidebar(255, 11, 12, 13);       // #0b0c0d (custom header / titlebar)
    static const Color Surface1(255, 15, 16, 17);      // #0f1011 (footer / baseline)
    static const Color Surface2(255, 20, 21, 22);      // #141516 (cards / elevated)
    static const Color Surface3(255, 25, 26, 28);      // #191a1c (card hover / wells)
    static const Color Surface4(255, 32, 33, 36);      // #202124 (toggle wells / badges)

    // Interactive States
    static const Color SurfaceHover(16, 255, 255, 255);    // rgba(255, 255, 255, 0.045)
    static const Color SurfaceSelected(32, 94, 106, 210);  // rgba(94, 106, 210, 0.12)
    static const Color FocusRing(184, 130, 143, 255);      // rgba(130, 143, 255, 0.72)

    // Hairline Borders & Dividers (structure-felt-not-seen)
    static const Color LineSubtle(16, 255, 255, 255);      // rgba(255, 255, 255, 0.055)
    static const Color LineDefault(24, 255, 255, 255);     // rgba(255, 255, 255, 0.09)
    static const Color LineStrong(40, 255, 255, 255);      // rgba(255, 255, 255, 0.15)

    // Text Hierarchy
    static const Color TextPrimary(255, 244, 245, 247);    // #f4f5f7
    static const Color TextSecondary(255, 201, 203, 209);  // #c9cbd1
    static const Color TextMuted(255, 139, 143, 152);      // #8b8f98
    static const Color TextDisabled(255, 98, 102, 109);    // #62666d

    // Brand & Semantic Accents
    static const Color Accent(255, 94, 106, 210);          // #5e6ad2 (KIN signature Purple-Indigo)
    static const Color AccentHover(255, 98, 109, 204);     // #626dcc
    static const Color AccentSoft(36, 94, 106, 210);       // rgba(94, 106, 210, 0.14)
    static const Color Positive(255, 80, 173, 125);        // #50ad7d (HDR Active / Normal)
    static const Color PositiveSoft(28, 80, 173, 125);     // rgba(80, 173, 125, 0.10)
    static const Color Warning(255, 198, 154, 69);         // #c69a45 (Cooldown protection)
    static const Color WarningSoft(28, 198, 154, 69);      // rgba(198, 154, 69, 0.10)
    static const Color Critical(255, 216, 88, 102);        // #d85866 (Danger / Close button hover)
    static const Color CriticalSoft(28, 216, 88, 102);     // rgba(216, 88, 102, 0.10)
}

struct DashboardCallbacks {
    std::function<Mode()> getMode;
    std::function<HdrState()> getHdrState;
    std::function<bool()> getAutoStart;
    std::function<DWORD()> getDwmPid;
    std::function<ULONGLONG()> getCooldownRemainingMs;
    std::function<void(Mode)> onSelectMode;
    std::function<void()> onToggleAutoStart;
    std::function<void()> onExit;
};

class DashboardWindow {
public:
    static constexpr wchar_t className[] = L"FuckSRGB.KinDashboard.1";
    static constexpr int defaultWidth = 780;
    static constexpr int defaultHeight = 580;
    static constexpr int minWidth = 680;
    static constexpr int minHeight = 520;

    explicit DashboardWindow(HINSTANCE instance, DashboardCallbacks callbacks)
        : instance_(instance), callbacks_(std::move(callbacks)) {
        registerClass();
    }

    ~DashboardWindow() {
        if (hwnd_ && IsWindow(hwnd_)) {
            DestroyWindow(hwnd_);
        }
    }

    void show(HWND = nullptr) {
        if (!hwnd_ || !IsWindow(hwnd_)) {
            create();
        }
        if (hwnd_) {
            ShowWindow(hwnd_, SW_SHOWNORMAL);
            SetForegroundWindow(hwnd_);
        }
    }

    void hide() {
        if (hwnd_ && IsWindow(hwnd_)) {
            ShowWindow(hwnd_, SW_HIDE);
        }
    }

    bool isVisible() const {
        return hwnd_ && IsWindow(hwnd_) && IsWindowVisible(hwnd_);
    }

    void refresh() {
        if (isVisible()) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    HWND hwnd() const { return hwnd_; }

private:
    HINSTANCE instance_{};
    HWND hwnd_{};
    DashboardCallbacks callbacks_;

    // 交互悬停状态
    bool hoverTitleMin_ = false;
    bool hoverTitleMax_ = false;
    bool hoverTitleClose_ = false;
    int hoverCardIndex_ = -1;
    bool hoverToggle_ = false;
    bool hoverMinBtn_ = false;
    bool hoverExitBtn_ = false;

    // 响应式布局矩形区域
    RectF titleMinRect_{};
    RectF titleMaxRect_{};
    RectF titleCloseRect_{};
    RectF graphCardRect_{};
    RectF telemetryCardRect_{};
    RectF cardRects_[4]{};
    RectF toggleRect_{};
    RectF minBtnRect_{};
    RectF exitBtnRect_{};

    void registerClass() {
        WNDCLASSW wc{};
        wc.hInstance = instance_;
        wc.lpfnWndProc = wndProc;
        wc.lpszClassName = className;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
        wc.hbrBackground = nullptr; // 双缓冲无闪烁自绘
        RegisterClassW(&wc);
    }

    void create() {
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        const int posX = (screenW - defaultWidth) / 2;
        const int posY = (screenH - defaultHeight) / 2;

        // 使用支持无边框拉伸、Aero Snap 与阴影的窗口样式组合
        hwnd_ = CreateWindowExW(
            WS_EX_APPWINDOW,
            className,
            L"Fuck sRGB · 控制中心",
            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CAPTION,
            posX, posY, defaultWidth, defaultHeight,
            nullptr, nullptr, instance_, this
        );

        if (!hwnd_) return;

        // 启用 Windows 11 原生深色模式与圆角支持
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
        DWORD corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        // 扩展 DWM 边框到客户区，保留系统级硬件阴影与 Aero 贴边贴角能力
        MARGINS margins = { 1, 1, 1, 1 };
        DwmExtendFrameIntoClientArea(hwnd_, &margins);

        // 触发非客户区重算，彻底移除原生老旧标题栏
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        // 启动 300ms 定时器用于倒计时刷新与状态检测
        SetTimer(hwnd_, 101, 300, nullptr);
    }

    void updateLayout(float w, float h) {
        // 自定义 KIN 标题栏高度 40px
        const float btnW = 44.0f;
        const float btnH = 40.0f;
        titleCloseRect_ = RectF(w - btnW, 0.0f, btnW, btnH);
        titleMaxRect_   = RectF(w - btnW * 2.0f, 0.0f, btnW, btnH);
        titleMinRect_   = RectF(w - btnW * 3.0f, 0.0f, btnW, btnH);

        const float padX = 22.0f;
        const float titleBarH = 40.0f;
        const float footerH = 56.0f;
        const float padTop = 14.0f;

        const float startY = titleBarH + padTop;
        const float availW = (std::max)(100.0f, w - padX * 2.0f);
        const float availH = (std::max)(100.0f, h - startY - footerH - 12.0f);

        // 上半部：EOTF 曲线图 (52%) 与 管线监控 (48%) 响应式自适应
        const float topRowH = (std::max)(185.0f, (std::min)(235.0f, availH * 0.55f));
        const float gapX = 14.0f;
        const float leftW = (availW - gapX) * 0.52f;
        const float rightW = (availW - gapX) * 0.48f;

        graphCardRect_ = RectF(padX, startY, leftW, topRowH);
        telemetryCardRect_ = RectF(padX + leftW + gapX, startY, rightW, topRowH);

        // 中间部：4 模式卡片水平排布
        const float middleY = startY + topRowH + 12.0f;
        const float cardH = (std::max)(125.0f, (std::min)(160.0f, h - middleY - footerH - 12.0f));

        const float cardGap = 10.0f;
        const float cardW = (availW - cardGap * 3.0f) / 4.0f;

        for (int i = 0; i < 4; ++i) {
            cardRects_[i] = RectF(padX + i * (cardW + cardGap), middleY, cardW, cardH);
        }

        // 底部：KIN 开关与操作按钮
        const float footerY = h - footerH;
        toggleRect_ = RectF(padX, footerY + 17.0f, 38.0f, 20.0f);
        exitBtnRect_ = RectF(w - padX - 80.0f, footerY + 12.0f, 80.0f, 32.0f);
        minBtnRect_ = RectF(w - padX - 80.0f - 10.0f - 115.0f, footerY + 12.0f, 115.0f, 32.0f);
    }

    bool isOverTitleBarButtons(float x, float y) const {
        return titleMinRect_.Contains(x, y) ||
               titleMaxRect_.Contains(x, y) ||
               titleCloseRect_.Contains(x, y);
    }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        if (message == WM_NCCREATE) {
            auto* creation = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* window = static_cast<DashboardWindow*>(creation->lpCreateParams);
            window->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            return DefWindowProcW(hwnd, message, wp, lp);
        }

        auto* window = reinterpret_cast<DashboardWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!window) return DefWindowProcW(hwnd, message, wp, lp);

        switch (message) {
        case WM_NCCALCSIZE: {
            // 返回 0 完全消除 Windows 原生粗丑标题栏与非客户区，呈现 100% 现代客户区自绘
            if (wp) {
                if (IsZoomed(hwnd)) {
                    // 最大化时自动适配当前屏幕工作区，避免边缘溢出
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO mi{ sizeof(mi) };
                    if (GetMonitorInfoW(hMon, &mi)) {
                        params->rgrc[0] = mi.rcWork;
                    }
                }
                return 0;
            }
            return DefWindowProcW(hwnd, message, wp, lp);
        }

        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT rc;
            GetWindowRect(hwnd, &rc);

            const int border = 6;
            const bool canResize = !IsZoomed(hwnd);

            // 边缘拉伸命中测试（提供 Windows 原生平滑的上下左右及四角无缝自由拉伸调整大小）
            if (canResize) {
                bool left   = pt.x >= rc.left && pt.x < rc.left + border;
                bool right  = pt.x < rc.right && pt.x >= rc.right - border;
                bool top    = pt.y >= rc.top && pt.y < rc.top + border;
                bool bottom = pt.y < rc.bottom && pt.y >= rc.bottom - border;

                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }

            POINT clientPt = pt;
            ScreenToClient(hwnd, &clientPt);

            // 顶部 40px 自定义标题栏区域
            if (clientPt.y >= 0 && clientPt.y < 40) {
                if (window->isOverTitleBarButtons(static_cast<float>(clientPt.x), static_cast<float>(clientPt.y))) {
                    return HTCLIENT; // 允许按钮捕获悬停与点击
                }
                // 返回 HTCAPTION：让系统原生支持无缝拖动窗口、双击最大化/还原以及贴边 Aero Snap！
                return HTCAPTION;
            }

            return HTCLIENT;
        }

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize.x = minWidth;
            info->ptMinTrackSize.y = minHeight;
            return 0;
        }

        case WM_SIZE: {
            const int w = LOWORD(lp);
            const int h = HIWORD(lp);
            window->updateLayout(static_cast<float>(w), static_cast<float>(h));
            window->refresh();
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            window->render(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return TRUE; // 拦截背景擦除，全双缓冲彻底杜绝任何闪烁

        case WM_TIMER:
            if (wp == 101) {
                window->refresh();
            }
            return 0;

        case WM_MOUSEMOVE: {
            const float x = static_cast<float>(GET_X_LPARAM(lp));
            const float y = static_cast<float>(GET_Y_LPARAM(lp));
            window->handleMouseMove(x, y);
            return 0;
        }

        case WM_MOUSELEAVE: {
            window->handleMouseLeave();
            return 0;
        }

        case WM_LBUTTONUP: {
            const float x = static_cast<float>(GET_X_LPARAM(lp));
            const float y = static_cast<float>(GET_Y_LPARAM(lp));
            window->handleMouseClick(x, y);
            return 0;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lp) == HTCLIENT) {
                if (window->hoverTitleMin_ || window->hoverTitleMax_ || window->hoverTitleClose_ ||
                    window->hoverCardIndex_ >= 0 || window->hoverToggle_ ||
                    window->hoverMinBtn_ || window->hoverExitBtn_) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            return DefWindowProcW(hwnd, message, wp, lp);
        }

        case WM_CLOSE:
            // 点右上角 X 彻底优雅关闭窗口（隐藏至系统托盘，后台常驻接管 HDR）
            window->hide();
            return 0;

        default:
            return DefWindowProcW(hwnd, message, wp, lp);
        }
    }

    void handleMouseMove(float x, float y) {
        bool oldTMin = hoverTitleMin_;
        bool oldTMax = hoverTitleMax_;
        bool oldTClose = hoverTitleClose_;
        int oldCard = hoverCardIndex_;
        bool oldToggle = hoverToggle_;
        bool oldMin = hoverMinBtn_;
        bool oldExit = hoverExitBtn_;

        hoverTitleMin_ = titleMinRect_.Contains(x, y);
        hoverTitleMax_ = titleMaxRect_.Contains(x, y);
        hoverTitleClose_ = titleCloseRect_.Contains(x, y);

        hoverCardIndex_ = -1;
        for (int i = 0; i < 4; ++i) {
            if (cardRects_[i].Contains(x, y)) {
                hoverCardIndex_ = i;
                break;
            }
        }

        hoverToggle_ = toggleRect_.Contains(x, y);
        hoverMinBtn_ = minBtnRect_.Contains(x, y);
        hoverExitBtn_ = exitBtnRect_.Contains(x, y);

        if (oldTMin != hoverTitleMin_ || oldTMax != hoverTitleMax_ || oldTClose != hoverTitleClose_ ||
            oldCard != hoverCardIndex_ || oldToggle != hoverToggle_ ||
            oldMin != hoverMinBtn_ || oldExit != hoverExitBtn_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd_, 0 };
        TrackMouseEvent(&tme);
    }

    void handleMouseLeave() {
        hoverTitleMin_ = false;
        hoverTitleMax_ = false;
        hoverTitleClose_ = false;
        hoverCardIndex_ = -1;
        hoverToggle_ = false;
        hoverMinBtn_ = false;
        hoverExitBtn_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void handleMouseClick(float x, float y) {
        // 自定义标题栏按钮点击响应
        if (titleMinRect_.Contains(x, y)) {
            ShowWindow(hwnd_, SW_MINIMIZE);
            return;
        }
        if (titleMaxRect_.Contains(x, y)) {
            ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
            return;
        }
        if (titleCloseRect_.Contains(x, y)) {
            hide(); // 关闭并隐藏到托盘
            return;
        }

        const ULONGLONG cooldown = callbacks_.getCooldownRemainingMs ? callbacks_.getCooldownRemainingMs() : 0;

        // 点击 4 张模式卡片
        for (int i = 0; i < 4; ++i) {
            if (cardRects_[i].Contains(x, y)) {
                if (cooldown == 0 && callbacks_.onSelectMode) {
                    callbacks_.onSelectMode(static_cast<Mode>(i));
                    refresh();
                }
                return;
            }
        }

        // 点击开机自启动开关
        if (toggleRect_.Contains(x, y)) {
            if (callbacks_.onToggleAutoStart) {
                callbacks_.onToggleAutoStart();
                refresh();
            }
            return;
        }

        // 底部最小化到托盘按钮
        if (minBtnRect_.Contains(x, y)) {
            hide();
            return;
        }

        // 底部退出程序按钮
        if (exitBtnRect_.Contains(x, y)) {
            if (callbacks_.onExit) {
                callbacks_.onExit();
            }
            return;
        }
    }

    // 绘制符合 KIN 规范的细腻圆角矩形
    void drawRoundedRect(Graphics& g, const Pen* pen, const Brush* brush, const RectF& rect, float radius) {
        if (rect.Width <= 0.0f || rect.Height <= 0.0f) return;
        float r = (std::min)(radius, (std::min)(rect.Width, rect.Height) * 0.5f);
        if (r <= 0.0f) {

            if (brush) g.FillRectangle(brush, rect);
            if (pen) g.DrawRectangle(pen, rect);
            return;
        }

        GraphicsPath path;
        const float d = r * 2.0f;
        path.AddArc(rect.X, rect.Y, d, d, 180, 90);
        path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
        path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90);
        path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
        path.CloseFigure();

        if (brush) g.FillPath(brush, &path);
        if (pen) g.DrawPath(pen, &path);
    }

    // 主渲染流程：双缓冲 GDI+ 高精度抗锯齿绘制
    void render(HDC hdc) {
        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const int w = clientRect.right - clientRect.left;
        const int h = clientRect.bottom - clientRect.top;
        if (w <= 0 || h <= 0) return;

        updateLayout(static_cast<float>(w), static_cast<float>(h));

        Bitmap buffer(w, h, PixelFormat32bppARGB);
        Graphics g(&buffer);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        // 1. 底层画布：KIN dark-canvas (#08090a)
        SolidBrush bgBrush(Kin::Canvas);
        g.FillRectangle(&bgBrush, 0, 0, w, h);

        // KIN 字体族定义
        Font fontDisplay(L"Segoe UI", 12.0f, FontStyleBold, UnitPoint);
        Font fontHeading(L"Segoe UI", 10.5f, FontStyleBold, UnitPoint);
        Font fontBody(L"Segoe UI", 9.0f, FontStyleRegular, UnitPoint);
        Font fontSmall(L"Segoe UI", 8.0f, FontStyleRegular, UnitPoint);
        Font fontMicro(L"Segoe UI", 7.5f, FontStyleRegular, UnitPoint);
        Font fontMono(L"Consolas", 8.0f, FontStyleRegular, UnitPoint);

        SolidBrush textPrimary(Kin::TextPrimary);
        SolidBrush textSecondary(Kin::TextSecondary);
        SolidBrush textMuted(Kin::TextMuted);
        SolidBrush textDisabled(Kin::TextDisabled);
        SolidBrush accentBrush(Kin::Accent);
        SolidBrush positiveBrush(Kin::Positive);
        SolidBrush warningBrush(Kin::Warning);

        const Mode currentMode = callbacks_.getMode ? callbacks_.getMode() : Mode::Srgb;
        const HdrState hdr = callbacks_.getHdrState ? callbacks_.getHdrState() : HdrState::Unavailable;
        const bool autoStart = callbacks_.getAutoStart ? callbacks_.getAutoStart() : false;
        const DWORD dwmPid = callbacks_.getDwmPid ? callbacks_.getDwmPid() : 0;
        const ULONGLONG cooldownMs = callbacks_.getCooldownRemainingMs ? callbacks_.getCooldownRemainingMs() : 0;
        const bool hdrActive = (hdr == HdrState::Active);

        // 2. 自定义 KIN 无边框标题栏 (Y: 0 ~ 40px, #0b0c0d)
        SolidBrush titleBg(Kin::Sidebar);
        g.FillRectangle(&titleBg, 0, 0, w, 40);

        // 标题栏底部极细发丝分割线 (#ffffff0e)
        Pen titleLinePen(Kin::LineSubtle, 1.0f);
        g.DrawLine(&titleLinePen, 0.0f, 40.0f, static_cast<float>(w), 40.0f);

        // 左侧 KIN 品牌徽标 (Aperture / Gamma 几何符号)
        RectF brandRect(20.0f, 10.0f, 20.0f, 20.0f);
        SolidBrush brandBg(Kin::Surface3);
        Pen brandBorder(Kin::Accent, 1.0f);
        drawRoundedRect(g, &brandBorder, &brandBg, brandRect, 4.0f);

        GraphicsPath brandPath;
        brandPath.AddBezier(brandRect.X + 4.0f, brandRect.Y + 15.0f,
                            brandRect.X + 8.0f, brandRect.Y + 13.5f,
                            brandRect.X + 11.0f, brandRect.Y + 8.5f,
                            brandRect.X + 16.0f, brandRect.Y + 5.0f);
        Pen brandPen(Kin::Accent, 1.8f);
        g.DrawPath(&brandPen, &brandPath);

        // 软件名称与版本药丸标签
        g.DrawString(L"Fuck sRGB", -1, &fontDisplay, PointF(47.0f, 10.0f), &textPrimary);

        RectF verBadge(130.0f, 12.0f, 42.0f, 17.0f);
        SolidBrush verBg(Kin::Surface3);
        Pen verPen(Kin::LineDefault, 1.0f);
        drawRoundedRect(g, &verPen, &verBg, verBadge, 3.0f);
        g.DrawString(L"v1.3.2", -1, &fontMicro, PointF(verBadge.X + 5.0f, verBadge.Y + 2.0f), &textMuted);

        g.DrawString(L"·  HDR 色彩与伽马校正引擎", -1, &fontSmall, PointF(180.0f, 12.5f), &textMuted);

        // 标题栏右侧自定义窗口控制按钮（最小化、最大化/还原、关闭）
        auto drawTitleBtn = [&](const RectF& rect, bool hovered, int type) {
            if (hovered) {
                if (type == 3) {
                    SolidBrush closeBg(Kin::Critical);
                    g.FillRectangle(&closeBg, rect);
                } else {
                    SolidBrush btnHover(Kin::SurfaceHover);
                    g.FillRectangle(&btnHover, rect);
                }
            }
            const float cx = rect.X + rect.Width * 0.5f;
            const float cy = rect.Y + rect.Height * 0.5f;
            Pen iconPen((type == 3 && hovered) ? Color(255, 255, 255, 255) : Kin::TextSecondary, 1.2f);

            if (type == 1) { // 最小化
                g.DrawLine(&iconPen, cx - 5.0f, cy, cx + 5.0f, cy);
            } else if (type == 2) { // 最大化 / 还原
                if (IsZoomed(hwnd_)) {
                    g.DrawRectangle(&iconPen, cx - 4.0f, cy - 2.0f, 7.0f, 7.0f);
                    g.DrawRectangle(&iconPen, cx - 2.0f, cy - 4.0f, 7.0f, 7.0f);
                } else {
                    g.DrawRectangle(&iconPen, cx - 5.0f, cy - 5.0f, 10.0f, 10.0f);
                }
            } else if (type == 3) { // 关闭
                g.DrawLine(&iconPen, cx - 4.5f, cy - 4.5f, cx + 4.5f, cy + 4.5f);
                g.DrawLine(&iconPen, cx + 4.5f, cy - 4.5f, cx - 4.5f, cy + 4.5f);
            }
        };

        drawTitleBtn(titleMinRect_, hoverTitleMin_, 1);
        drawTitleBtn(titleMaxRect_, hoverTitleMax_, 2);
        drawTitleBtn(titleCloseRect_, hoverTitleClose_, 3);

        // 3. 上半部左侧：EOTF 实时伽马响应曲线图表
        SolidBrush cardBg(Kin::Surface2);
        Pen cardPen(Kin::LineDefault, 1.0f);
        drawRoundedRect(g, &cardPen, &cardBg, graphCardRect_, 7.0f);

        g.DrawString(L"伽马响应曲线 (EOTF 对比)", -1, &fontHeading,
                     PointF(graphCardRect_.X + 16.0f, graphCardRect_.Y + 12.0f), &textPrimary);
        g.DrawString(L"实时渲染", -1, &fontMicro,
                     PointF(graphCardRect_.X + graphCardRect_.Width - 60.0f, graphCardRect_.Y + 14.0f), &textMuted);

        // 坐标系参数
        const float gx = graphCardRect_.X + 34.0f;
        const float gy = graphCardRect_.Y + graphCardRect_.Height - 34.0f;
        const float gw = graphCardRect_.Width - 50.0f;
        const float gh = graphCardRect_.Height - 74.0f;

        Pen gridPen(Color(12, 255, 255, 255), 1.0f);
        Pen axisPen(Color(32, 255, 255, 255), 1.0f);
        g.DrawLine(&gridPen, gx, gy - gh * 0.5f, gx + gw, gy - gh * 0.5f);
        g.DrawLine(&gridPen, gx + gw * 0.5f, gy, gx + gw * 0.5f, gy - gh);
        g.DrawLine(&axisPen, gx, gy, gx + gw, gy);
        g.DrawLine(&axisPen, gx, gy, gx, gy - gh);

        g.DrawString(L"0", -1, &fontMicro, PointF(gx - 12.0f, gy - 6.0f), &textMuted);
        g.DrawString(L"1.0", -1, &fontMicro, PointF(gx + gw - 8.0f, gy + 3.0f), &textMuted);
        g.DrawString(L"1.0", -1, &fontMicro, PointF(gx - 18.0f, gy - gh - 6.0f), &textMuted);

        // 绘制 4 条曲线（原生、Gamma2.2、Gamma2.4、线性）
        auto plotCurve = [&](int modeIdx, Color color, float width) {
            constexpr int steps = 50;
            std::vector<PointF> pts;
            pts.reserve(steps + 1);
            for (int s = 0; s <= steps; ++s) {
                const float t = static_cast<float>(s) / static_cast<float>(steps);
                float val = 0.0f;
                if (modeIdx == 0) { // sRGB
                    val = (t <= 0.04045f) ? (t / 12.92f) : std::pow((t + 0.055f) / 1.055f, 2.4f);
                } else if (modeIdx == 1) { // 2.2
                    val = std::pow(t, 2.2f);
                } else if (modeIdx == 2) { // 2.4
                    val = std::pow(t, 2.4f);
                } else if (modeIdx == 3) { // Linear
                    val = t;
                }
                pts.push_back({ gx + t * gw, gy - val * gh });
            }
            Pen pen(color, width);
            g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
        };

        // 先画未激活的参考曲线 (以 KIN 柔和低透明度灰度线渲染)
        for (int m = 0; m < 4; ++m) {
            if (m != static_cast<int>(currentMode)) {
                plotCurve(m, Color(70, 139, 143, 152), 1.0f);
            }
        }
        // 高亮激活模式曲线 (以 KIN 专属 Purple-Indigo 粗线醒目绘制)
        plotCurve(static_cast<int>(currentMode), Kin::Accent, 2.6f);

        // 底部图例
        const wchar_t* legends[] = { L"● 原生", L"● 2.2", L"● 2.4", L"● 线性" };
        const float legW = gw / 4.0f;
        for (int i = 0; i < 4; ++i) {
            const bool isCur = (static_cast<int>(currentMode) == i);
            SolidBrush lBrush(isCur ? Kin::Accent : Kin::TextMuted);
            g.DrawString(legends[i], -1, &fontSmall, PointF(gx + i * legW + 4.0f, gy + 12.0f), &lBrush);
        }

        // 3. 上半部右侧：系统管线与色彩监控面板
        drawRoundedRect(g, &cardPen, &cardBg, telemetryCardRect_, 7.0f);
        g.DrawString(L"系统管线与色彩监控", -1, &fontHeading,
                     PointF(telemetryCardRect_.X + 16.0f, telemetryCardRect_.Y + 12.0f), &textPrimary);

        // 右上角 HDR 状态指示药丸标签
        RectF badgeRect(telemetryCardRect_.X + telemetryCardRect_.Width - 142.0f,
                       telemetryCardRect_.Y + 11.0f, 126.0f, 22.0f);
        SolidBrush badgeBg(hdrActive ? Kin::PositiveSoft : Kin::Surface3);
        Pen badgePen(hdrActive ? Kin::Positive : Kin::LineDefault, 1.0f);
        drawRoundedRect(g, &badgePen, &badgeBg, badgeRect, 4.0f);

        SolidBrush dotBrush(hdrActive ? Kin::Positive : Kin::TextMuted);
        g.FillEllipse(&dotBrush, badgeRect.X + 8.0f, badgeRect.Y + 7.0f, 8.0f, 8.0f);
        const wchar_t* badgeText = hdrActive ? L"HDR 已生效 (BT.2020)" : L"HDR 未开启 (SDR)";
        g.DrawString(badgeText, -1, &fontMicro, PointF(badgeRect.X + 20.0f, badgeRect.Y + 4.0f), &textPrimary);

        // 获取显示器分辨率与刷新率
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        std::wstring resStr = L"3840 × 2160 (主显示器)";
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
            resStr = std::to_wstring(dm.dmPelsWidth) + L" × " + std::to_wstring(dm.dmPelsHeight) +
                     L" @ " + std::to_wstring(dm.dmDisplayFrequency) + L"Hz";
        }

        auto drawRow = [&](float yOff, const wchar_t* label, const wchar_t* val, Color valCol) {
            g.DrawString(label, -1, &fontSmall, PointF(telemetryCardRect_.X + 16.0f, telemetryCardRect_.Y + yOff), &textSecondary);
            SolidBrush vb(valCol);
            g.DrawString(val, -1, &fontSmall, PointF(telemetryCardRect_.X + 104.0f, telemetryCardRect_.Y + yOff), &vb);
        };

        const float rowGap = 23.0f;
        float curRowY = 44.0f;
        drawRow(curRowY, L"当前显示器:", resStr.c_str(), Kin::TextPrimary);
        curRowY += rowGap;
        drawRow(curRowY, L"HDR 状态:", hdrActive ? L"已启用 (BT.2020 广色域 / 10-bit)" : L"未开启 (标准动态范围)",
                hdrActive ? Kin::Positive : Kin::TextSecondary);
        curRowY += rowGap;
        std::wstring dwmText = dwmPid ? (L"PID " + std::to_wstring(dwmPid) + L" · 着色器已平稳接管") : L"未就绪";
        drawRow(curRowY, L"DWM 进程:", dwmText.c_str(), Kin::Positive);
        curRowY += rowGap;
        drawRow(curRowY, L"开机自启动:", autoStart ? L"免 UAC 计划任务 (最高特权)" : L"未开启",
                autoStart ? Kin::Accent : Kin::TextSecondary);
        curRowY += rowGap;
        if (cooldownMs > 0) {
            std::wstring cdStr = L"显卡防护中 (" + std::to_wstring((cooldownMs + 999) / 1000) + L"s) · 防掉驱动";
            drawRow(curRowY, L"显卡防护:", cdStr.c_str(), Kin::Warning);
        } else {
            drawRow(curRowY, L"显卡防护:", L"稳定就绪 (3000ms 刷新保护守卫)", Kin::Positive);
        }

        // 底部微型注释
        g.DrawString(L"注: Windows HDR 混入原生 sRGB 导致暗部泛白，Gamma 2.2/2.4 可恢复纯正黑位。",
                     -1, &fontMicro,
                     PointF(telemetryCardRect_.X + 16.0f, telemetryCardRect_.Y + telemetryCardRect_.Height - 22.0f),
                     &textMuted);

        // 4. 中间部：4 模式卡片 (原生 sRGB, Gamma 2.2, Gamma 2.4, 线性 1.0)
        struct CardInfo {
            const wchar_t* title;
            const wchar_t* badge;
            const wchar_t* desc1;
            const wchar_t* desc2;
            const wchar_t* note;
        };

        const CardInfo cards[4] = {
            { L"原生 sRGB", L"默认", L"Windows 默认", L"未校准 sRGB 曲线", L"暗部明显发灰泛白" },
            { L"Gamma 2.2", L"黄金标准 ★", L"绝大多数游戏", L"与标准影视通用", L"通透饱满 细节均衡" },
            { L"Gamma 2.4", L"影院母带", L"BT.1886 电影", L"暗室母带制作标准", L"黑场深邃 沉浸浓郁" },
            { L"线性 1.0", L"专业直出", L"1:1 无曲线映射", L"直出 SDR 原始亮度", L"适合色彩测试调色" }
        };

        for (int i = 0; i < 4; ++i) {
            const RectF& rc = cardRects_[i];
            const bool isSelected = (static_cast<int>(currentMode) == i);
            const bool isHovered = (hoverCardIndex_ == i);

            // 卡片背景与边框按照 KIN 规范层级处理
            SolidBrush cBg(isSelected ? Kin::Surface2 : (isHovered ? Kin::Surface3 : Kin::Surface2));
            drawRoundedRect(g, nullptr, &cBg, rc, 7.0f);

            if (isSelected) {
                // 激活状态柔和内衬高亮与 Indigo 边框
                SolidBrush selOverlay(Kin::SurfaceSelected);
                drawRoundedRect(g, nullptr, &selOverlay, rc, 7.0f);
                Pen selPen(Kin::Accent, 1.5f);
                drawRoundedRect(g, &selPen, nullptr, rc, 7.0f);
            } else {
                Pen norPen(isHovered ? Kin::LineStrong : Kin::LineDefault, 1.0f);
                drawRoundedRect(g, &norPen, nullptr, rc, 7.0f);
            }

            // 模式标题
            g.DrawString(cards[i].title, -1, &fontHeading, PointF(rc.X + 12.0f, rc.Y + 12.0f), &textPrimary);

            // 小标签 Badge
            RectF tagRect(rc.X + 12.0f, rc.Y + 36.0f, 84.0f, 18.0f);
            SolidBrush tagBg(isSelected ? Kin::Accent : Kin::Surface4);
            drawRoundedRect(g, nullptr, &tagBg, tagRect, 3.0f);
            g.DrawString(cards[i].badge, -1, &fontMicro, PointF(tagRect.X + 6.0f, tagRect.Y + 2.0f), &textPrimary);

            // 说明文字
            g.DrawString(cards[i].desc1, -1, &fontSmall, PointF(rc.X + 12.0f, rc.Y + 62.0f), &textSecondary);
            g.DrawString(cards[i].desc2, -1, &fontSmall, PointF(rc.X + 12.0f, rc.Y + 79.0f), &textSecondary);
            g.DrawString(cards[i].note, -1, &fontMicro, PointF(rc.X + 12.0f, rc.Y + 100.0f), isSelected ? &textPrimary : &textMuted);

            // 激活/切换指示文字
            if (isSelected) {
                g.DrawString(L"● 已激活生效", -1, &fontSmall, PointF(rc.X + 12.0f, rc.Y + rc.Height - 24.0f), &positiveBrush);
            } else {
                const wchar_t* btnLbl = (cooldownMs > 0) ? L"显卡冷却中" : L"点击切换";
                SolidBrush swBrush((cooldownMs > 0) ? Kin::Warning : Kin::TextMuted);
                g.DrawString(btnLbl, -1, &fontMicro, PointF(rc.X + 12.0f, rc.Y + rc.Height - 24.0f), &swBrush);
            }
        }

        // 5. 底部栏：开机自启开关 与 操作按钮 (Y: h - 56px, #0f1011)
        const float footerY = static_cast<float>(h) - 56.0f;
        SolidBrush footerBg(Kin::Surface1);
        g.FillRectangle(&footerBg, 0.0f, footerY, static_cast<float>(w), 56.0f);

        Pen footerLinePen(Kin::LineSubtle, 1.0f);
        g.DrawLine(&footerLinePen, 0.0f, footerY, static_cast<float>(w), footerY);

        // KIN 切换开关 (Track 38x20px, Knob 14x14px)
        SolidBrush swTrackBg(autoStart ? Kin::Accent : Kin::Surface4);
        drawRoundedRect(g, nullptr, &swTrackBg, toggleRect_, 10.0f);

        const float knobX = autoStart ? (toggleRect_.X + 21.0f) : (toggleRect_.X + 3.0f);
        SolidBrush knobBrush(Color(255, 255, 255, 255));
        g.FillEllipse(&knobBrush, knobX, toggleRect_.Y + 3.0f, 14.0f, 14.0f);

        g.DrawString(L"开机自动启动 (免 UAC 计划任务)", -1, &fontBody, PointF(toggleRect_.X + 46.0f, footerY + 10.0f), &textPrimary);
        g.DrawString(L"系统登录时静默常驻托盘并自动载入保存的伽马模式", -1, &fontMicro, PointF(toggleRect_.X + 46.0f, footerY + 30.0f), &textMuted);

        // 最小化到托盘按钮
        SolidBrush minBtnBg(hoverMinBtn_ ? Kin::Surface3 : Kin::Surface2);
        Pen minBtnPen(Kin::LineDefault, 1.0f);
        drawRoundedRect(g, &minBtnPen, &minBtnBg, minBtnRect_, 5.0f);
        g.DrawString(L"最小化到托盘", -1, &fontSmall, PointF(minBtnRect_.X + 18.0f, minBtnRect_.Y + 7.5f), &textSecondary);

        // 退出程序按钮
        SolidBrush exitBtnBg(hoverExitBtn_ ? Kin::CriticalSoft : Kin::Surface2);
        Pen exitBtnPen(hoverExitBtn_ ? Kin::Critical : Kin::LineDefault, 1.0f);
        drawRoundedRect(g, &exitBtnPen, &exitBtnBg, exitBtnRect_, 5.0f);
        SolidBrush exitText(hoverExitBtn_ ? Kin::Critical : Kin::TextMuted);
        g.DrawString(L"退出程序", -1, &fontSmall, PointF(exitBtnRect_.X + 14.0f, exitBtnRect_.Y + 7.5f), &exitText);

        // 6. 非最大化时绘制 1px 细微外边框，完美脱离背景
        if (!IsZoomed(hwnd_)) {
            Pen winBorder(Color(28, 255, 255, 255), 1.0f);
            g.DrawRectangle(&winBorder, 0, 0, w - 1, h - 1);
        }

        // 7. 高速 Blit 到屏幕 DC，消除闪烁
        Graphics screen(hdc);
        screen.DrawImage(&buffer, 0, 0);
    }
};

} // namespace ModernUI
