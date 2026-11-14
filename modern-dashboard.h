#pragma once

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#pragma warning(push)
#pragma warning(disable: 4458)
#include <gdiplus.h>
#pragma warning(pop)

#include <cmath>
#include <string>
#include <vector>
#include <memory>
#include <functional>

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
    static constexpr wchar_t className[] = L"FuckSRGB.ModernDashboard.1";
    static constexpr int windowWidth = 720;
    static constexpr int windowHeight = 570;

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
            ShowWindow(hwnd_, SW_RESTORE);
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

    int hoverCardIndex_ = -1;
    bool hoverToggle_ = false;
    bool hoverMinBtn_ = false;
    bool hoverExitBtn_ = false;

    // 控件坐标区域（用于点击命中测试）
    RectF cardRects_[4]{};
    RectF toggleRect_{};
    RectF minBtnRect_{};
    RectF exitBtnRect_{};

    void registerClass() {
        WNDCLASSW wc{};
        wc.hInstance = instance_;
        wc.lpfnWndProc = wndProc;
        wc.lpszClassName = className;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
        wc.hbrBackground = nullptr; // 双缓冲无闪烁绘制
        RegisterClassW(&wc);
    }

    void create() {
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        const int posX = (screenW - windowWidth) / 2;
        const int posY = (screenH - windowHeight) / 2;

        hwnd_ = CreateWindowExW(
            WS_EX_APPWINDOW,
            className,
            L"Fuck sRGB · 控制中心",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            posX, posY, windowWidth, windowHeight,
            nullptr, nullptr, instance_, this
        );

        if (!hwnd_) return;

        // 启用 Windows 11 深色模式标题栏与圆角
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
        DWORD corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        // 启动 300ms 定时器用于更新倒计时与状态
        SetTimer(hwnd_, 101, 300, nullptr);
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
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            window->render(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return TRUE; // 拦截背景擦除，彻底杜绝闪烁

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

        case WM_LBUTTONUP: {
            const float x = static_cast<float>(GET_X_LPARAM(lp));
            const float y = static_cast<float>(GET_Y_LPARAM(lp));
            window->handleMouseClick(x, y);
            return 0;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lp) == HTCLIENT) {
                if (window->hoverCardIndex_ >= 0 || window->hoverToggle_ ||
                    window->hoverMinBtn_ || window->hoverExitBtn_) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            return DefWindowProcW(hwnd, message, wp, lp);
        }

        case WM_CLOSE:
            // 点右上角 X 默认最小化隐藏到系统托盘，保持后台平稳常驻
            window->hide();
            return 0;

        default:
            return DefWindowProcW(hwnd, message, wp, lp);
        }
    }

    void handleMouseMove(float x, float y) {
        int oldCard = hoverCardIndex_;
        bool oldToggle = hoverToggle_;
        bool oldMin = hoverMinBtn_;
        bool oldExit = hoverExitBtn_;

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

        if (oldCard != hoverCardIndex_ || oldToggle != hoverToggle_ ||
            oldMin != hoverMinBtn_ || oldExit != hoverExitBtn_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }

        // 跟踪鼠标离开窗口事件
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
    }

    void handleMouseClick(float x, float y) {
        const ULONGLONG cooldown = callbacks_.getCooldownRemainingMs ? callbacks_.getCooldownRemainingMs() : 0;
        
        // 点击卡片选择模式
        for (int i = 0; i < 4; ++i) {
            if (cardRects_[i].Contains(x, y)) {
                if (cooldown == 0 && callbacks_.onSelectMode) {
                    callbacks_.onSelectMode(static_cast<Mode>(i));
                    refresh();
                }
                return;
            }
        }

        // 点击开机自启开关
        if (toggleRect_.Contains(x, y)) {
            if (callbacks_.onToggleAutoStart) {
                callbacks_.onToggleAutoStart();
                refresh();
            }
            return;
        }

        // 最小化到托盘
        if (minBtnRect_.Contains(x, y)) {
            hide();
            return;
        }

        // 退出程序
        if (exitBtnRect_.Contains(x, y)) {
            if (callbacks_.onExit) {
                callbacks_.onExit();
            }
            return;
        }
    }

    // 辅助函数：绘制现代圆角矩形
    void drawRoundedRect(Graphics& g, const Pen* pen, const Brush* brush, const RectF& rect, float radius) {
        GraphicsPath path;
        const float d = radius * 2.0f;
        path.AddArc(rect.X, rect.Y, d, d, 180, 90);
        path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
        path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90);
        path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
        path.CloseFigure();

        if (brush) g.FillPath(brush, &path);
        if (pen) g.DrawPath(pen, &path);
    }

    // 主渲染流程：双缓冲 GDI+ 极速抗锯齿绘制
    void render(HDC hdc) {
        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const int w = clientRect.right - clientRect.left;
        const int h = clientRect.bottom - clientRect.top;
        if (w <= 0 || h <= 0) return;

        Bitmap buffer(w, h, PixelFormat32bppARGB);
        Graphics g(&buffer);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        // 1. 背景底色：高级深邃暗夜黑 (#161618)
        SolidBrush bgBrush(Color(255, 22, 22, 24));
        g.FillRectangle(&bgBrush, 0, 0, w, h);

        // 字体配置
        Font fontTitle(L"Segoe UI", 15.0f, FontStyleBold, UnitPoint);
        Font fontHeading(L"Segoe UI", 11.5f, FontStyleBold, UnitPoint);
        Font fontBody(L"Segoe UI", 9.5f, FontStyleRegular, UnitPoint);
        Font fontSmall(L"Segoe UI", 8.5f, FontStyleRegular, UnitPoint);
        Font fontTiny(L"Segoe UI", 7.5f, FontStyleRegular, UnitPoint);

        // 色彩配置
        SolidBrush textWhite(Color(255, 255, 255, 255));
        SolidBrush textGray(Color(255, 160, 160, 165));
        SolidBrush textDim(Color(255, 110, 110, 115));
        SolidBrush accentBrush(Color(255, 0, 120, 215)); // Win11 Accent Blue
        SolidBrush greenBrush(Color(255, 46, 204, 113)); // Active Green
        SolidBrush redBrush(Color(255, 235, 87, 87));

        const Mode currentMode = callbacks_.getMode ? callbacks_.getMode() : Mode::Srgb;
        const HdrState hdr = callbacks_.getHdrState ? callbacks_.getHdrState() : HdrState::Unavailable;
        const bool autoStart = callbacks_.getAutoStart ? callbacks_.getAutoStart() : false;
        const DWORD dwmPid = callbacks_.getDwmPid ? callbacks_.getDwmPid() : 0;
        const ULONGLONG cooldownMs = callbacks_.getCooldownRemainingMs ? callbacks_.getCooldownRemainingMs() : 0;

        // 2. 顶部 Header 状态栏 (Y: 18 ~ 60)
        g.DrawString(L"Fuck sRGB · 控制中心", -1, &fontTitle, PointF(26, 18), &textWhite);
        g.DrawString(L"Windows 桌面 HDR 伽马精度与黑位校正引擎", -1, &fontSmall, PointF(28, 44), &textDim);

        // 右上角 HDR 状态指示徽标
        const bool hdrActive = (hdr == HdrState::Active);
        RectF badgeRect(w - 195.0f, 22.0f, 170.0f, 28.0f);
        SolidBrush badgeBg(hdrActive ? Color(255, 18, 46, 32) : Color(255, 40, 40, 44));
        Pen badgePen(hdrActive ? Color(255, 46, 160, 90) : Color(255, 70, 70, 76), 1.0f);
        drawRoundedRect(g, &badgePen, &badgeBg, badgeRect, 6.0f);

        SolidBrush dotBrush(hdrActive ? Color(255, 46, 204, 113) : Color(255, 150, 150, 150));
        g.FillEllipse(&dotBrush, badgeRect.X + 12.0f, badgeRect.Y + 9.0f, 10.0f, 10.0f);

        const wchar_t* badgeText = hdrActive ? L"HDR 显示已激活" : L"HDR 未开启 / 离线";
        g.DrawString(badgeText, -1, &fontSmall, PointF(badgeRect.X + 28.0f, badgeRect.Y + 6.0f), &textWhite);

        // 3. 中上部左侧：实时伽马曲线可视化图表 (X: 25, Y: 75, W: 335, H: 200)
        RectF graphCard(25.0f, 75.0f, 335.0f, 200.0f);
        SolidBrush cardBg(Color(255, 30, 30, 34));
        Pen cardPen(Color(255, 48, 48, 54), 1.0f);
        drawRoundedRect(g, &cardPen, &cardBg, graphCard, 8.0f);

        g.DrawString(L"伽马转换函数 (EOTF 对比)", -1, &fontHeading, PointF(graphCard.X + 14.0f, graphCard.Y + 12.0f), &textWhite);

        // 绘制坐标系
        const float gx = graphCard.X + 38.0f;
        const float gy = graphCard.Y + 165.0f;
        const float gw = 265.0f;
        const float gh = 115.0f;

        Pen gridPen(Color(255, 48, 48, 54), 1.0f);
        Pen axisPen(Color(255, 80, 80, 88), 1.0f);
        // 网格线与标尺
        g.DrawLine(&gridPen, gx, gy - gh * 0.5f, gx + gw, gy - gh * 0.5f);
        g.DrawLine(&gridPen, gx + gw * 0.5f, gy, gx + gw * 0.5f, gy - gh);
        g.DrawLine(&axisPen, gx, gy, gx + gw, gy);
        g.DrawLine(&axisPen, gx, gy, gx, gy - gh);

        g.DrawString(L"0", -1, &fontTiny, PointF(gx - 12.0f, gy - 6.0f), &textDim);
        g.DrawString(L"1.0", -1, &fontTiny, PointF(gx + gw - 8.0f, gy + 4.0f), &textDim);
        g.DrawString(L"1.0", -1, &fontTiny, PointF(gx - 18.0f, gy - gh - 6.0f), &textDim);

        // 绘制 4 条曲线（原生、Gamma2.2、Gamma2.4、Linear）
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
                pts.push_back({gx + t * gw, gy - val * gh});
            }
            Pen pen(color, width);
            g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
        };

        // 先画非激活的参考细线
        for (int m = 0; m < 4; ++m) {
            if (m != static_cast<int>(currentMode)) {
                Color c = (m == 0) ? Color(100, 230, 126, 34) :
                          (m == 1) ? Color(100, 52, 152, 219) :
                          (m == 2) ? Color(100, 155, 89, 182) :
                                     Color(100, 149, 165, 166);
                plotCurve(m, c, 1.2f);
            }
        }
        // 再高亮激活的当前曲线（粗线加亮）
        Color activeColor = (currentMode == Mode::Srgb) ? Color(255, 243, 156, 18) :
                            (currentMode == Mode::Gamma22) ? Color(255, 0, 168, 255) :
                            (currentMode == Mode::Gamma24) ? Color(255, 190, 80, 255) :
                                                             Color(255, 236, 240, 241);
        plotCurve(static_cast<int>(currentMode), activeColor, 2.8f);

        // 曲线图例徽章 (底部标签)
        const wchar_t* legends[] = { L"● 原生", L"● 2.2", L"● 2.4", L"● 线性" };
        Color legendColors[] = { Color(255, 243, 156, 18), Color(255, 0, 168, 255),
                                 Color(255, 190, 80, 255), Color(255, 236, 240, 241) };
        for (int i = 0; i < 4; ++i) {
            SolidBrush lBrush(legendColors[i]);
            g.DrawString(legends[i], -1, &fontSmall, PointF(graphCard.X + 22.0f + i * 74.0f, graphCard.Y + 176.0f), &lBrush);
        }

        // 3. 中上部右侧：屏幕与管线状态监控面板 (X: 375, Y: 75, W: 320, H: 200)
        RectF infoCard(375.0f, 75.0f, 320.0f, 200.0f);
        drawRoundedRect(g, &cardPen, &cardBg, infoCard, 8.0f);

        g.DrawString(L"系统管线与色彩监控", -1, &fontHeading, PointF(infoCard.X + 14.0f, infoCard.Y + 12.0f), &textWhite);

        // 获取显示器分辨率与刷新率
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        std::wstring resStr = L"3840 × 2160 (主显示器)";
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
            resStr = std::to_wstring(dm.dmPelsWidth) + L" × " + std::to_wstring(dm.dmPelsHeight) +
                     L" @ " + std::to_wstring(dm.dmDisplayFrequency) + L"Hz";
        }

        auto drawRow = [&](float yOff, const wchar_t* label, const wchar_t* val, Color valCol) {
            g.DrawString(label, -1, &fontSmall, PointF(infoCard.X + 16.0f, infoCard.Y + yOff), &textGray);
            SolidBrush vb(valCol);
            g.DrawString(val, -1, &fontSmall, PointF(infoCard.X + 110.0f, infoCard.Y + yOff), &vb);
        };

        drawRow(42.0f, L"当前显示器:", resStr.c_str(), Color(255, 255, 255, 255));
        drawRow(66.0f, L"HDR 状态:", hdrActive ? L"已启用 (BT.2020 广色域)" : L"未开启 (标准动态范围)",
                hdrActive ? Color(255, 46, 204, 113) : Color(255, 180, 180, 180));
        
        std::wstring dwmText = dwmPid ? (L"PID " + std::to_wstring(dwmPid) + L" · 着色器注入成功") : L"未连接";
        drawRow(90.0f, L"DWM 进程:", dwmText.c_str(), Color(255, 46, 204, 113));
        drawRow(114.0f, L"开机自启动:", autoStart ? L"免 UAC 计划任务激活" : L"未开启",
                autoStart ? Color(255, 0, 168, 255) : Color(255, 180, 180, 180));

        // 显卡安全保护状态
        if (cooldownMs > 0) {
            std::wstring cdStr = L"显卡冷却保护中 (" + std::to_wstring((cooldownMs + 999) / 1000) + L"s)";
            drawRow(138.0f, L"显卡防护:", cdStr.c_str(), Color(255, 241, 196, 15));
        } else {
            drawRow(138.0f, L"显卡防护:", L"稳定就绪 (防掉驱动守卫)", Color(255, 46, 204, 113));
        }

        // 小贴士
        g.DrawString(L"提示: HDR 下 Windows 默认混入 sRGB 使暗部发灰，修正后黑位深邃饱满。",
                    -1, &fontTiny, PointF(infoCard.X + 14.0f, infoCard.Y + 168.0f), &textDim);

        // 4. 中下部：4 张色彩模式选择卡片 (Y: 290, H: 165)
        const float cardW = 158.0f;
        const float cardH = 165.0f;
        const float cardGap = 12.0f;
        const float startX = 25.0f;
        const float cardY = 290.0f;

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
            const float cx = startX + i * (cardW + cardGap);
            cardRects_[i] = RectF(cx, cardY, cardW, cardH);

            const bool isSelected = (static_cast<int>(currentMode) == i);
            const bool isHovered = (hoverCardIndex_ == i);

            // 卡片背景与边框
            SolidBrush cBg(isSelected ? Color(255, 16, 42, 68) :
                           isHovered  ? Color(255, 36, 36, 42) :
                                        Color(255, 28, 28, 32));
            Pen cPen(isSelected ? Color(255, 0, 140, 255) :
                     isHovered  ? Color(255, 75, 75, 85) :
                                  Color(255, 45, 45, 52), isSelected ? 2.0f : 1.0f);
            drawRoundedRect(g, &cPen, &cBg, cardRects_[i], 8.0f);

            // 模式标题
            g.DrawString(cards[i].title, -1, &fontHeading, PointF(cx + 12.0f, cardY + 14.0f), &textWhite);

            // 小标签 Badge
            RectF tagRect(cx + 12.0f, cardY + 40.0f, 96.0f, 20.0f);
            SolidBrush tagBg(isSelected ? Color(255, 0, 110, 210) : Color(255, 45, 45, 52));
            drawRoundedRect(g, nullptr, &tagBg, tagRect, 4.0f);
            g.DrawString(cards[i].badge, -1, &fontTiny, PointF(tagRect.X + 6.0f, tagRect.Y + 3.0f), &textWhite);

            // 说明文字
            g.DrawString(cards[i].desc1, -1, &fontSmall, PointF(cx + 12.0f, cardY + 70.0f), &textGray);
            g.DrawString(cards[i].desc2, -1, &fontSmall, PointF(cx + 12.0f, cardY + 90.0f), &textGray);
            g.DrawString(cards[i].note, -1, &fontTiny, PointF(cx + 12.0f, cardY + 115.0f), isSelected ? &textWhite : &textDim);

            // 选中标记
            if (isSelected) {
                SolidBrush actBg(Color(255, 46, 204, 113));
                g.DrawString(L"✓ 当前使用", -1, &fontSmall, PointF(cx + 12.0f, cardY + 138.0f), &actBg);
            } else {
                SolidBrush swBg(Color(255, 120, 120, 130));
                g.DrawString(cooldownMs > 0 ? L"冷却中" : L"点击切换", -1, &fontTiny, PointF(cx + 12.0f, cardY + 138.0f), &swBg);
            }
        }

        // 5. 底部栏：开机自启开关 与 操作按钮 (Y: 480 ~ 530)
        const float footerY = 482.0f;

        // 开机自启动开关区域 (X: 25, Y: footerY)
        toggleRect_ = RectF(25.0f, footerY + 2.0f, 44.0f, 24.0f);
        SolidBrush swTrackBg(autoStart ? Color(255, 0, 120, 215) : Color(255, 55, 55, 60));
        drawRoundedRect(g, nullptr, &swTrackBg, toggleRect_, 12.0f);

        // 开关滑块按钮 (Knob)
        const float knobX = autoStart ? (toggleRect_.X + 22.0f) : (toggleRect_.X + 3.0f);
        SolidBrush knobBrush(Color(255, 255, 255, 255));
        g.FillEllipse(&knobBrush, knobX, toggleRect_.Y + 3.0f, 18.0f, 18.0f);

        g.DrawString(L"开机自动启动 (免 UAC 计划任务)", -1, &fontBody, PointF(toggleRect_.X + 54.0f, footerY + 3.0f), &textWhite);
        g.DrawString(L"开机时自动随系统后台静默启动并应用偏好", -1, &fontTiny, PointF(toggleRect_.X + 54.0f, footerY + 24.0f), &textDim);

        // 最小化到托盘按钮 (X: 505)
        minBtnRect_ = RectF(w - 225.0f, footerY + 4.0f, 110.0f, 32.0f);
        SolidBrush minBtnBg(hoverMinBtn_ ? Color(255, 48, 48, 54) : Color(255, 36, 36, 40));
        Pen minBtnPen(Color(255, 60, 60, 68), 1.0f);
        drawRoundedRect(g, &minBtnPen, &minBtnBg, minBtnRect_, 6.0f);
        g.DrawString(L"最小化到托盘", -1, &fontSmall, PointF(minBtnRect_.X + 16.0f, minBtnRect_.Y + 7.0f), &textWhite);

        // 退出程序按钮 (X: 625)
        exitBtnRect_ = RectF(w - 105.0f, footerY + 4.0f, 80.0f, 32.0f);
        SolidBrush exitBtnBg(hoverExitBtn_ ? Color(255, 60, 30, 30) : Color(255, 36, 36, 40));
        Pen exitBtnPen(hoverExitBtn_ ? Color(255, 200, 60, 60) : Color(255, 60, 60, 68), 1.0f);
        drawRoundedRect(g, &exitBtnPen, &exitBtnBg, exitBtnRect_, 6.0f);
        SolidBrush exitText(hoverExitBtn_ ? Color(255, 255, 100, 100) : Color(255, 180, 180, 180));
        g.DrawString(L"退出", -1, &fontSmall, PointF(exitBtnRect_.X + 26.0f, exitBtnRect_.Y + 7.0f), &exitText);

        // 6. 将离线缓冲一次性高速 Blit 到屏幕 DC，消除闪烁
        Graphics screen(hdc);
        screen.DrawImage(&buffer, 0, 0);
    }
};

} // namespace ModernUI
