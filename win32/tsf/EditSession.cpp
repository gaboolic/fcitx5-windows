#include "CandidateListUiElement.h"
#include "tsf.h"

#include <fcitx-utils/keysym.h>

#include <algorithm>
#include <cstdint>
#include "TsfStubLog.h"
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <imm.h>

namespace {

using Microsoft::WRL::ComPtr;

/// Ctrl/Alt chords are host shortcuts (e.g. Ctrl+Back); must not go to IME.
bool tsfChordHasCtrlOrAlt() {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
           (GetKeyState(VK_MENU) & 0x8000) != 0;
}

/// AltGr is LeftCtrl+RightAlt — not a Ctrl+Alt editor shortcut; Latin must
/// reach IME.
bool tsfIsAltGrPhysicalDown() {
    return (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 &&
           (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
}

/// Latin A–Z: pass through on Ctrl/Alt chords.
/// - Ctrl/Alt: require GetKeyState AND GetAsyncKeyState both high so a stale
///   GetKeyState alone (e.g. after Ctrl+A) does not suppress IME.
bool tsfLatinKeyShouldPassToApp() {
    if (tsfIsAltGrPhysicalDown()) {
        return false;
    }
    const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) &&
                          (GetAsyncKeyState(VK_CONTROL) & 0x8000);
    const bool altDown =
        (GetKeyState(VK_MENU) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000);
    return ctrlDown || altDown;
}

std::string tsfWideToUtf8(const std::wstring &text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr,
                                         0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        utf8.data(), size, nullptr, nullptr);
    return utf8;
}

/// Caret in client coords of hwndCaret → screen. Many hosts return empty
/// ITfContextView::GetTextExt; GUI thread caret matches the real insertion
/// point. US-keyboard punctuation keys: must reach fcitx (punctuation addon)
/// instead of the host.
bool tsfIsChineseModePunctuationVk(unsigned vk) {
    switch (vk) {
    case VK_OEM_1: // ; :
    case VK_OEM_2: // / ?
    case VK_OEM_3: // ` ~
    case VK_OEM_4: // [ {
    case VK_OEM_5: // \ |
    case VK_OEM_6: // ] }
    case VK_OEM_7: // ' "
    case VK_OEM_PLUS:
    case VK_OEM_COMMA:
    case VK_OEM_MINUS:
    case VK_OEM_PERIOD:
    case VK_OEM_102: // extra key on some layouts
    case VK_DECIMAL: // numpad .
        return true;
    default:
        return false;
    }
}

bool tsfIsChineseModeShiftNumberSymbolVk(unsigned vk) {
    if ((GetKeyState(VK_SHIFT) & 0x8000) == 0) {
        return false;
    }
    return vk >= static_cast<unsigned>('0') && vk <= static_cast<unsigned>('9');
}

wchar_t tsfShiftNumberFallbackSymbol(unsigned vk) {
    switch (vk) {
    case '1':
        return L'!';
    case '2':
        return L'@';
    case '3':
        return L'#';
    case '4':
        return L'$';
    case '5':
        return L'%';
    case '6':
        return L'^';
    case '7':
        return L'&';
    case '8':
        return L'*';
    case '9':
        return L'(';
    case '0':
        return L')';
    default:
        return L'\0';
    }
}

bool tsfCanAttemptRawImeKey(unsigned vk) {
    if ((vk >= static_cast<unsigned>('0') &&
         vk <= static_cast<unsigned>('9')) ||
        (vk >= static_cast<unsigned>('A') &&
         vk <= static_cast<unsigned>('Z'))) {
        return true;
    }
    switch (vk) {
    case VK_OEM_1:
    case VK_OEM_2:
    case VK_OEM_3:
    case VK_OEM_4:
    case VK_OEM_5:
    case VK_OEM_6:
    case VK_OEM_7:
    case VK_OEM_PLUS:
    case VK_OEM_COMMA:
    case VK_OEM_MINUS:
    case VK_OEM_PERIOD:
    case VK_OEM_102:
    case VK_DECIMAL:
        return true;
    default:
        return false;
    }
}

std::string tsfHresultString(HRESULT hr) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase
        << static_cast<unsigned long>(static_cast<unsigned>(hr));
    return oss.str();
}

/// One-line **IME thread vs focus / foreground / cursor** for correlating
/// `candidatePos` lines (compare with **Weasel** `Composition.cpp` /
/// `CandidateList.cpp` which use **GetActiveView** + focus).
void tsfTraceCandidateSnapshot(const char *where) {
    const DWORD imeTid = GetCurrentThreadId();
    HWND focus = GetFocus();
    HWND fg = GetForegroundWindow();
    DWORD focusTid = 0;
    DWORD fgTid = 0;
    if (focus) {
        focusTid = GetWindowThreadProcessId(focus, nullptr);
    }
    if (fg) {
        fgTid = GetWindowThreadProcessId(fg, nullptr);
    }
    POINT cur = {};
    GetCursorPos(&cur);
    fcitx::tsfTrace(
        std::string("candidatePos snapshot ") + where + " imeTid=" +
        std::to_string(static_cast<unsigned long>(imeTid)) + " focusHwnd=" +
        std::to_string(reinterpret_cast<std::uintptr_t>(focus)) +
        " focusTid=" + std::to_string(static_cast<unsigned long>(focusTid)) +
        " fgHwnd=" + std::to_string(reinterpret_cast<std::uintptr_t>(fg)) +
        " fgTid=" + std::to_string(static_cast<unsigned long>(fgTid)) +
        " cursorScr=" + std::to_string(static_cast<long>(cur.x)) + "," +
        std::to_string(static_cast<long>(cur.y)));
}

/// **Weasel** `CGetTextExtentEditSession` uses a **collapsed** range (caret /
/// selection), not the full composition span — **GetTextExt** on the full range
/// often maps to the wrong box in Chromium / QQ.
bool tsfCloneRangeCollapsedAtPreeditCaret(TfEditCookie ec,
                                          ITfRange *compositionRange,
                                          int caretUtf16,
                                          ComPtr<ITfRange> *outRange) {
    if (!compositionRange || !outRange) {
        return false;
    }
    ComPtr<ITfRange> r;
    if (FAILED(compositionRange->Clone(r.ReleaseAndGetAddressOf())) || !r) {
        return false;
    }
    if (FAILED(r->Collapse(ec, TF_ANCHOR_START))) {
        return false;
    }
    if (caretUtf16 > 0) {
        LONG moved = 0;
        if (FAILED(r->ShiftEnd(ec, caretUtf16, &moved, nullptr))) {
            return false;
        }
    }
    if (FAILED(r->Collapse(ec, TF_ANCHOR_END))) {
        return false;
    }
    *outRange = std::move(r);
    return true;
}

/// Map **ITfContextView::GetTextExt** rect to a screen anchor (bottom-left).
/// Follows Weasel **Composition.cpp** `CGetTextExtentEditSession`: reject CUAS
/// garbage **(0,0)**; optional **enhanced** rect when top-left is outside fg
/// hwnd. Hosts often return **rw=0, rh>0** (caret strip / vertical line) —
/// Weasel accepts that; only **both** dimensions non-positive is truly
/// degenerate.
bool tsfTextExtRectToCandidateAnchor(RECT rc, POINT *out,
                                     const char *traceTag) {
    if (!out) {
        return false;
    }
    const int rw = rc.right - rc.left;
    const int rh = rc.bottom - rc.top;
    if (rw <= 0 && rh <= 0) {
        fcitx::tsfTrace(std::string("candidatePos ") + traceTag +
                        " textExt reject degenerate rw=" + std::to_string(rw) +
                        " rh=" + std::to_string(rh));
        return false;
    }
    if (rw <= 0 && rh > 0) {
        fcitx::tsfTrace(std::string("candidatePos ") + traceTag +
                        " textExt caretLine rw=0 rh=" + std::to_string(rh) +
                        " (Weasel accepts; anchor left,bottom)");
    } else if (rh <= 0 && rw > 0) {
        fcitx::tsfTrace(std::string("candidatePos ") + traceTag +
                        " textExt thinLine rh=0 rw=" + std::to_string(rw) +
                        " anchor left,bottom");
    }
    if (rc.left == 0 && rc.top == 0) {
        fcitx::tsfTrace(std::string("candidatePos ") + traceTag +
                        " textExt reject topleft00 (CUAS) rw=" +
                        std::to_string(rw) + " rh=" + std::to_string(rh));
        return false;
    }
    HWND fg = GetForegroundWindow();
    if (fg) {
        RECT wr = {};
        if (GetWindowRect(fg, &wr)) {
            if (rc.left < wr.left || rc.left > wr.right || rc.top < wr.top ||
                rc.top > wr.bottom) {
                POINT cp = {};
                const BOOL hasCaret = GetCaretPos(&cp);
                const int offsetx = static_cast<int>(wr.left) - rc.left +
                                    (hasCaret ? static_cast<int>(cp.x) : 0);
                const int offsety = static_cast<int>(wr.top) - rc.top +
                                    (hasCaret ? static_cast<int>(cp.y) : 0);
                fcitx::tsfTrace(std::string("candidatePos ") + traceTag +
                                " textExt outsideFg applyOffset hasCaret=" +
                                std::string(hasCaret ? "1" : "0") + " cp=" +
                                std::to_string(static_cast<long>(cp.x)) + "," +
                                std::to_string(static_cast<long>(cp.y)) +
                                " off=" + std::to_string(offsetx) + "," +
                                std::to_string(offsety));
                rc.left += offsetx;
                rc.right += offsetx;
                rc.top += offsety;
                rc.bottom += offsety;
            }
        }
    }
    out->x = rc.left;
    out->y = rc.bottom;
    fcitx::tsfTrace(
        std::string("candidatePos ") + traceTag +
        " textExt ok anchor=" + std::to_string(static_cast<long>(out->x)) +
        "," + std::to_string(static_cast<long>(out->y)));
    return true;
}

bool tsfTryGetTextExtForAnchor(TfEditCookie ec, ITfContextView *view,
                               ITfRange *extentRange, const char *traceTag,
                               const char *rangeMode, POINT *screenPt) {
    if (!view || !extentRange || !screenPt) {
        return false;
    }
    HWND w = nullptr;
    if (FAILED(view->GetWnd(&w)) || !w) {
        fcitx::tsfTrace(std::string("candidatePos ") + traceTag +
                        " GetWnd failed rangeMode=" + rangeMode);
        return false;
    }
    RECT rc{};
    BOOL clipped = FALSE;
    const HRESULT hrExt = view->GetTextExt(ec, extentRange, &rc, &clipped);
    const int rw = rc.right - rc.left;
    const int rh = rc.bottom - rc.top;
    fcitx::tsfTrace(std::string("candidatePos ") + traceTag +
                    " GetTextExt hr=" + tsfHresultString(hrExt) + " clipped=" +
                    (clipped ? "1" : "0") + " rangeMode=" + rangeMode +
                    " rcLTRB=" + std::to_string(static_cast<long>(rc.left)) +
                    "," + std::to_string(static_cast<long>(rc.top)) + "," +
                    std::to_string(static_cast<long>(rc.right)) + "," +
                    std::to_string(static_cast<long>(rc.bottom)) +
                    " rw=" + std::to_string(rw) + " rh=" + std::to_string(rh));
    if (!SUCCEEDED(hrExt)) {
        return false;
    }
    return tsfTextExtRectToCandidateAnchor(rc, screenPt, traceTag);
}

bool tsfScreenPtFromCaretGuiThread(DWORD threadId, POINT *out) {
    if (!out || threadId == 0) {
        return false;
    }
    GUITHREADINFO gti = {};
    gti.cbSize = sizeof(GUITHREADINFO);
    if (!GetGUIThreadInfo(threadId, &gti) || !gti.hwndCaret) {
        fcitx::tsfTrace("candidatePos guiThread noCaret tid=" +
                        std::to_string(static_cast<unsigned long>(threadId)) +
                        " (try fgAttach if IME thread != GUI thread)");
        return false;
    }
    const int cw = gti.rcCaret.right - gti.rcCaret.left;
    const int ch = gti.rcCaret.bottom - gti.rcCaret.top;
    if (cw <= 0 && ch <= 0) {
        fcitx::tsfTrace("candidatePos guiThread zeroCaret tid=" +
                        std::to_string(static_cast<unsigned long>(threadId)));
        return false;
    }
    out->x = gti.rcCaret.left;
    out->y = gti.rcCaret.bottom;
    if (!ClientToScreen(gti.hwndCaret, out)) {
        fcitx::tsfTrace("candidatePos guiThread ClientToScreen failed");
        return false;
    }
    fcitx::tsfTrace("candidatePos guiThread ok tid=" +
                    std::to_string(static_cast<unsigned long>(threadId)) +
                    " pt=" + std::to_string(static_cast<long>(out->x)) + "," +
                    std::to_string(static_cast<long>(out->y)));
    return true;
}

/// When **EnumViews** is **E_NOTIMPL** or empty, **ITfContextView::GetTextExt**
/// is unavailable; **ImmGetCompositionWindow** still often tracks the
/// composition caret (QQ / Chromium hosts that do not fill **GUITHREADINFO**).
bool tsfScreenPtImmCompositionAnchor(POINT *out) {
    if (!out) {
        return false;
    }
    HWND focus = GetFocus();
    if (!focus) {
        fcitx::tsfTrace("candidatePos immCompWin skip GetFocus=null");
        return false;
    }
    HIMC himc = ImmGetContext(focus);
    if (!himc) {
        fcitx::tsfTrace(
            "candidatePos immCompWin ImmGetContext=null focusHwnd=" +
            std::to_string(reinterpret_cast<std::uintptr_t>(focus)));
        return false;
    }
    COMPOSITIONFORM cf = {};
    const BOOL got = ImmGetCompositionWindow(himc, &cf);
    ImmReleaseContext(focus, himc);
    if (!got) {
        fcitx::tsfTrace(
            "candidatePos immCompWin ImmGetCompositionWindow=FALSE focusHwnd=" +
            std::to_string(reinterpret_cast<std::uintptr_t>(focus)));
        return false;
    }
    switch (cf.dwStyle) {
    case CFS_POINT:
        *out = cf.ptCurrentPos;
        fcitx::tsfTrace(std::string("candidatePos immCompWin CFS_POINT pt=") +
                        std::to_string(static_cast<long>(out->x)) + "," +
                        std::to_string(static_cast<long>(out->y)));
        return true;
    case CFS_RECT: {
        const RECT &r = cf.rcArea;
        const int rw = r.right - r.left;
        const int rh = r.bottom - r.top;
        if (rw <= 0 || rh <= 0) {
            fcitx::tsfTrace("candidatePos immCompWin CFS_RECT degenerate rw=" +
                            std::to_string(rw) + " rh=" + std::to_string(rh));
            return false;
        }
        POINT p = {};
        p.x = (r.left + r.right) / 2;
        p.y = r.bottom;
        if (!ClientToScreen(focus, &p)) {
            return false;
        }
        *out = p;
        fcitx::tsfTrace(std::string("candidatePos immCompWin CFS_RECT pt=") +
                        std::to_string(static_cast<long>(out->x)) + "," +
                        std::to_string(static_cast<long>(out->y)));
        return true;
    }
    case CFS_DEFAULT: {
        POINT p = cf.ptCurrentPos;
        if (!ClientToScreen(focus, &p)) {
            return false;
        }
        *out = p;
        fcitx::tsfTrace(std::string("candidatePos immCompWin CFS_DEFAULT pt=") +
                        std::to_string(static_cast<long>(out->x)) + "," +
                        std::to_string(static_cast<long>(out->y)));
        return true;
    }
    default:
        break;
    }
    if (cf.ptCurrentPos.x != 0 || cf.ptCurrentPos.y != 0) {
        POINT p = cf.ptCurrentPos;
        if (ClientToScreen(focus, &p)) {
            *out = p;
            fcitx::tsfTrace(
                "candidatePos immCompWin styleFallback pt=" +
                std::to_string(static_cast<long>(out->x)) + "," +
                std::to_string(static_cast<long>(out->y)) + " style=" +
                std::to_string(static_cast<unsigned long>(cf.dwStyle)));
            return true;
        }
    }
    fcitx::tsfTrace(
        "candidatePos immCompWin noAnchor style=" +
        std::to_string(static_cast<unsigned long>(cf.dwStyle)) +
        " ptCur=" + std::to_string(static_cast<long>(cf.ptCurrentPos.x)) + "," +
        std::to_string(static_cast<long>(cf.ptCurrentPos.y)));
    return false;
}

/// Chromium / Electron often fail **ITfContext::EnumViews**;
/// **GetGUIThreadInfo** on another thread needs **AttachThreadInput** (IME
/// thread != editor GUI thread). Prefer **GetFocus()** (real editor surface)
/// over **GetForegroundWindow()** (shell / root), so the caret thread matches
/// the typing target.
bool tsfScreenPtForegroundCaretAttach(POINT *out) {
    if (!out) {
        return false;
    }
    HWND target = GetFocus();
    if (!target) {
        target = GetForegroundWindow();
    }
    if (!target) {
        fcitx::tsfTrace("candidatePos fgAttach no focus or foreground hwnd");
        return false;
    }
    DWORD tid = GetWindowThreadProcessId(target, nullptr);
    if (!tid) {
        return false;
    }
    const DWORD cur = GetCurrentThreadId();
    fcitx::tsfTrace(
        "candidatePos fgAttach targetHwnd=" +
        std::to_string(reinterpret_cast<std::uintptr_t>(target)) +
        " targetTid=" + std::to_string(static_cast<unsigned long>(tid)) +
        " imeTid=" + std::to_string(static_cast<unsigned long>(cur)) +
        " sameThread=" + std::string(tid == cur ? "1" : "0"));
    if (tid == cur) {
        if (tsfScreenPtFromCaretGuiThread(tid, out)) {
            return true;
        }
        POINT cp = {};
        HWND foc = GetFocus();
        // GetCaretPos is relative to the caret owner; ClientToScreen(focus) is
        // only valid when they match. Chromium often leaves (0,0) and still
        // returns TRUE, which maps to screen (0,0) — top-left; reject that.
        if (foc && GetCaretPos(&cp) && ClientToScreen(foc, &cp)) {
            if (cp.x == 0 && cp.y == 0) {
                fcitx::tsfTrace(
                    "candidatePos fgAttach sameThread GetCaretPos+focus "
                    "rejected screen 0,0");
                return false;
            }
            *out = cp;
            fcitx::tsfTrace(
                "candidatePos fgAttach sameThread GetCaretPos+focus pt=" +
                std::to_string(static_cast<long>(cp.x)) + "," +
                std::to_string(static_cast<long>(cp.y)));
            return true;
        }
        return false;
    }
    if (!AttachThreadInput(cur, tid, TRUE)) {
        fcitx::tsfTrace(
            "candidatePos fgAttach AttachThreadInput failed gle=" +
            std::to_string(static_cast<unsigned long>(GetLastError())));
        return false;
    }
    GUITHREADINFO gti = {};
    gti.cbSize = sizeof(GUITHREADINFO);
    const BOOL got = GetGUIThreadInfo(tid, &gti);
    AttachThreadInput(cur, tid, FALSE);
    if (!got || !gti.hwndCaret) {
        fcitx::tsfTrace("candidatePos fgAttach no hwndCaret after attach tid=" +
                        std::to_string(static_cast<unsigned long>(tid)));
        return false;
    }
    const int cw = gti.rcCaret.right - gti.rcCaret.left;
    const int ch = gti.rcCaret.bottom - gti.rcCaret.top;
    if (cw <= 0 && ch <= 0) {
        fcitx::tsfTrace("candidatePos fgAttach zero rcCaret after attach");
        return false;
    }
    out->x = gti.rcCaret.left;
    out->y = gti.rcCaret.bottom;
    if (!ClientToScreen(gti.hwndCaret, out)) {
        fcitx::tsfTrace("candidatePos fgAttach ClientToScreen failed");
        return false;
    }
    fcitx::tsfTrace("candidatePos fgAttach ok tid=" +
                    std::to_string(static_cast<unsigned long>(tid)) +
                    " pt=" + std::to_string(static_cast<long>(out->x)) + "," +
                    std::to_string(static_cast<long>(out->y)));
    return true;
}

/// Last-resort anchor near the typing area (not mouse): bottom of the focused
/// window's client rect, biased toward the left (chat / wide editors: center
/// was often far from the real caret).
bool tsfScreenPtFocusClientBottomCenter(POINT *out) {
    if (!out) {
        return false;
    }
    HWND w = GetFocus();
    if (!w) {
        w = GetForegroundWindow();
    }
    if (!w) {
        return false;
    }
    RECT rc = {};
    if (!GetClientRect(w, &rc)) {
        return false;
    }
    const int width = rc.right - rc.left;
    POINT p = {};
    p.x = rc.left + (width > 0 ? (std::max)(8, width / 8) : 0);
    p.y = rc.bottom;
    if (!ClientToScreen(w, &p)) {
        return false;
    }
    *out = p;
    fcitx::tsfTrace(
        std::string("candidatePos approxClientBottomLeftish hwnd=") +
        std::to_string(reinterpret_cast<std::uintptr_t>(w)) +
        " pt=" + std::to_string(static_cast<long>(out->x)) + "," +
        std::to_string(static_cast<long>(out->y)));
    return true;
}

} // namespace

namespace fcitx {

namespace {

std::uint32_t tsfHostKeyboardStateMaskForPipe() {
    std::uint32_t m = 0;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        m |= static_cast<std::uint32_t>(KeyState::Shift);
    }
    if ((GetKeyState(VK_CAPITAL) & 1) != 0) {
        m |= static_cast<std::uint32_t>(KeyState::CapsLock);
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        m |= static_cast<std::uint32_t>(KeyState::Ctrl);
    }
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
        m |= static_cast<std::uint32_t>(KeyState::Alt);
    }
    if (((GetKeyState(VK_LWIN) & 0x8000) != 0) ||
        ((GetKeyState(VK_RWIN) & 0x8000) != 0)) {
        m |= static_cast<std::uint32_t>(KeyState::Super);
    }
    if ((GetKeyState(VK_NUMLOCK) & 1) != 0) {
        m |= static_cast<std::uint32_t>(KeyState::NumLock);
    }
    return m;
}

} // namespace

bool Tsf::queryCandidateAnchor(TfEditCookie ec, POINT *screenPt) {
    if (!textEditSinkContext_ || !screenPt) {
        return false;
    }
    ComPtr<ITfRange> extentRange;
    const char *rangeMode = "ias";
    if (compositionRange_ && engine_) {
        const int caretUtf16 = engine_->preeditCaretUtf16();
        if (!tsfCloneRangeCollapsedAtPreeditCaret(ec, compositionRange_.Get(),
                                                  caretUtf16, &extentRange) ||
            !extentRange) {
            fcitx::tsfTrace("candidatePos query caretClone failed; fallback "
                            "compositionFull "
                            "caretUtf16=" +
                            std::to_string(caretUtf16));
            extentRange = compositionRange_;
            rangeMode = "compositionFull_fallback";
        } else {
            rangeMode = "caret";
            fcitx::tsfTrace("candidatePos query extentRange=caret caretUtf16=" +
                            std::to_string(caretUtf16));
        }
    } else if (compositionRange_) {
        extentRange = compositionRange_;
        rangeMode = "compositionFull";
    } else {
        ComPtr<ITfInsertAtSelection> ias;
        if (FAILED(textEditSinkContext_->QueryInterface(
                IID_ITfInsertAtSelection,
                reinterpret_cast<void **>(ias.ReleaseAndGetAddressOf())))) {
            fcitx::tsfTrace("candidatePos query no ITfInsertAtSelection");
            return false;
        }
        if (FAILED(ias->InsertTextAtSelection(
                ec, TF_IAS_QUERYONLY, nullptr, 0,
                extentRange.ReleaseAndGetAddressOf())) ||
            !extentRange) {
            fcitx::tsfTrace("candidatePos query IAS QUERYONLY failed");
            return false;
        }
    }

    tsfTraceCandidateSnapshot("queryBegin");
    // Weasel **Composition.cpp** `_UpdateCompositionWindow`: `GetActiveView`
    // then `GetTextExt` on the **active** view (not `EnumViews`). Chromium
    // often returns
    // **E_NOTIMPL** for `EnumViews` but still implements `GetActiveView`.
    ComPtr<ITfContextView> activeView;
    const HRESULT hrActive = textEditSinkContext_->GetActiveView(
        activeView.ReleaseAndGetAddressOf());
    if (FAILED(hrActive) || !activeView) {
        fcitx::tsfTrace(
            "candidatePos GetActiveView hr=" + tsfHresultString(hrActive) +
            std::string(!activeView ? " nullView" : ""));
    } else {
        HWND avHwnd = nullptr;
        if (FAILED(activeView->GetWnd(&avHwnd)) || !avHwnd) {
            fcitx::tsfTrace(
                "candidatePos GetActiveView hr=ok GetWnd failed or null hwnd");
        } else {
            fcitx::tsfTrace(
                "candidatePos GetActiveView ok hwnd=" +
                std::to_string(reinterpret_cast<std::uintptr_t>(avHwnd)) +
                " (Weasel path; try GetTextExt before EnumViews)");
            if (tsfTryGetTextExtForAnchor(ec, activeView.Get(),
                                          extentRange.Get(), "weaselActiveView",
                                          rangeMode, screenPt)) {
                return true;
            }
        }
    }

    ComPtr<IEnumTfContextViews> enumViews;
    const HRESULT hrEnum =
        textEditSinkContext_->EnumViews(enumViews.ReleaseAndGetAddressOf());
    if (FAILED(hrEnum) || !enumViews) {
        fcitx::tsfTrace(
            "candidatePos EnumViews hr=" + tsfHresultString(hrEnum) +
            std::string(enumViews ? "" : " null enumerator") +
            " (GetActiveView+GetTextExt already tried above)");
        if (tsfScreenPtImmCompositionAnchor(screenPt)) {
            return true;
        }
        if (tsfScreenPtForegroundCaretAttach(screenPt)) {
            return true;
        }
        return false;
    }
    struct ViewEntry {
        ComPtr<ITfContextView> view;
        HWND hwnd = nullptr;
        int index = 0;
    };
    std::vector<ViewEntry> viewEntries;
    for (int idx = 0;; ++idx) {
        ITfContextView *viewRaw = nullptr;
        ULONG fetched = 0;
        if (FAILED(enumViews->Next(1, &viewRaw, &fetched)) || fetched == 0 ||
            !viewRaw) {
            break;
        }
        ComPtr<ITfContextView> view;
        view.Attach(viewRaw);
        HWND w = nullptr;
        if (FAILED(view->GetWnd(&w)) || !w) {
            continue;
        }
        viewEntries.push_back(ViewEntry{std::move(view), w, idx});
    }
    const HWND fg = GetForegroundWindow();
    std::stable_partition(
        viewEntries.begin(), viewEntries.end(), [fg](const ViewEntry &e) {
            if (!fg || !e.hwnd) {
                return false;
            }
            return e.hwnd == fg || IsChild(fg, e.hwnd) != FALSE;
        });
    fcitx::tsfTrace(
        "candidatePos enumViews count=" + std::to_string(viewEntries.size()) +
        " hrEnum=" + tsfHresultString(hrEnum) +
        " fgHwnd=" + std::to_string(reinterpret_cast<std::uintptr_t>(fg)));
    if (viewEntries.empty()) {
        fcitx::tsfTrace("candidatePos enumViews empty (no views)");
        if (tsfScreenPtImmCompositionAnchor(screenPt)) {
            return true;
        }
        if (tsfScreenPtForegroundCaretAttach(screenPt)) {
            return true;
        }
    }
    DWORD caretThread = 0;
    for (size_t i = 0; i < viewEntries.size(); ++i) {
        ViewEntry &ent = viewEntries[i];
        if (caretThread == 0) {
            caretThread = GetWindowThreadProcessId(ent.hwnd, nullptr);
        }
        const bool fgAssoc = fg && ent.hwnd &&
                             (ent.hwnd == fg || IsChild(fg, ent.hwnd) != FALSE);
        const std::string tag = std::string("view[") +
                                std::to_string(ent.index) + "]" +
                                (fgAssoc ? " fgAssoc" : "");
        if (tsfTryGetTextExtForAnchor(ec, ent.view.Get(), extentRange.Get(),
                                      tag.c_str(), rangeMode, screenPt)) {
            return true;
        }
    }
    if (caretThread != 0) {
        if (tsfScreenPtFromCaretGuiThread(caretThread, screenPt)) {
            return true;
        }
    }
    if (tsfScreenPtImmCompositionAnchor(screenPt)) {
        return true;
    }
    if (tsfScreenPtForegroundCaretAttach(screenPt)) {
        return true;
    }
    fcitx::tsfTrace(
        "candidatePos query exhausted; syncCandidateWindow fallbacks");
    return false;
}

void Tsf::resetCompositionState() {
    endCandidateListUiElement();
    composition_.Reset();
    compositionRange_.Reset();
    if (engine_) {
        engine_->clear();
    }
    pendingMousePick_ = -1;
}

bool Tsf::ensureCompositionStarted(TfEditCookie ec) {
    if (composition_) {
        return true;
    }
    ComPtr<ITfInsertAtSelection> insertAtSelection;
    if (FAILED(textEditSinkContext_->QueryInterface(
            IID_ITfInsertAtSelection,
            reinterpret_cast<void **>(
                insertAtSelection.ReleaseAndGetAddressOf())))) {
        return false;
    }
    ComPtr<ITfRange> range;
    if (FAILED(insertAtSelection->InsertTextAtSelection(
            ec, TF_IAS_QUERYONLY, nullptr, 0,
            range.ReleaseAndGetAddressOf()))) {
        return false;
    }
    ComPtr<ITfContextComposition> contextComposition;
    if (FAILED(textEditSinkContext_->QueryInterface(
            IID_ITfContextComposition,
            reinterpret_cast<void **>(
                contextComposition.ReleaseAndGetAddressOf())))) {
        return false;
    }
    if (FAILED(contextComposition->StartComposition(
            ec, range.Get(), this, composition_.ReleaseAndGetAddressOf())) ||
        !composition_) {
        return false;
    }
    compositionRange_.Reset();
    if (FAILED(composition_->GetRange(
            compositionRange_.ReleaseAndGetAddressOf())) ||
        !compositionRange_) {
        composition_.Reset();
        return false;
    }
    return true;
}

void Tsf::updatePreeditText(TfEditCookie ec) {
    if (!compositionRange_ || !engine_) {
        return;
    }
    const auto &p = engine_->preedit();
    compositionRange_->SetText(ec, 0, p.c_str(), static_cast<LONG>(p.size()));
    // After SetText the caret is at the start; align with fcitx Text::cursor().
    if (!textEditSinkContext_ || p.empty()) {
        return;
    }
    ComPtr<ITfRange> caretRange;
    if (FAILED(compositionRange_->Clone(caretRange.ReleaseAndGetAddressOf())) ||
        !caretRange) {
        return;
    }
    if (FAILED(caretRange->Collapse(ec, TF_ANCHOR_START))) {
        return;
    }
    const int caretUtf16 = engine_->preeditCaretUtf16();
    if (caretUtf16 > 0) {
        LONG moved = 0;
        if (FAILED(caretRange->ShiftEnd(ec, caretUtf16, &moved, nullptr))) {
            return;
        }
    }
    if (FAILED(caretRange->Collapse(ec, TF_ANCHOR_END))) {
        return;
    }
    TF_SELECTION sel{};
    sel.range = caretRange.Get();
    sel.style.ase = TF_AE_NONE;
    sel.style.fInterimChar = FALSE;
    textEditSinkContext_->SetSelection(ec, 1, &sel);
}

void Tsf::endCompositionCommit(TfEditCookie ec, const std::wstring &text,
                               bool clearEngineState) {
    tsfTrace("endCompositionCommit text=" + tsfWideToUtf8(text));
    candidateWin_.hide();
    endCandidateListUiElement();
    if (compositionRange_) {
        compositionRange_->SetText(ec, 0, text.c_str(),
                                   static_cast<LONG>(text.size()));
        if (textEditSinkContext_ && !text.empty()) {
            ComPtr<ITfRange> caretAfter;
            if (SUCCEEDED(compositionRange_->Clone(
                    caretAfter.ReleaseAndGetAddressOf())) &&
                caretAfter &&
                SUCCEEDED(caretAfter->Collapse(ec, TF_ANCHOR_END))) {
                TF_SELECTION sel{};
                sel.range = caretAfter.Get();
                sel.style.ase = TF_AE_NONE;
                sel.style.fInterimChar = FALSE;
                textEditSinkContext_->SetSelection(ec, 1, &sel);
            }
        }
    } else if (!text.empty() && textEditSinkContext_) {
        ComPtr<ITfInsertAtSelection> ias;
        if (SUCCEEDED(textEditSinkContext_->QueryInterface(
                IID_ITfInsertAtSelection,
                reinterpret_cast<void **>(ias.ReleaseAndGetAddressOf())))) {
            ComPtr<ITfRange> inserted;
            if (SUCCEEDED(ias->InsertTextAtSelection(
                    ec, 0, text.c_str(), static_cast<LONG>(text.size()),
                    inserted.ReleaseAndGetAddressOf())) &&
                inserted && SUCCEEDED(inserted->Collapse(ec, TF_ANCHOR_END))) {
                TF_SELECTION sel{};
                sel.range = inserted.Get();
                sel.style.ase = TF_AE_NONE;
                sel.style.fInterimChar = FALSE;
                textEditSinkContext_->SetSelection(ec, 1, &sel);
            }
        }
    }
    if (composition_) {
        composition_->EndComposition(ec);
    }
    composition_.Reset();
    compositionRange_.Reset();
    if (clearEngineState && engine_) {
        engine_->clear();
    }
}

void Tsf::endCompositionCancel(TfEditCookie ec) {
    candidateWin_.hide();
    endCandidateListUiElement();
    if (compositionRange_) {
        compositionRange_->SetText(ec, 0, L"", 0);
    }
    if (composition_) {
        composition_->EndComposition(ec);
    }
    composition_.Reset();
    compositionRange_.Reset();
    if (engine_) {
        engine_->clear();
    }
}

void Tsf::drainCommitsAfterEngine(TfEditCookie ec) {
    if (!engine_) {
        return;
    }
    for (;;) {
        const std::wstring w = engine_->drainNextCommit();
        if (w.empty()) {
            break;
        }
        // A single key may both commit the previous segment and leave a new
        // preedit/candidate state (common for table IMs like Wubi). Commit the
        // old TSF composition first, then let afterFcitxEngineKey() rebuild
        // composition from the engine's current state.
        endCompositionCommit(ec, w, false);
    }
}

void Tsf::afterFcitxEngineKey(TfEditCookie ec) {
    drainCommitsAfterEngine(ec);
    if (!engine_) {
        return;
    }
    if (engine_->preedit().empty() && engine_->candidates().empty()) {
        if (composition_) {
            endCompositionCancel(ec);
        }
    } else {
        if (!ensureCompositionStarted(ec)) {
            return;
        }
        updatePreeditText(ec);
        syncCandidateWindow(ec);
    }
}

HRESULT Tsf::queryCandidateListDocumentMgr(ITfDocumentMgr **ppDim) {
    if (!ppDim) {
        return E_INVALIDARG;
    }
    *ppDim = nullptr;
    if (!textEditSinkContext_) {
        return E_FAIL;
    }
    return textEditSinkContext_->GetDocumentMgr(ppDim);
}

void Tsf::endCandidateListUiElement() {
    if (candidateUiElementId_ == TF_INVALID_UIELEMENTID || !threadMgr_) {
        return;
    }
    ComPtr<ITfUIElementMgr> mgr;
    if (SUCCEEDED(threadMgr_->QueryInterface(
            IID_ITfUIElementMgr,
            reinterpret_cast<void **>(mgr.ReleaseAndGetAddressOf())))) {
        mgr->EndUIElement(candidateUiElementId_);
    }
    candidateUiElementId_ = TF_INVALID_UIELEMENTID;
}

void Tsf::syncCandidateListUiElement() {
    if (!threadMgr_ || !engine_) {
        endCandidateListUiElement();
        return;
    }
    if (engine_->candidates().empty()) {
        endCandidateListUiElement();
        return;
    }
    ComPtr<ITfUIElementMgr> mgr;
    if (FAILED(threadMgr_->QueryInterface(
            IID_ITfUIElementMgr,
            reinterpret_cast<void **>(mgr.ReleaseAndGetAddressOf())))) {
        return;
    }
    if (!candidateListUi_) {
        candidateListUi_.Attach(new CandidateListUiElement(this));
    }
    ITfUIElement *el = static_cast<ITfUIElement *>(candidateListUi_.Get());
    if (candidateUiElementId_ == TF_INVALID_UIELEMENTID) {
        BOOL show = TRUE;
        DWORD id = TF_INVALID_UIELEMENTID;
        if (SUCCEEDED(mgr->BeginUIElement(el, &show, &id))) {
            candidateUiElementId_ = id;
        }
    } else {
        mgr->UpdateUIElement(candidateUiElementId_);
    }
}

void Tsf::syncCandidateWindow(TfEditCookie ec) {
    if (!engine_ || !textEditSinkContext_) {
        candidateWin_.hide();
        endCandidateListUiElement();
        return;
    }
    const auto &cands = engine_->candidates();
    if (cands.empty()) {
        candidateWin_.hide();
        endCandidateListUiElement();
        return;
    }
    std::vector<std::wstring> labels;
    labels.reserve(cands.size());
    for (size_t i = 0; i < cands.size(); ++i) {
        if (i < 9) {
            labels.push_back(std::to_wstring(i + 1) + L". " + cands[i]);
        } else if (i == 9) {
            labels.push_back(L"0. " + cands[i]);
        } else {
            labels.push_back(std::to_wstring(i + 1) + L". " + cands[i]);
        }
    }
    // Do not seed with GetCursorPos: if queryCandidateAnchor fails without
    // writing pt, we must not show the candidate list at the mouse (common in
    // Electron).
    POINT pt = {};
    tsfTraceCandidateSnapshot("syncBegin");
    bool anchorOk = queryCandidateAnchor(ec, &pt);
    if (anchorOk && pt.x == 0 && pt.y == 0) {
        fcitx::tsfTrace("candidatePos sync queryAnchor ok but pt=0,0 rejected");
        anchorOk = false;
    }
    if (!anchorOk) {
        fcitx::tsfTrace("candidatePos sync trying fallbacks");
        bool anchored = false;
        const char *fallbackWinner = "none";
        if (tsfScreenPtImmCompositionAnchor(&pt)) {
            fcitx::tsfTrace("candidatePos sync fallback immCompWin");
            anchored = true;
            fallbackWinner = "immCompWin";
        }
        if (!anchored) {
            const HWND tryAnchors[] = {GetFocus(), GetForegroundWindow()};
            for (HWND anchor : tryAnchors) {
                if (!anchor) {
                    continue;
                }
                const DWORD tid = GetWindowThreadProcessId(anchor, nullptr);
                if (tid && tsfScreenPtFromCaretGuiThread(tid, &pt)) {
                    fcitx::tsfTrace(
                        "candidatePos sync fallback guiThread anchorHwnd=" +
                        std::to_string(
                            reinterpret_cast<std::uintptr_t>(anchor)));
                    anchored = true;
                    fallbackWinner = "guiThread";
                    break;
                }
            }
        }
        if (!anchored) {
            if (!tsfScreenPtFocusClientBottomCenter(&pt)) {
                GetCursorPos(&pt);
                fcitx::tsfTrace(
                    std::string(
                        "candidatePos sync fallback lastResort mouse=") +
                    std::to_string(static_cast<long>(pt.x)) + "," +
                    std::to_string(static_cast<long>(pt.y)));
                fallbackWinner = "lastResortMouse";
            } else {
                fallbackWinner = "approxClientBottomLeftish";
            }
        }
        fcitx::tsfTrace(std::string("candidatePos sync fallbackChosen=") +
                        fallbackWinner +
                        " finalPt=" + std::to_string(static_cast<long>(pt.x)) +
                        "," + std::to_string(static_cast<long>(pt.y)));
    } else {
        fcitx::tsfTrace(
            std::string("candidatePos sync chosen=queryAnchor pt=") +
            std::to_string(static_cast<long>(pt.x)) + "," +
            std::to_string(static_cast<long>(pt.y)));
    }
    fcitx::tsfTrace(std::string("candidatePos sync show pt=") +
                    std::to_string(static_cast<long>(pt.x)) + "," +
                    std::to_string(static_cast<long>(pt.y)));
    candidateWin_.show(pt.x, pt.y, labels, engine_->highlightIndex());
    syncCandidateListUiElement();
}

bool Tsf::keyWouldBeHandled(WPARAM wParam, LPARAM lParam) {
    if (!textEditSinkContext_ || !engine_) {
        return false;
    }
    const UINT vk = static_cast<UINT>(wParam);
    if (engine_->fcitxModifierHotkeyUsesFullKeyEvent(vk)) {
        return true;
    }
    if (engine_->imManagerHotkeyWouldEat(vk,
                                         static_cast<std::uintptr_t>(lParam))) {
        return true;
    }
    if (tsfChordHasCtrlOrAlt() && tsfCanAttemptRawImeKey(vk)) {
        return true;
    }
    if (vk == VK_SPACE && (GetKeyState(VK_CONTROL) & 0x8000)) {
        return true;
    }
    if (!chineseActive_) {
        return false;
    }
    if (vk == VK_RETURN || vk == VK_BACK || vk == VK_ESCAPE) {
        engine_->syncInputPanelFromIme();
    }
    const bool hasPanel =
        !engine_->preedit().empty() || !engine_->candidates().empty();
    const bool composing = composition_ != nullptr;
    // Do not eat Backspace/Enter/Escape when there is nothing to edit — let the
    // app handle lone Enter, etc.
    if (vk == VK_BACK) {
        if (tsfChordHasCtrlOrAlt()) {
            return false;
        }
        return hasPanel && !engine_->preedit().empty();
    }
    if (vk == VK_RETURN) {
        if (tsfChordHasCtrlOrAlt()) {
            return false;
        }
        return hasPanel;
    }
    if (vk == VK_ESCAPE) {
        if (tsfChordHasCtrlOrAlt()) {
            return false;
        }
        return hasPanel || composing;
    }
    if (vk == VK_SPACE) {
        if (tsfChordHasCtrlOrAlt()) {
            return false;
        }
        return !engine_->candidates().empty();
    }
    if (!engine_->candidates().empty() && !tsfChordHasCtrlOrAlt() &&
        ((((vk >= '0' && vk <= '9') &&
           (GetKeyState(VK_SHIFT) & 0x8000) == 0)) ||
         vk == VK_UP || vk == VK_DOWN)) {
        return true;
    }
    if (vk >= 'A' && vk <= 'Z') {
        return !tsfLatinKeyShouldPassToApp();
    }
    if (!tsfChordHasCtrlOrAlt() && tsfIsChineseModePunctuationVk(vk)) {
        return true;
    }
    if (!tsfChordHasCtrlOrAlt() && tsfIsChineseModeShiftNumberSymbolVk(vk)) {
        return true;
    }
    return false;
}

bool Tsf::keyUpWouldBeHandled(WPARAM wParam, LPARAM lParam) const {
    if (!textEditSinkContext_ || !engine_) {
        return false;
    }
    (void)lParam;
    return engine_->fcitxModifierHotkeyUsesFullKeyEvent(
        static_cast<unsigned>(wParam));
}

HRESULT Tsf::runKeyEditSession(TfEditCookie ec, WPARAM wp, LPARAM lp,
                               bool isRelease) {
    pendingKeyHandled_ = false;
    if (!engine_) {
        return S_OK;
    }
    const UINT vk = static_cast<UINT>(wp);
    const std::uint32_t rawKeyStateMask =
        engine_->usesHostKeyboardStateForRawKeyDelivery()
            ? tsfHostKeyboardStateMaskForPipe()
            : ImeEngine::kFcitxRawKeyUseProcessKeyboardState;
    const bool traceLatinO = (vk == 'O');
    if (traceLatinO) {
        tsfTrace(
            std::string("o-runKeyEditSession enter release=") +
            (isRelease ? "true" : "false") +
            " chineseActive=" + (chineseActive_ ? "true" : "false") +
            " ctrlAlt=" + (tsfChordHasCtrlOrAlt() ? "true" : "false") +
            " preedit=" + tsfWideToUtf8(engine_->preedit()) +
            " candidateCount=" + std::to_string(engine_->candidates().size()));
    }
    if (engine_->fcitxModifierHotkeyUsesFullKeyEvent(vk)) {
        pendingKeyHandled_ = engine_->deliverFcitxRawKeyEvent(
            vk, static_cast<std::uintptr_t>(lp), isRelease, rawKeyStateMask);
        if (pendingKeyHandled_) {
            afterFcitxEngineKey(ec);
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }
    if (isRelease) {
        // Non-modifier keys use KeyDown-only path today.
        return S_OK;
    }

    if (engine_->tryConsumeImManagerHotkey(vk,
                                           static_cast<std::uintptr_t>(lp))) {
        pendingKeyHandled_ = true;
        afterFcitxEngineKey(ec);
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (tsfChordHasCtrlOrAlt() && tsfCanAttemptRawImeKey(vk)) {
        pendingKeyHandled_ = engine_->deliverFcitxRawKeyEvent(
            vk, static_cast<std::uintptr_t>(lp), false, rawKeyStateMask);
        if (pendingKeyHandled_) {
            afterFcitxEngineKey(ec);
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (vk == VK_SPACE && (GetKeyState(VK_CONTROL) & 0x8000)) {
        pendingKeyHandled_ = true;
        chineseActive_ = !chineseActive_;
        if (!chineseActive_) {
            endCompositionCancel(ec);
        }
        syncCandidateWindow(ec);
        drainCommitsAfterEngine(ec);
        langBarNotifyIconUpdate();
        return S_OK;
    }

    if (!chineseActive_) {
        return S_OK;
    }

    if (vk == VK_ESCAPE) {
        if (tsfChordHasCtrlOrAlt()) {
            drainCommitsAfterEngine(ec);
            return S_OK;
        }
        pendingKeyHandled_ = true;
        endCompositionCancel(ec);
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (vk == VK_BACK) {
        if (tsfChordHasCtrlOrAlt()) {
            drainCommitsAfterEngine(ec);
            return S_OK;
        }
        if (!engine_->preedit().empty()) {
            if (engine_->backspace()) {
                pendingKeyHandled_ = true;
                if (engine_->preedit().empty()) {
                    endCompositionCancel(ec);
                } else {
                    ensureCompositionStarted(ec);
                    updatePreeditText(ec);
                    syncCandidateWindow(ec);
                }
            }
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    // 处理数字键选择候选词（仅在未按 Shift 时）
    if (!engine_->candidates().empty() && !tsfChordHasCtrlOrAlt() &&
        vk >= '0' && vk <= '9' && (GetKeyState(VK_SHIFT) & 0x8000) == 0) {
        const int idx = (vk == '0') ? 9 : (static_cast<int>(vk - '1'));
        if (idx >= 0 && engine_->hasCandidate(static_cast<size_t>(idx))) {
            pendingKeyHandled_ = true;
            if (engine_->tryForwardCandidateKey(vk)) {
                afterFcitxEngineKey(ec);
            } else {
                endCompositionCommit(
                    ec, engine_->candidateText(static_cast<size_t>(idx)));
            }
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (!engine_->candidates().empty() && !tsfChordHasCtrlOrAlt() &&
        (vk == VK_UP || vk == VK_DOWN)) {
        pendingKeyHandled_ = true;
        if (engine_->tryForwardCandidateKey(vk)) {
            afterFcitxEngineKey(ec);
        } else {
            if (vk == VK_UP) {
                engine_->moveHighlight(-1);
            } else {
                engine_->moveHighlight(1);
            }
            syncCandidateWindow(ec);
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (vk == VK_RETURN) {
        if (tsfChordHasCtrlOrAlt()) {
            drainCommitsAfterEngine(ec);
            return S_OK;
        }
        if (!engine_->candidates().empty()) {
            if (engine_->tryForwardCandidateKey(VK_RETURN)) {
                pendingKeyHandled_ = true;
                afterFcitxEngineKey(ec);
            } else {
                const auto t = engine_->highlightedCandidateText();
                if (!t.empty()) {
                    pendingKeyHandled_ = true;
                    endCompositionCommit(ec, t);
                }
            }
        } else if (!engine_->preedit().empty()) {
            if (engine_->tryForwardPreeditCommit()) {
                pendingKeyHandled_ = true;
                afterFcitxEngineKey(ec);
            } else {
                pendingKeyHandled_ = true;
                endCompositionCommit(ec, engine_->preedit());
            }
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (vk == VK_SPACE && !tsfChordHasCtrlOrAlt() &&
        !engine_->candidates().empty()) {
        if (engine_->tryForwardCandidateKey(VK_SPACE)) {
            pendingKeyHandled_ = true;
            afterFcitxEngineKey(ec);
        } else {
            const auto t = engine_->highlightedCandidateText();
            if (!t.empty()) {
                pendingKeyHandled_ = true;
                endCompositionCommit(ec, t);
            }
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (vk >= 'A' && vk <= 'Z') {
        if (tsfLatinKeyShouldPassToApp()) {
            if (traceLatinO) {
                tsfTrace("o-runKeyEditSession pass-to-app");
            }
            drainCommitsAfterEngine(ec);
            return S_OK;
        }
        // Table engines like Wubi need the real vk/lParam/modifier context;
        // the simplified appendLatin path can be accepted by pinyin but ignored
        // by fcitx-table.
        pendingKeyHandled_ = engine_->deliverFcitxRawKeyEvent(
            vk, static_cast<std::uintptr_t>(lp), false, rawKeyStateMask);
        if (pendingKeyHandled_) {
            afterFcitxEngineKey(ec);
        }
        if (traceLatinO) {
            tsfTrace(std::string("o-runKeyEditSession handled preedit=") +
                     tsfWideToUtf8(engine_->preedit()) + " candidateCount=" +
                     std::to_string(engine_->candidates().size()) + " top0=" +
                     (engine_->candidates().empty()
                          ? std::string()
                          : tsfWideToUtf8(engine_->candidateText(0))));
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (tsfIsChineseModePunctuationVk(vk)) {
        if (tsfChordHasCtrlOrAlt()) {
            drainCommitsAfterEngine(ec);
            return S_OK;
        }
        pendingKeyHandled_ = engine_->deliverFcitxRawKeyEvent(
            vk, static_cast<std::uintptr_t>(lp), false, rawKeyStateMask);
        if (pendingKeyHandled_) {
            afterFcitxEngineKey(ec);
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    // 处理 Shift+数字键 输入中文标点符号（如 Shift+1 输入 "！"）
    if (tsfIsChineseModeShiftNumberSymbolVk(vk)) {
        FCITX_INFO() << "Shift+number key: vk=" << vk
                     << " shift state=" << (GetKeyState(VK_SHIFT) & 0x8000);
        if (tsfChordHasCtrlOrAlt()) {
            drainCommitsAfterEngine(ec);
            return S_OK;
        }
        FCITX_INFO() << "Delivering Shift+number key to engine: vk=" << vk;
        pendingKeyHandled_ = engine_->deliverFcitxRawKeyEvent(
            vk, static_cast<std::uintptr_t>(lp), false, rawKeyStateMask);
        if (pendingKeyHandled_) {
            afterFcitxEngineKey(ec);
        } else {
            const wchar_t fallback = tsfShiftNumberFallbackSymbol(vk);
            if (fallback != L'\0') {
                pendingKeyHandled_ = true;
                endCompositionCommit(ec, std::wstring(1, fallback));
            }
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    drainCommitsAfterEngine(ec);
    return S_OK;
}

STDMETHODIMP Tsf::DoEditSession(TfEditCookie ec) {
    if (pendingTrayToggleChinese_) {
        pendingTrayToggleChinese_ = false;
        trayToggleChineseInEditSession(ec);
        return S_OK;
    }
    if (pendingTraySetChineseModeValid_) {
        const bool wantChinese = pendingTraySetChineseMode_;
        pendingTraySetChineseModeValid_ = false;
        traySetChineseModeInEditSession(ec, wantChinese);
        return S_OK;
    }
    if (!engine_) {
        return S_OK;
    }
    if (pendingTrayReloadPinyinConfig_) {
        pendingTrayReloadPinyinConfig_ = false;
        if (engine_->reloadPinyinConfig()) {
            clearSharedTrayPinyinReloadRequest();
            langBarNotifyIconUpdate();
        }
        return S_OK;
    }
    if (!pendingTrayStatusAction_.empty()) {
        const auto actionName = std::move(pendingTrayStatusAction_);
        pendingTrayStatusAction_.clear();
        const bool fromSharedRequest =
            pendingTrayStatusActionFromSharedRequest_;
        pendingTrayStatusActionFromSharedRequest_ = false;
        const bool activated = engine_->activateTrayStatusAction(actionName);
        if (fromSharedRequest && activated) {
            clearSharedTrayStatusActionRequest();
        }
        persistSharedTrayStatusActionState();
        langBarNotifyIconUpdate();
        return S_OK;
    }
    if (!pendingTrayInputMethod_.empty()) {
        const auto uniqueName = std::move(pendingTrayInputMethod_);
        pendingTrayInputMethod_.clear();
        const bool fromSharedRequest = pendingTrayInputMethodFromSharedRequest_;
        pendingTrayInputMethodFromSharedRequest_ = false;
        tsfTrace(
            "DoEditSession pending tray target=" + uniqueName +
            " fromShared=" + std::string(fromSharedRequest ? "true" : "false"));
        FCITX_INFO() << "DoEditSession pending tray input method target="
                     << uniqueName << " fromShared=" << fromSharedRequest
                     << " pid=" << GetCurrentProcessId();
        chineseActive_ = true;
        if (composition_) {
            endCompositionCancel(ec);
        } else {
            candidateWin_.hide();
            endCandidateListUiElement();
            engine_->clear();
        }
        const bool activated = engine_->activateProfileInputMethod(uniqueName);
        tsfTrace("DoEditSession activateProfileInputMethod result=" +
                 std::string(activated ? "true" : "false") +
                 " target=" + uniqueName);
        FCITX_INFO() << "DoEditSession activateProfileInputMethod result="
                     << activated << " target=" << uniqueName
                     << " pid=" << GetCurrentProcessId();
        if (fromSharedRequest && activated) {
            deferredSharedTrayInputMethod_.clear();
            clearSharedTrayInputMethodRequest();
            tsfTrace("DoEditSession cleared shared tray request target=" +
                     uniqueName);
            FCITX_INFO() << "DoEditSession cleared shared tray request target="
                         << uniqueName << " pid=" << GetCurrentProcessId();
        } else if (fromSharedRequest && !activated) {
            deferredSharedTrayInputMethod_ = uniqueName;
            tsfTrace("DoEditSession deferred shared tray request target=" +
                     uniqueName);
            FCITX_WARN() << "DoEditSession deferred shared tray request target="
                         << uniqueName << " pid=" << GetCurrentProcessId();
        }
        syncCandidateWindow(ec);
        drainCommitsAfterEngine(ec);
        langBarNotifyIconUpdate();
        return S_OK;
    }
    if (pendingMousePick_ >= 0) {
        const int idx = pendingMousePick_;
        pendingMousePick_ = -1;
        if (!composition_ || !engine_->hasCandidate(static_cast<size_t>(idx))) {
            return S_OK;
        }
        if (engine_->feedCandidatePick(static_cast<size_t>(idx))) {
            afterFcitxEngineKey(ec);
        } else {
            endCompositionCommit(
                ec, engine_->candidateText(static_cast<size_t>(idx)));
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }
    const bool isRelease = pendingKeyIsRelease_;
    pendingKeyIsRelease_ = false;
    pendingKeyHandled_ = false;
    return runKeyEditSession(ec, pendingKeyWParam_, pendingKeyLParam_,
                             isRelease);
}

} // namespace fcitx
