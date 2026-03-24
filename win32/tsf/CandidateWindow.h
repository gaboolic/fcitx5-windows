#pragma once

#include <Windows.h>

#include <functional>
#include <string>
#include <vector>

namespace fcitx {

// Popup window: numbered candidates, highlight, mouse pick (keyboard handled in Tsf).
class CandidateWindow {
  public:
    CandidateWindow() = default;
    ~CandidateWindow();

    CandidateWindow(const CandidateWindow &) = delete;
    CandidateWindow &operator=(const CandidateWindow &) = delete;

    void setOnPick(std::function<void(int index)> cb) { onPick_ = std::move(cb); }

    void show(int screenX, int screenY, const std::vector<std::wstring> &labels,
              int highlightIndex);
    void hide();

    bool isVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }

  private:
    static LRESULT CALLBACK staticWndProc(HWND hwnd, UINT msg, WPARAM wp,
                                          LPARAM lp);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void ensureClass();
    void layoutAndPaint();
    void clampToWorkArea(int *screenX, int *screenY) const;
    int hitTestLine(int y) const;

    HWND hwnd_ = nullptr;
    int popupW_ = 0;
    int popupH_ = 0;
    std::vector<std::wstring> labels_;
    int highlight_ = 0;
    int lineHeight_ = 0;
    int padX_ = 8;
    int padY_ = 6;
    HFONT font_ = nullptr;
    std::function<void(int)> onPick_;
};

} // namespace fcitx
