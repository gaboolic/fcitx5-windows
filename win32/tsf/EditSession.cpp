#include "CandidateListUiElement.h"
#include "tsf.h"

#include <fcitx-utils/log.h>
#include <cstdint>
#include <string>

namespace {

/// Ctrl/Alt chords are host shortcuts (e.g. Ctrl+Back); must not go to IME.
bool tsfChordHasCtrlOrAlt() {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
           (GetKeyState(VK_MENU) & 0x8000) != 0;
}

/// AltGr is LeftCtrl+RightAlt — not a Ctrl+Alt editor shortcut; Latin must reach IME.
bool tsfIsAltGrPhysicalDown() {
    return (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 &&
           (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
}

/// Latin A–Z: pass through on Ctrl/Alt/Shift chords.
/// - Shift: GetKeyState only — GetAsyncKeyState(VK_SHIFT) is unreliable in some TSF
///   stacks and can stay "down", which hid all pinyin.
/// - Ctrl/Alt: require GetKeyState AND GetAsyncKeyState both high so a stale
///   GetKeyState alone (e.g. after Ctrl+A) does not suppress IME.
bool tsfLatinKeyShouldPassToApp() {
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        return true;
    }
    if (tsfIsAltGrPhysicalDown()) {
        return false;
    }
    const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) &&
                          (GetAsyncKeyState(VK_CONTROL) & 0x8000);
    const bool altDown = (GetKeyState(VK_MENU) & 0x8000) &&
                         (GetAsyncKeyState(VK_MENU) & 0x8000);
    return ctrlDown || altDown;
}

/// Caret in client coords of hwndCaret → screen. Many hosts return empty
/// ITfContextView::GetTextExt; GUI thread caret matches the real insertion point.
/// US-keyboard punctuation keys: must reach fcitx (punctuation addon) instead of the host.
bool tsfIsChineseModePunctuationVk(unsigned vk) {
    switch (vk) {
    case VK_OEM_1:   // ; :
    case VK_OEM_2:   // / ?
    case VK_OEM_3:   // ` ~
    case VK_OEM_4:   // [ {
    case VK_OEM_5:   // \ |
    case VK_OEM_6:   // ] }
    case VK_OEM_7:   // ' "
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

bool tsfScreenPtFromCaretGuiThread(DWORD threadId, POINT *out) {
    if (!out || threadId == 0) {
        return false;
    }
    GUITHREADINFO gti = {};
    gti.cbSize = sizeof(GUITHREADINFO);
    if (!GetGUIThreadInfo(threadId, &gti) || !gti.hwndCaret) {
        return false;
    }
    out->x = gti.rcCaret.left;
    out->y = gti.rcCaret.bottom;
    return ClientToScreen(gti.hwndCaret, out) != FALSE;
}

} // namespace

namespace fcitx {

bool Tsf::queryCandidateAnchor(TfEditCookie ec, POINT *screenPt) {
    if (!textEditSinkContext_ || !screenPt) {
        return false;
    }
    ComPtr<ITfRange> range;
    if (compositionRange_) {
        range = compositionRange_;
    } else {
        ComPtr<ITfInsertAtSelection> ias;
        if (FAILED(textEditSinkContext_->QueryInterface(
                IID_ITfInsertAtSelection,
                reinterpret_cast<void **>(ias.ReleaseAndGetAddressOf())))) {
            return false;
        }
        if (FAILED(ias->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0,
                range.ReleaseAndGetAddressOf())) ||
            !range) {
            return false;
        }
    }
    ComPtr<IEnumTfContextViews> enumViews;
    if (FAILED(textEditSinkContext_->EnumViews(
            enumViews.ReleaseAndGetAddressOf())) ||
        !enumViews) {
        return false;
    }
    DWORD caretThread = 0;
    for (;;) {
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
        if (caretThread == 0) {
            caretThread = GetWindowThreadProcessId(w, nullptr);
        }
        RECT rc {};
        BOOL clipped = FALSE;
        const HRESULT hrExt =
            view->GetTextExt(ec, range.Get(), &rc, &clipped);
        const int rw = rc.right - rc.left;
        const int rh = rc.bottom - rc.top;
        if (SUCCEEDED(hrExt) && rw > 0 && rh > 0) {
            screenPt->x = rc.left;
            screenPt->y = rc.bottom;
            if (ClientToScreen(w, screenPt)) {
                return true;
            }
        }
    }
    if (caretThread != 0) {
        return tsfScreenPtFromCaretGuiThread(caretThread, screenPt);
    }
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

void Tsf::endCompositionCommit(TfEditCookie ec, const std::wstring &text) {
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
                inserted &&
                SUCCEEDED(inserted->Collapse(ec, TF_ANCHOR_END))) {
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
    if (engine_) {
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
        endCompositionCommit(ec, w);
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
    POINT pt = {100, 100};
    if (!queryCandidateAnchor(ec, &pt)) {
        bool anchored = false;
        const HWND tryAnchors[] = {GetForegroundWindow(), GetFocus()};
        for (HWND anchor : tryAnchors) {
            if (!anchor) {
                continue;
            }
            const DWORD tid = GetWindowThreadProcessId(anchor, nullptr);
            if (tid && tsfScreenPtFromCaretGuiThread(tid, &pt)) {
                anchored = true;
                break;
            }
        }
        if (!anchored && GetCaretPos(&pt)) {
            HWND focus = GetFocus();
            if (focus) {
                ClientToScreen(focus, &pt);
            }
        }
    }
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
    if (engine_->imManagerHotkeyWouldEat(
            vk, static_cast<std::uintptr_t>(lParam))) {
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
    const bool hasPanel = !engine_->preedit().empty() || !engine_->candidates().empty();
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
        ((vk >= '0' && vk <= '9') || vk == VK_UP || vk == VK_DOWN)) {
        return true;
    }
    if (vk >= 'A' && vk <= 'Z') {
        return !tsfLatinKeyShouldPassToApp();
    }
    if (!tsfChordHasCtrlOrAlt() && tsfIsChineseModePunctuationVk(vk)) {
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
    if (!engine_) {
        return S_OK;
    }
    const UINT vk = static_cast<UINT>(wp);
    if (engine_->fcitxModifierHotkeyUsesFullKeyEvent(vk)) {
        engine_->deliverFcitxRawKeyEvent(vk, static_cast<std::uintptr_t>(lp),
                                        isRelease);
        afterFcitxEngineKey(ec);
        drainCommitsAfterEngine(ec);
        return S_OK;
    }
    if (isRelease) {
        // Non-modifier keys use KeyDown-only path today.
        return S_OK;
    }

    if (engine_->tryConsumeImManagerHotkey(vk,
                                           static_cast<std::uintptr_t>(lp))) {
        afterFcitxEngineKey(ec);
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (vk == VK_SPACE && (GetKeyState(VK_CONTROL) & 0x8000)) {
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
            engine_->backspace();
            if (engine_->preedit().empty()) {
                endCompositionCancel(ec);
            } else {
                ensureCompositionStarted(ec);
                updatePreeditText(ec);
                syncCandidateWindow(ec);
            }
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (!engine_->candidates().empty() && !tsfChordHasCtrlOrAlt() &&
        vk >= '0' && vk <= '9') {
        const int idx = (vk == '0') ? 9 : (static_cast<int>(vk - '1'));
        if (idx >= 0 && engine_->hasCandidate(static_cast<size_t>(idx))) {
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
                afterFcitxEngineKey(ec);
            } else {
                const auto t = engine_->highlightedCandidateText();
                if (!t.empty()) {
                    endCompositionCommit(ec, t);
                }
            }
        } else if (!engine_->preedit().empty()) {
            if (engine_->tryForwardPreeditCommit()) {
                afterFcitxEngineKey(ec);
            } else {
                endCompositionCommit(ec, engine_->preedit());
            }
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (vk == VK_SPACE && !tsfChordHasCtrlOrAlt() &&
        !engine_->candidates().empty()) {
        if (engine_->tryForwardCandidateKey(VK_SPACE)) {
            afterFcitxEngineKey(ec);
        } else {
            const auto t = engine_->highlightedCandidateText();
            if (!t.empty()) {
                endCompositionCommit(ec, t);
            }
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (vk >= 'A' && vk <= 'Z') {
        if (tsfLatinKeyShouldPassToApp()) {
            drainCommitsAfterEngine(ec);
            return S_OK;
        }
        wchar_t ch = static_cast<wchar_t>(vk - L'A' + L'a');
        if (engine_->preedit().size() < 32) {
            engine_->appendLatinLowercase(ch);
            if (!ensureCompositionStarted(ec)) {
                drainCommitsAfterEngine(ec);
                return S_OK;
            }
            updatePreeditText(ec);
            syncCandidateWindow(ec);
        }
        drainCommitsAfterEngine(ec);
        return S_OK;
    }

    if (tsfIsChineseModePunctuationVk(vk)) {
        if (tsfChordHasCtrlOrAlt()) {
            drainCommitsAfterEngine(ec);
            return S_OK;
        }
        engine_->deliverFcitxRawKeyEvent(vk, static_cast<std::uintptr_t>(lp),
                                         false);
        afterFcitxEngineKey(ec);
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
    if (!engine_) {
        return S_OK;
    }
    if (!pendingTrayInputMethod_.empty()) {
        const auto uniqueName = std::move(pendingTrayInputMethod_);
        pendingTrayInputMethod_.clear();
        const bool fromSharedRequest = pendingTrayInputMethodFromSharedRequest_;
        pendingTrayInputMethodFromSharedRequest_ = false;
        tsfTrace("DoEditSession pending tray target=" + uniqueName +
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
            clearSharedTrayInputMethodRequest();
            tsfTrace("DoEditSession cleared shared tray request target=" + uniqueName);
            FCITX_INFO() << "DoEditSession cleared shared tray request target="
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
    return runKeyEditSession(ec, pendingKeyWParam_, pendingKeyLParam_, isRelease);
}

} // namespace fcitx
