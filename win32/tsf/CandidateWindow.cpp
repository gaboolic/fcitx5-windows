#include "CandidateWindow.h"

#include "../dll/register.h"

#include <algorithm>
#include <windowsx.h>

namespace fcitx {

namespace {
constexpr wchar_t kClassName[] = L"Fcitx5CandidateWnd";
}

CandidateWindow::~CandidateWindow() {
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void CandidateWindow::ensureClass() {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = staticWndProc;
    wc.hInstance = dllInstance;
    wc.lpszClassName = kClassName;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.style = CS_SAVEBITS;
    RegisterClassExW(&wc);
    registered = true;
}

LRESULT CALLBACK CandidateWindow::staticWndProc(HWND hwnd, UINT msg, WPARAM wp,
                                                LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        auto *self = static_cast<CandidateWindow *>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    auto *self = reinterpret_cast<CandidateWindow *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        return self->handleMessage(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CandidateWindow::handleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT cr;
        GetClientRect(hwnd, &cr);
        FillRect(hdc, &cr, (HBRUSH)(COLOR_WINDOW + 1));
        HFONT old = nullptr;
        if (font_) {
            old = (HFONT)SelectObject(hdc, font_);
        }
        SetBkMode(hdc, TRANSPARENT);
        for (size_t i = 0; i < labels_.size(); ++i) {
            RECT line = cr;
            line.top = static_cast<LONG>(padY_ + static_cast<int>(i) * lineHeight_);
            line.bottom = line.top + lineHeight_;
            if (static_cast<int>(i) == highlight_) {
                HBRUSH hi = CreateSolidBrush(RGB(200, 220, 255));
                FillRect(hdc, &line, hi);
                DeleteObject(hi);
            }
            RECT textRc = line;
            textRc.left += padX_;
            DrawTextW(hdc, labels_[i].c_str(), static_cast<int>(labels_[i].size()),
                      &textRc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        if (old) {
            SelectObject(hdc, old);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int y = GET_Y_LPARAM(lp);
        int idx = hitTestLine(y);
        if (idx >= 0 && idx < static_cast<int>(labels_.size()) && onPick_) {
            onPick_(idx);
        }
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void CandidateWindow::clampToWorkArea(int *screenX, int *screenY) const {
    if (!screenX || !screenY || popupW_ <= 0 || popupH_ <= 0) {
        return;
    }
    POINT p = {*screenX, *screenY};
    HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) {
        return;
    }
    const RECT &wr = mi.rcWork;
    if (*screenX + popupW_ > wr.right) {
        *screenX = wr.right - popupW_;
    }
    if (*screenY + popupH_ > wr.bottom) {
        *screenY = wr.bottom - popupH_;
    }
    if (*screenX < wr.left) {
        *screenX = wr.left;
    }
    if (*screenY < wr.top) {
        *screenY = wr.top;
    }
}

int CandidateWindow::hitTestLine(int y) const {
    if (lineHeight_ <= 0) {
        return -1;
    }
    int inner = y - padY_;
    if (inner < 0) {
        return -1;
    }
    return inner / lineHeight_;
}

void CandidateWindow::layoutAndPaint() {
    if (!hwnd_) {
        return;
    }
    HDC hdc = GetDC(hwnd_);
    HFONT old = nullptr;
    if (font_) {
        old = (HFONT)SelectObject(hdc, font_);
    }
    int maxW = 80;
    for (const auto &s : labels_) {
        SIZE sz = {};
        GetTextExtentPoint32W(hdc, s.c_str(), static_cast<int>(s.size()), &sz);
        maxW = (std::max)(maxW, static_cast<int>(sz.cx));
    }
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    lineHeight_ = tm.tmHeight + tm.tmExternalLeading + 2;
    if (old) {
        SelectObject(hdc, old);
    }
    ReleaseDC(hwnd_, hdc);

    int h = padY_ * 2 + lineHeight_ * static_cast<int>(labels_.size());
    int w = padX_ * 2 + maxW;
    popupW_ = w;
    popupH_ = h;
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, w, h,
                 SWP_NOMOVE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void CandidateWindow::show(int screenX, int screenY,
                           const std::vector<std::wstring> &labels,
                           int highlightIndex) {
    labels_ = labels;
    highlight_ = highlightIndex;
    if (labels_.empty()) {
        hide();
        return;
    }
    ensureClass();
    if (!font_) {
        font_ = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    if (!hwnd_) {
        hwnd_ =
            CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            kClassName, L"", WS_POPUP | WS_BORDER, screenX, screenY,
                            100, 100, nullptr, nullptr, dllInstance, this);
    }
    layoutAndPaint();
    clampToWorkArea(&screenX, &screenY);
    SetWindowPos(hwnd_, HWND_TOPMOST, screenX, screenY, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(hwnd_, SW_SHOWNA);
    UpdateWindow(hwnd_);
}

void CandidateWindow::hide() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
    labels_.clear();
}

} // namespace fcitx
