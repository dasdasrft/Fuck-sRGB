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
// KIN Design System (v3.0.4) Design Tokens & Hierarchy
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
    static const Color SurfaceHover(18, 255, 255, 255);    // rgba(255, 255, 255, 0.045)
    static const Color SurfaceSelected(36, 94, 106, 210);  // rgba(94, 106, 210, 0.14)
    static const Color FocusRing(184, 130, 143, 255);      // rgba(130, 143, 255, 0.72)

    // Hairline Borders & Dividers (structure-felt-not-seen)
    static const Color LineSubtle(16, 255, 255, 255);      // rgba(255, 255, 255, 0.055)
    static const Color LineDefault(26, 255, 255, 255);     // rgba(255, 255, 255, 0.09)
    static const Color LineStrong(45, 255, 255, 255);      // rgba(255, 255, 255, 0.16)

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
    static const Color PositiveSoft(30, 80, 173, 125);     // rgba(80, 173, 125, 0.12)
    static const Color Warning(255, 198, 154, 69);         // #c69a45 (Cooldown protection)
    static const Color WarningSoft(30, 198, 154, 69);      // rgba(198, 154, 69, 0.12)
    static const Color Critical(255, 216, 88, 102);        // #d85866 (Danger / Close button hover)
    static const Color CriticalSoft(30, 216, 88, 102);     // rgba(216, 88, 102, 0.12)
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
    // 精准黄金分割固定尺寸：锁定 840 × 600，彻底禁止任何拉伸与位移
    static constexpr int windowWidth = 840;
    static constexpr int windowHeight = 600;

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
    bool hoverTitleClose_ = false;
    int hoverCardIndex_ = -1;
    bool hoverToggle_ = false;
    bool hoverMinBtn_ = false;
    bool hoverExitBtn_ = false;

    // 固定布局矩形区域
    RectF titleMinRect_{};
    RectF titleCloseRect_{};
    RectF graphCardRect_{};
    RectF telemetryCardRect_{};
    RectF cardRects_[4]{};
    RectF toggleRect_{};
    RectF minBtnRect_{};
    RectF exitBtnRect_{};

    void registerClass() {
        WNDCLASSW wc{};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hInstance = instance_;
        wc.lpfnWndProc = wndProc;
        wc.lpszClassName = className;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
        wc.hbrBackground = nullptr;
        RegisterClassW(&wc);
    }

    void create() {
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        const int posX = (screenW - windowWidth) / 2;
        const int posY = (screenH - windowHeight) / 2;

        // 固定尺寸窗口样式组合：无 WS_MAXIMIZEBOX，无 WS_THICKFRAME
        hwnd_ = CreateWindowExW(
            WS_EX_APPWINDOW,
            className,
            L"Fuck sRGB · 控制中心",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            posX, posY, windowWidth, windowHeight,
            nullptr, nullptr, instance_, this
        );

        if (!hwnd_) return;

        // Windows 11 深色模式与圆角
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
        DWORD corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        // 扩展边框保留 DWM 原生硬件级投影
        MARGINS margins = { 1, 1, 1, 1 };
        DwmExtendFrameIntoClientArea(hwnd_, &margins);

        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        // 初始化固定布局
        updateLayout();

        SetTimer(hwnd_, 101, 300, nullptr);
    }

    // 绝对锁定的黄金律布局计算：间距严格统一为 14px，绝无任何拉伸与位移
    void updateLayout() {
        const float w = static_cast<float>(windowWidth);
        const float h = static_cast<float>(windowHeight);

        // 标题栏：高度 44px，仅保留最小化与关闭按钮，无最大化
        const float titleBarH = 44.0f;
        const float btnW = 44.0f;
        titleCloseRect_ = RectF(w - btnW, 0.0f, btnW, titleBarH);
        titleMinRect_   = RectF(w - btnW * 2.0f, 0.0f, btnW, titleBarH);

        const float padX = 20.0f;
        const float availW = w - padX * 2.0f; // 800px

        // 上半部：EOTF 曲线 (393px) + 管线监控 (393px)，间距 14px，高度 224px
        const float startY = titleBarH + 14.0f; // 58px
        const float topRowH = 224.0f;
        const float gapX = 14.0f;
        const float halfW = (availW - gapX) * 0.50f; // 393px

        graphCardRect_ = RectF(padX, startY, halfW, topRowH);
        telemetryCardRect_ = RectF(padX + halfW + gapX, startY, halfW, topRowH);

        // 中间部：4 模式卡片 (每张宽 192.5px，高 230px)，间距 10px
        const float middleY = startY + topRowH + 14.0f; // 296px
        const float cardH = 230.0f;
        const float cardGap = 10.0f;
        const float cardW = (availW - cardGap * 3.0f) / 4.0f; // 192.5px

        for (int i = 0; i < 4; ++i) {
            cardRects_[i] = RectF(padX + i * (cardW + cardGap), middleY, cardW, cardH);
        }

        // 底部 Footer：Y = 540px，高度 60px
        const float footerY = h - 60.0f;
        toggleRect_ = RectF(padX, footerY + 20.0f, 38.0f, 20.0f);

        const float footerBtnH = 34.0f;
        const float exitW = 96.0f;
        const float minW = 128.0f;
        const float btnGap = 14.0f;
        const float btnY = footerY + (60.0f - footerBtnH) * 0.5f; // 553px

        exitBtnRect_ = RectF(w - padX - exitW, btnY, exitW, footerBtnH);
        minBtnRect_  = RectF(exitBtnRect_.X - btnGap - minW, btnY, minW, footerBtnH);
    }

    bool isOverTitleBarButtons(float x, float y) const {
        return titleMinRect_.Contains(x, y) || titleCloseRect_.Contains(x, y);
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
            if (wp) return 0; // 完全移除原生粗丑标题栏与非客户区边框
            return DefWindowProcW(hwnd, message, wp, lp);
        }

        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            POINT clientPt = pt;
            ScreenToClient(hwnd, &clientPt);

            // 标题栏 44px 拖拽移动区域
            if (clientPt.y >= 0 && clientPt.y < 44) {
                if (window->isOverTitleBarButtons(static_cast<float>(clientPt.x), static_cast<float>(clientPt.y))) {
                    return HTCLIENT; // 允许按钮响应悬停与点击
                }
                return HTCAPTION; // 允许拖拽移动整个窗口
            }

            // 彻底禁止任何边缘拉伸命中，永远返回 HTCLIENT
            return HTCLIENT;
        }

        case WM_GETMINMAXINFO: {
            // 双重硬件锁定：最小尺寸与最大尺寸强制相等，彻底锁死固定视窗大小
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize.x = windowWidth;
            info->ptMinTrackSize.y = windowHeight;
            info->ptMaxTrackSize.x = windowWidth;
            info->ptMaxTrackSize.y = windowHeight;
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
            return TRUE;

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
                if (window->hoverTitleMin_ || window->hoverTitleClose_ ||
                    window->hoverCardIndex_ >= 0 || window->hoverToggle_ ||
                    window->hoverMinBtn_ || window->hoverExitBtn_) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            return DefWindowProcW(hwnd, message, wp, lp);
        }

        case WM_CLOSE:
            window->hide();
            return 0;

        default:
            return DefWindowProcW(hwnd, message, wp, lp);
        }
    }

    void handleMouseMove(float x, float y) {
        bool oldTMin = hoverTitleMin_;
        bool oldTClose = hoverTitleClose_;
        int oldCard = hoverCardIndex_;
        bool oldToggle = hoverToggle_;
        bool oldMin = hoverMinBtn_;
        bool oldExit = hoverExitBtn_;

        hoverTitleMin_ = titleMinRect_.Contains(x, y);
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

        if (oldTMin != hoverTitleMin_ || oldTClose != hoverTitleClose_ ||
            oldCard != hoverCardIndex_ || oldToggle != hoverToggle_ ||
            oldMin != hoverMinBtn_ || oldExit != hoverExitBtn_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd_, 0 };
        TrackMouseEvent(&tme);
    }

    void handleMouseLeave() {
        hoverTitleMin_ = false;
        hoverTitleClose_ = false;
        hoverCardIndex_ = -1;
        hoverToggle_ = false;
        hoverMinBtn_ = false;
        hoverExitBtn_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void handleMouseClick(float x, float y) {
        if (titleMinRect_.Contains(x, y)) {
            ShowWindow(hwnd_, SW_MINIMIZE);
            return;
        }
        if (titleCloseRect_.Contains(x, y)) {
            hide();
            return;
        }

        const ULONGLONG cooldown = callbacks_.getCooldownRemainingMs ? callbacks_.getCooldownRemainingMs() : 0;

        for (int i = 0; i < 4; ++i) {
            if (cardRects_[i].Contains(x, y)) {
                if (cooldown == 0 && callbacks_.onSelectMode) {
                    callbacks_.onSelectMode(static_cast<Mode>(i));
                    refresh();
                }
                return;
            }
        }

        if (toggleRect_.Contains(x, y)) {
            if (callbacks_.onToggleAutoStart) {
                callbacks_.onToggleAutoStart();
                refresh();
            }
            return;
        }

        if (minBtnRect_.Contains(x, y)) {
            hide();
            return;
        }

        if (exitBtnRect_.Contains(x, y)) {
            if (callbacks_.onExit) {
                callbacks_.onExit();
            }
            return;
        }
    }

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

    void render(HDC hdc) {
        const int w = windowWidth;
        const int h = windowHeight;

        Bitmap buffer(w, h, PixelFormat32bppARGB);
        Graphics g(&buffer);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        // 1. 底层画布：KIN dark-canvas (#08090a)
        SolidBrush bgBrush(Kin::Canvas);
        g.FillRectangle(&bgBrush, 0, 0, w, h);

        // 字体配置：严格遵循 KIN 字级规范
        Font fontDisplay(L"Segoe UI", 12.0f, FontStyleBold, UnitPoint);
        Font fontHeading(L"Segoe UI", 10.5f, FontStyleBold, UnitPoint);
        Font fontBody(L"Segoe UI", 9.0f, FontStyleRegular, UnitPoint);
        Font fontSmall(L"Segoe UI", 8.0f, FontStyleRegular, UnitPoint);
        Font fontMicro(L"Segoe UI", 7.5f, FontStyleRegular, UnitPoint);

        SolidBrush textPrimary(Kin::TextPrimary);
        SolidBrush textSecondary(Kin::TextSecondary);
        SolidBrush textMuted(Kin::TextMuted);
        SolidBrush positiveBrush(Kin::Positive);

        StringFormat centerFmt;
        centerFmt.SetAlignment(StringAlignmentCenter);
        centerFmt.SetLineAlignment(StringAlignmentCenter);

        const Mode currentMode = callbacks_.getMode ? callbacks_.getMode() : Mode::Srgb;
        const HdrState hdr = callbacks_.getHdrState ? callbacks_.getHdrState() : HdrState::Unavailable;
        const bool autoStart = callbacks_.getAutoStart ? callbacks_.getAutoStart() : false;
        const DWORD dwmPid = callbacks_.getDwmPid ? callbacks_.getDwmPid() : 0;
        const ULONGLONG cooldownMs = callbacks_.getCooldownRemainingMs ? callbacks_.getCooldownRemainingMs() : 0;
        const bool hdrActive = (hdr == HdrState::Active);

        // 2. 自定义 KIN 标题栏 (44px, #0b0c0d)
        SolidBrush titleBg(Kin::Sidebar);
        g.FillRectangle(&titleBg, 0, 0, w, 44);

        Pen titleLinePen(Kin::LineSubtle, 1.0f);
        g.DrawLine(&titleLinePen, 0.0f, 44.0f, static_cast<float>(w), 44.0f);

        // 左侧品牌徽标 (与下方卡片对齐)
        const float brandX = graphCardRect_.X;
        RectF brandRect(brandX, 11.0f, 22.0f, 22.0f);
        SolidBrush brandBg(Kin::Surface3);
        Pen brandBorder(Kin::Accent, 1.0f);
        drawRoundedRect(g, &brandBorder, &brandBg, brandRect, 5.0f);

        GraphicsPath brandPath;
        brandPath.AddBezier(brandRect.X + 4.5f, brandRect.Y + 16.0f,
                            brandRect.X + 9.0f, brandRect.Y + 14.5f,
                            brandRect.X + 12.0f, brandRect.Y + 9.0f,
                            brandRect.X + 17.5f, brandRect.Y + 5.5f);
        Pen brandPen(Kin::Accent, 1.8f);
        g.DrawPath(&brandPen, &brandPath);

        // 标题文本与自动测距 (以 (0,0) 纯净基准精准测距，彻底杜绝坐标原点叠加漂移)
        const PointF titlePos(brandX + 30.0f, 11.5f);
        g.DrawString(L"Fuck sRGB", -1, &fontDisplay, titlePos, &textPrimary);

        RectF titleBBox;
        g.MeasureString(L"Fuck sRGB", -1, &fontDisplay, PointF(0.0f, 0.0f), &titleBBox);

        // 版本徽标药丸 (动态紧随标题，精准垂直居中且清晰可见，永不被遮挡或裁切)
        const float verBadgeX = titlePos.X + titleBBox.Width + 10.0f;
        RectF verTextBBox;
        g.MeasureString(L"v1.3.2", -1, &fontMicro, PointF(0.0f, 0.0f), &verTextBBox);
        const float verBadgeW = (std::max)(50.0f, verTextBBox.Width + 16.0f);
        const float verBadgeH = 20.0f;
        const float verBadgeY = 12.0f;
        RectF verBadge(verBadgeX, verBadgeY, verBadgeW, verBadgeH);

        SolidBrush verBg(Kin::Surface3);
        Pen verPen(Kin::LineDefault, 1.0f);
        drawRoundedRect(g, &verPen, &verBg, verBadge, 4.0f);
        g.DrawString(L"v1.3.2", -1, &fontMicro, verBadge, &centerFmt, &textSecondary);

        // 副标题文本 (动态紧随版本药丸，安全边距 12px)
        const float subX = verBadgeX + verBadgeW + 12.0f;
        g.DrawString(L"·   HDR 伽马精度与色彩系统", -1, &fontSmall,
                     PointF(subX, 13.5f), &textMuted);

        // 标题栏右侧控制按钮：仅保留最小化与关闭，简洁现代
        auto drawTitleBtn = [&](const RectF& rect, bool hovered, bool isClose) {
            if (hovered) {
                if (isClose) {
                    SolidBrush closeBg(Kin::Critical);
                    g.FillRectangle(&closeBg, rect);
                } else {
                    SolidBrush btnHover(Kin::SurfaceHover);
                    g.FillRectangle(&btnHover, rect);
                }
            }
            const float cx = rect.X + rect.Width * 0.5f;
            const float cy = rect.Y + rect.Height * 0.5f;
            Pen iconPen((isClose && hovered) ? Color(255, 255, 255, 255) : Kin::TextSecondary, 1.2f);

            if (!isClose) { // 最小化
                g.DrawLine(&iconPen, cx - 5.0f, cy, cx + 5.0f, cy);
            } else { // 关闭
                g.DrawLine(&iconPen, cx - 4.5f, cy - 4.5f, cx + 4.5f, cy + 4.5f);
                g.DrawLine(&iconPen, cx + 4.5f, cy - 4.5f, cx - 4.5f, cy + 4.5f);
            }
        };

        drawTitleBtn(titleMinRect_, hoverTitleMin_, false);
        drawTitleBtn(titleCloseRect_, hoverTitleClose_, true);

        // 3. 上半部左侧：EOTF 实时伽马响应曲线图表
        SolidBrush cardBg(Kin::Surface2);
        Pen cardPen(Kin::LineDefault, 1.0f);
        drawRoundedRect(g, &cardPen, &cardBg, graphCardRect_, 8.0f);

        g.DrawString(L"伽马响应曲线 (EOTF)", -1, &fontHeading,
                     PointF(graphCardRect_.X + 16.0f, graphCardRect_.Y + 14.0f), &textPrimary);
        g.DrawString(L"实时渲染", -1, &fontMicro,
                     PointF(graphCardRect_.X + graphCardRect_.Width - 65.0f, graphCardRect_.Y + 16.0f), &textMuted);

        // 坐标系严格安全边距
        const float gx = graphCardRect_.X + 38.0f;
        const float gy = graphCardRect_.Y + graphCardRect_.Height - 44.0f;
        const float gw = graphCardRect_.Width - 56.0f;
        const float gh = gy - (graphCardRect_.Y + 48.0f);

        Pen gridPen(Color(14, 255, 255, 255), 1.0f);
        Pen axisPen(Color(36, 255, 255, 255), 1.0f);
        g.DrawLine(&gridPen, gx, gy - gh * 0.5f, gx + gw, gy - gh * 0.5f);
        g.DrawLine(&gridPen, gx + gw * 0.5f, gy, gx + gw * 0.5f, gy - gh);
        g.DrawLine(&axisPen, gx, gy, gx + gw, gy);
        g.DrawLine(&axisPen, gx, gy, gx, gy - gh);

        g.DrawString(L"0", -1, &fontMicro, PointF(gx - 12.0f, gy - 6.0f), &textMuted);
        g.DrawString(L"0.5", -1, &fontMicro, PointF(gx - 22.0f, gy - gh * 0.5f - 6.0f), &textMuted);
        g.DrawString(L"1.0", -1, &fontMicro, PointF(gx - 22.0f, gy - gh - 7.0f), &textMuted);
        g.DrawString(L"0.5", -1, &fontMicro, PointF(gx + gw * 0.5f - 8.0f, gy + 4.0f), &textMuted);
        g.DrawString(L"1.0", -1, &fontMicro, PointF(gx + gw - 8.0f, gy + 4.0f), &textMuted);

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

        for (int m = 0; m < 4; ++m) {
            if (m != static_cast<int>(currentMode)) {
                plotCurve(m, Color(70, 139, 143, 152), 1.0f);
            }
        }
        plotCurve(static_cast<int>(currentMode), Kin::Accent, 2.8f);

        // 底部图例
        const wchar_t* legends[] = { L"● sRGB", L"● 2.2", L"● 2.4", L"● 线性" };
        const float legColW = gw / 4.0f;
        for (int i = 0; i < 4; ++i) {
            const bool isCur = (static_cast<int>(currentMode) == i);
            SolidBrush lBrush(isCur ? Kin::Accent : Kin::TextMuted);
            g.DrawString(legends[i], -1, &fontSmall, PointF(gx + i * legColW + 6.0f, gy + 22.0f), &lBrush);
        }

        // 3. 上半部右侧：系统管线监控面板
        drawRoundedRect(g, &cardPen, &cardBg, telemetryCardRect_, 8.0f);

        g.DrawString(L"系统管线监控", -1, &fontHeading,
                     PointF(telemetryCardRect_.X + 16.0f, telemetryCardRect_.Y + 14.0f), &textPrimary);

        // 右上角 HDR 状态指示徽章
        const wchar_t* badgeText = hdrActive ? L"HDR 已生效" : L"HDR 未开启";
        RectF badgeTextBBox;
        g.MeasureString(badgeText, -1, &fontMicro, PointF(0, 0), &badgeTextBBox);
        const float badgeW = badgeTextBBox.Width + 24.0f;
        RectF badgeRect(telemetryCardRect_.X + telemetryCardRect_.Width - badgeW - 16.0f,
                       telemetryCardRect_.Y + 13.0f, badgeW, 22.0f);
        SolidBrush badgeBg(hdrActive ? Kin::PositiveSoft : Kin::Surface3);
        Pen badgePen(hdrActive ? Kin::Positive : Kin::LineDefault, 1.0f);
        drawRoundedRect(g, &badgePen, &badgeBg, badgeRect, 4.0f);

        SolidBrush dotBrush(hdrActive ? Kin::Positive : Kin::TextMuted);
        g.FillEllipse(&dotBrush, badgeRect.X + 7.0f, badgeRect.Y + 7.0f, 8.0f, 8.0f);
        g.DrawString(badgeText, -1, &fontMicro, PointF(badgeRect.X + 18.0f, badgeRect.Y + 3.5f), &textPrimary);

        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        std::wstring resStr = L"3840 × 2160 @ 160Hz";
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
            resStr = std::to_wstring(dm.dmPelsWidth) + L" × " + std::to_wstring(dm.dmPelsHeight) +
                     L" @ " + std::to_wstring(dm.dmDisplayFrequency) + L"Hz";
        }

        auto drawRow = [&](float yOff, const wchar_t* label, const wchar_t* val, Color valCol) {
            g.DrawString(label, -1, &fontSmall,
                         PointF(telemetryCardRect_.X + 16.0f, telemetryCardRect_.Y + yOff), &textSecondary);
            SolidBrush vb(valCol);
            g.DrawString(val, -1, &fontSmall,
                         PointF(telemetryCardRect_.X + 105.0f, telemetryCardRect_.Y + yOff), &vb);
        };

        const float rowStart = 46.0f;
        const float rowGap = 24.0f;

        drawRow(rowStart + 0.0f * rowGap, L"当前显示器:", resStr.c_str(), Kin::TextPrimary);
        drawRow(rowStart + 1.0f * rowGap, L"HDR 状态:", hdrActive ? L"BT.2020 广色域 / 10-bit" : L"SDR 标准动态范围",
                hdrActive ? Kin::Positive : Kin::TextSecondary);
        std::wstring dwmText = dwmPid ? (L"PID " + std::to_wstring(dwmPid) + L" (内存着色器已接管)") : L"未就绪";
        drawRow(rowStart + 2.0f * rowGap, L"DWM 进程:", dwmText.c_str(), Kin::Positive);
        drawRow(rowStart + 3.0f * rowGap, L"开机自启动:", autoStart ? L"免 UAC 计划任务 (已启用)" : L"未开启",
                autoStart ? Kin::Accent : Kin::TextSecondary);
        if (cooldownMs > 0) {
            std::wstring cdStr = L"显卡防护中 (" + std::to_wstring((cooldownMs + 999) / 1000) + L"s)";
            drawRow(rowStart + 4.0f * rowGap, L"显卡防护:", cdStr.c_str(), Kin::Warning);
        } else {
            drawRow(rowStart + 4.0f * rowGap, L"显卡防护:", L"稳定就绪 (3000ms 保护守卫)", Kin::Positive);
        }

        g.DrawString(L"注: Windows HDR 混入原生 sRGB 导致暗部泛白，Gamma 2.2/2.4 可恢复纯正黑位。",
                     -1, &fontMicro,
                     PointF(telemetryCardRect_.X + 16.0f, telemetryCardRect_.Y + telemetryCardRect_.Height - 20.0f),
                     &textMuted);

        // 4. 中间部：4 模式卡片 (严格校准描述：线性 1.0 为纯测试模式，非 HDR 直通)
        struct CardInfo {
            const wchar_t* title;
            const wchar_t* badge;
            const wchar_t* desc1;
            const wchar_t* desc2;
            const wchar_t* note;
        };

        const CardInfo cards[4] = {
            { L"原生 sRGB", L"默认映射", L"Windows 默认", L"未校准色彩曲线", L"暗部明显发灰泛白" },
            { L"Gamma 2.2", L"黄金标准 ★", L"绝大多数游戏影视", L"标准显示器通用", L"通透饱满 细节均衡" },
            { L"Gamma 2.4", L"影院母带", L"BT.1886 母带标准", L"暗室沉浸视听", L"黑场深邃 沉浸浓郁" },
            { L"线性 1.0", L"测试模式", L"Gamma 1.0 线性", L"无曲线变换调试", L"仅供调色对比与测试" }
        };

        for (int i = 0; i < 4; ++i) {
            const RectF& rc = cardRects_[i];
            const bool isSelected = (static_cast<int>(currentMode) == i);
            const bool isHovered = (hoverCardIndex_ == i);

            SolidBrush cBg(isSelected ? Kin::Surface2 : (isHovered ? Kin::Surface3 : Kin::Surface2));
            drawRoundedRect(g, nullptr, &cBg, rc, 8.0f);

            if (isSelected) {
                SolidBrush selOverlay(Kin::SurfaceSelected);
                drawRoundedRect(g, nullptr, &selOverlay, rc, 8.0f);
                Pen selPen(Kin::Accent, 1.5f);
                drawRoundedRect(g, &selPen, nullptr, rc, 8.0f);
            } else {
                Pen norPen(isHovered ? Kin::LineStrong : Kin::LineDefault, 1.0f);
                drawRoundedRect(g, &norPen, nullptr, rc, 8.0f);
            }

            // 模式标题
            g.DrawString(cards[i].title, -1, &fontHeading, PointF(rc.X + 14.0f, rc.Y + 14.0f), &textPrimary);

            // 小标签 Badge
            RectF tagBBox;
            g.MeasureString(cards[i].badge, -1, &fontMicro, PointF(0, 0), &tagBBox);
            const float tagW = tagBBox.Width + 14.0f;
            RectF tagRect(rc.X + 14.0f, rc.Y + 38.0f, tagW, 18.0f);
            SolidBrush tagBg(isSelected ? Kin::Accent : Kin::Surface4);
            drawRoundedRect(g, nullptr, &tagBg, tagRect, 4.0f);
            g.DrawString(cards[i].badge, -1, &fontMicro, tagRect, &centerFmt, &textPrimary);

            // 描述文字 (严格自上而下稳定排布)
            g.DrawString(cards[i].desc1, -1, &fontSmall, PointF(rc.X + 14.0f, rc.Y + 64.0f), &textSecondary);
            g.DrawString(cards[i].desc2, -1, &fontSmall, PointF(rc.X + 14.0f, rc.Y + 84.0f), &textSecondary);
            g.DrawString(cards[i].note, -1, &fontMicro, PointF(rc.X + 14.0f, rc.Y + 106.0f), isSelected ? &textPrimary : &textMuted);

            // 底部状态与操作指示
            const float statusY = rc.Y + rc.Height - 28.0f;
            if (isSelected) {
                g.DrawString(L"● 已激活生效", -1, &fontBody, PointF(rc.X + 14.0f, statusY), &positiveBrush);
            } else {
                const wchar_t* btnLbl = (cooldownMs > 0) ? L"显卡冷却中" : L"点击切换";
                SolidBrush swBrush((cooldownMs > 0) ? Kin::Warning : Kin::TextMuted);
                g.DrawString(btnLbl, -1, &fontSmall, PointF(rc.X + 14.0f, statusY), &swBrush);
            }
        }

        // 5. 底部栏：开机自启开关 与 操作按钮 (高度 60px, #0f1011)
        const float footerY = static_cast<float>(h) - 60.0f;
        SolidBrush footerBg(Kin::Surface1);
        g.FillRectangle(&footerBg, 0.0f, footerY, static_cast<float>(w), 60.0f);

        Pen footerLinePen(Kin::LineSubtle, 1.0f);
        g.DrawLine(&footerLinePen, 0.0f, footerY, static_cast<float>(w), footerY);

        // KIN 切换开关 (Track 38x20px, Knob 14x14px)
        SolidBrush swTrackBg(autoStart ? Kin::Accent : Kin::Surface4);
        drawRoundedRect(g, nullptr, &swTrackBg, toggleRect_, 10.0f);

        const float knobX = autoStart ? (toggleRect_.X + 21.0f) : (toggleRect_.X + 3.0f);
        SolidBrush knobBrush(Color(255, 255, 255, 255));
        g.FillEllipse(&knobBrush, knobX, toggleRect_.Y + 3.0f, 14.0f, 14.0f);

        g.DrawString(L"开机自动启动 (免 UAC 计划任务)", -1, &fontBody, PointF(toggleRect_.X + 46.0f, footerY + 12.0f), &textPrimary);
        g.DrawString(L"系统登录时后台静默常驻系统托盘并自动应用偏好", -1, &fontMicro, PointF(toggleRect_.X + 46.0f, footerY + 32.0f), &textMuted);

        // 按钮文本格式化：全局居中对齐，绝不偏移出边框
        StringFormat btnFmt;
        btnFmt.SetAlignment(StringAlignmentCenter);
        btnFmt.SetLineAlignment(StringAlignmentCenter);

        // 最小化到托盘按钮 (宽度 128px, 高度 34px, 间距 14px, 文本居中)
        SolidBrush minBtnBg(hoverMinBtn_ ? Kin::Surface3 : Kin::Surface2);
        Pen minBtnPen(hoverMinBtn_ ? Kin::LineStrong : Kin::LineDefault, 1.0f);
        drawRoundedRect(g, &minBtnPen, &minBtnBg, minBtnRect_, 6.0f);
        g.DrawString(L"最小化到托盘", -1, &fontSmall, minBtnRect_, &btnFmt, &textSecondary);

        // 退出程序按钮 (宽度 96px, 高度 34px, 文本居中)
        SolidBrush exitBtnBg(hoverExitBtn_ ? Kin::CriticalSoft : Kin::Surface2);
        Pen exitBtnPen(hoverExitBtn_ ? Kin::Critical : Kin::LineDefault, 1.0f);
        drawRoundedRect(g, &exitBtnPen, &exitBtnBg, exitBtnRect_, 6.0f);
        SolidBrush exitText(hoverExitBtn_ ? Kin::Critical : Kin::TextMuted);
        g.DrawString(L"退出程序", -1, &fontSmall, exitBtnRect_, &btnFmt, &exitText);

        // 6. 绘制发丝级外边框，完美脱离背景
        Pen winBorder(Color(28, 255, 255, 255), 1.0f);
        g.DrawRectangle(&winBorder, 0, 0, w - 1, h - 1);

        // 7. 高速 Blit 到屏幕 DC，消除闪烁
        Graphics screen(hdc);
        screen.DrawImage(&buffer, 0, 0);
    }
};

} // namespace ModernUI
