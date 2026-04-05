#pragma once

#include <Windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fcitx {

namespace webpanel {
struct WebPanelConfig;
}

// Popup window: numbered candidates, highlight, mouse pick (keyboard handled in
// Tsf).
class CandidateWindow {
  public:
    CandidateWindow();
    ~CandidateWindow();

    CandidateWindow(const CandidateWindow &) = delete;
    CandidateWindow &operator=(const CandidateWindow &) = delete;

    void setOnPick(std::function<void(int index)> cb) {
        onPick_ = std::move(cb);
    }

    void show(int screenX, int screenY,
              const std::vector<std::wstring> &candidates, int highlightIndex);
    void hide();

    bool isVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }

#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
    // Internal entry points used by COM event handlers defined in the .cpp.
    HRESULT onWebViewEnvironmentCreated(HRESULT result, void *env);
    HRESULT onWebViewControllerCreated(HRESULT result, void *controller);
    HRESULT onWebViewMessage(void *args);
    HRESULT onWebViewNavigationCompleted(void *args);
#endif

  private:
    static LRESULT CALLBACK staticWndProc(HWND hwnd, UINT msg, WPARAM wp,
                                          LPARAM lp);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void ensureClass();
    void ensureWindow();
    void reloadConfig();
    void recreateFont();
    void ensureWebView();
    void destroyWebView();
    void resizeWebView();
    void syncWebView();
    void layoutAndPaint();
    void paintFallback(HDC hdc, const RECT &clientRect);
    void clampToWorkArea(int *screenX, int *screenY) const;
    void clampWindowToWorkArea();
    std::wstring candidateLabel(size_t index) const;
    std::wstring candidateDisplayText(size_t index) const;
    int hitTestLine(int y) const;

#if FCITX5_WINDOWS_CANDIDATE_UI_WEBVIEW
    void pushWebViewState();
    void handleWebViewHostMessage(const std::wstring &msg);
    void applyWebViewLayoutRect(int contentRight, int contentBottom, double dx,
                                double dy, bool dragging);
#endif

    struct WebViewState;

    HWND hwnd_ = nullptr;
    int anchorX_ = 0;
    int anchorY_ = 0;
    int popupW_ = 0;
    int popupH_ = 0;
    UINT lastFontDpi_ = 0;
    std::vector<std::wstring> candidates_;
    int highlight_ = 0;
    int lineHeight_ = 0;
    int padX_ = 8;
    int padY_ = 6;
    HFONT font_ = nullptr;
    unsigned webViewEpoch_ = 0;
    std::unique_ptr<webpanel::WebPanelConfig> config_;
    std::function<void(int)> onPick_;
    std::unique_ptr<WebViewState> webView_;
};

} // namespace fcitx
