/**
 *
 * @file llCefBrowser.h
 * @brief Per-browser CEF client/handler implementation backing one llCefBrowserManager handle.
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#pragma once
#include "include/cef_client.h"
#include "llCefBrowserHandle.h"
#include "llCefBrowserPixelBuffer.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class llCefBrowserManagerImpl;

// One CEF offscreen browser. Owned exclusively via CefRefPtr (never mix
// unique_ptr with IMPLEMENT_REFCOUNTING types) and referenced from the
// outside world only through its llCefBrowserHandle.
class llCefBrowser : public CefClient,
    public CefRenderHandler,
    public CefLifeSpanHandler,
    public CefRequestHandler,
    public CefDisplayHandler,
    public CefLoadHandler,
    public CefDialogHandler,
    public CefJSDialogHandler,
    public CefDownloadHandler {
    public:
        llCefBrowser(llCefBrowserHandle handle, int width, int height, llCefBrowserManagerImpl* manager);

        // Opaque per-browser scratch storage for whatever the app wants to find
        // again given just a handle - see llCefBrowserManager::SetUserData.
        // Plain field: written once from the app/GL thread at creation time,
        // read from the same thread during rendering, so no synchronization is
        // required in the intended single-threaded-message-loop usage. Not
        // owned by this class - nothing here frees it.
        void* userData = nullptr;

        // --- CefClient ---
        CefRefPtr<CefRenderHandler> GetRenderHandler() override {
            return this;
        }
        CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
            return this;
        }
        CefRefPtr<CefRequestHandler> GetRequestHandler() override {
            return this;
        }
        CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
            return this;
        }
        CefRefPtr<CefLoadHandler> GetLoadHandler() override {
            return this;
        }
        CefRefPtr<CefDialogHandler> GetDialogHandler() override {
            return this;
        }
        CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override {
            return this;
        }
        CefRefPtr<CefDownloadHandler> GetDownloadHandler() override {
            return this;
        }
        bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                      CefProcessId sourceProcess,
                                      CefRefPtr<CefProcessMessage> message) override;

        // --- CefRenderHandler ---
        void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
        bool GetScreenInfo(CefRefPtr<CefBrowser> browser, CefScreenInfo& screen_info) override;
        void OnPaint(CefRefPtr<CefBrowser> browser,
                     PaintElementType type,
                     const RectList& dirtyRects,
                     const void* buffer,
                     int width, int height) override;

        // Popup layer (<select> dropdowns, autocomplete, date pickers, etc.) -
        // CEF paints these as a separate PET_POPUP surface rather than folding
        // them into the main PET_VIEW frame. Composited directly into the same
        // BGRA32 buffer CopyLatestFrame() hands out, so from the outside a
        // browser with an open dropdown looks exactly like a normal windowed
        // one - no separate popup texture/rect in the public API.
        void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override;
        void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect) override;

        // --- CefDisplayHandler ---
        // See llCefBrowserManager::SetOnAddressChangeCallback.
        void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                             const CefString& url) override;

        // See llCefBrowserManager::SetOnCursorChangedCallback. Return value:
        // false means "CEF may still apply its own default cursor handling" -
        // this callback is purely an observer here, so always false.
        bool OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor,
                            cef_cursor_type_t type, const CefCursorInfo& customCursorInfo) override;

        // See llCefBrowserManager::SetOnTitleChangeCallback.
        void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;

        // See llCefBrowserManager::SetOnStatusMessageCallback.
        void OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value) override;

        // See llCefBrowserManager::SetOnConsoleMessageCallback. Return value:
        // false means "let the message continue to CEF's own console/log
        // output too" - this observer never suppresses it.
        bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level,
                              const CefString& message, const CefString& source, int line) override;

        // See llCefBrowserManager::SetOnTooltipCallback. Return value: false
        // means "let CEF display the tooltip itself" - this observer never
        // suppresses or edits it.
        bool OnTooltip(CefRefPtr<CefBrowser> browser, CefString& text) override;

        // --- CefLoadHandler ---
        // See llCefBrowserManager::SetOnLoadStartCallback. Filtered to the
        // main frame only - see the header comment on that method.
        void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         TransitionType transitionType) override;

        // See llCefBrowserManager::SetOnLoadEndCallback. Main frame only.
        void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       int httpStatusCode) override;

        // See llCefBrowserManager::SetOnLoadErrorCallback. Main frame only.
        void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         ErrorCode errorCode, const CefString& errorText,
                         const CefString& failedUrl) override;

        // --- CefDialogHandler ---
        // See llCefBrowserManager::SetOnFileDialogCallback. Return value: true
        // means "we're handling this ourselves" (always the case once an app
        // callback is registered - see RespondToFileDialog); false lets CEF
        // show its own default native dialog instead.
        bool OnFileDialog(CefRefPtr<CefBrowser> browser, FileDialogMode mode,
                          const CefString& title, const CefString& defaultFilePath,
                          const std::vector<CefString>& acceptFilters,
                          const std::vector<CefString>& acceptExtensions,
                          const std::vector<CefString>& acceptDescriptions,
                          CefRefPtr<CefFileDialogCallback> callback) override;

        // --- CefDownloadHandler ---
        // A download's default handling (with no CefDownloadHandler at all) is to
        // silently cancel it for an Alloy-style browser -- windowless/OSR apps always
        // are Alloy-style, so without this override every download (e.g. a page's
        // <a download> blob-click) would just vanish with no callback and no error.
        // Instead, always route it through the exact same OnFileDialog/
        // SetOnFileDialogCallback flow used for genuine file dialogs (matching
        // Dullahan's own approach, a known-working reference for this): asking CEF to
        // show its file-dialog flow for this download, in FILE_DIALOG_SAVE mode, is
        // what makes CEF call OnFileDialog just below.
        bool OnBeforeDownload(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> downloadItem,
                              const CefString& suggestedName,
                              CefRefPtr<CefBeforeDownloadCallback> callback) override;

        // --- CefJSDialogHandler ---
        // See llCefBrowserManager::SetOnJSDialogCallback. Unlike OnFileDialog,
        // this never takes over rendering a custom dialog - always returns
        // false (use CEF's own default dialog implementation), with
        // suppressMessage as the only lever the app gets to pull.
        bool OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString& originUrl,
                        JSDialogType dialogType, const CefString& messageText,
                        const CefString& defaultPromptText, CefRefPtr<CefJSDialogCallback> callback,
                        bool& suppressMessage) override;

        // See llCefBrowserManager::SetOnBeforeUnloadCallback.
        bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser> browser, const CefString& messageText,
                                  bool isReload, CefRefPtr<CefJSDialogCallback> callback) override;

        // --- CefLifeSpanHandler ---
        void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
        void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

        // See llCefBrowserManager::SetOnOpenPopupCallback. Always returns
        // true (cancel) - this library never lets CEF create the popup
        // browser itself, since a windowless/OSR host has nowhere sensible
        // to put one.
        bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                           int popup_id, const CefString& target_url,
                           const CefString& target_frame_name,
                           CefLifeSpanHandler::WindowOpenDisposition target_disposition, bool user_gesture,
                           const CefPopupFeatures& popupFeatures, CefWindowInfo& windowInfo,
                           CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
                           CefRefPtr<CefDictionaryValue>& extra_info,
                           bool* no_javascript_access) override;

        // --- CefRequestHandler ---
        // Lets the message router cancel any pending JS queries for a frame
        // that's navigating away, and - see
        // llCefBrowserManager::SetOnCustomSchemeURLCallback - intercepts
        // navigation to a small, hardcoded set of recognized non-http(s) URL
        // schemes (e.g. "secondlife://"), firing that callback and returning
        // true (stop following the link) instead of letting CEF attempt to
        // load it.
        bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefRequest> request, bool userGesture,
                            bool isRedirect) override;

        // See llCefBrowserManager::SetOnAuthRequestCallback. Unlike every
        // other override in this class, CEF calls this one on its own IO
        // thread rather than the UI thread - resolved synchronously, inline,
        // using only mOnAuthRequest and the callback already in hand, with no
        // reach-back into llCefBrowserManagerImpl (whose state is only ever
        // touched from the UI thread).
        bool GetAuthCredentials(CefRefPtr<CefBrowser> browser, const CefString& originUrl,
                                bool isProxy, const CefString& host, int port,
                                const CefString& realm, const CefString& scheme,
                                CefRefPtr<CefAuthCallback> callback) override;

        llCefBrowserHandle GetHandle() const {
            return mHandle;
        }

        void SetSize(int w, int h, bool clearImmediately = false);

        // Requests a new compositor frame right now, rather than waiting for CEF's own
        // windowless_frame_rate-driven internal timer to eventually get around to one --
        // see llCefBrowserManagerImpl::CreateBrowser()'s external_begin_frame_enabled
        // setting, which this requires. A no-op if the browser hasn't been created yet.
        void SendExternalBeginFrame();

        int GetWidth() const {
            return mWidth;
        }
        int GetHeight() const {
            return mHeight;
        }

        // Closes the browser now if it already exists, otherwise remembers the
        // request so OnAfterCreated closes it the moment CEF hands over the
        // CefBrowser instance. Without this, destroying a browser during its
        // async creation window silently drops the close and leaks a live,
        // orphaned CEF browser that nothing ever tells to shut down.
        void RequestClose();

        // Call periodically (e.g. once per frame, for every live browser) to
        // recover from CEF occasionally never delivering the follow-up repaint
        // for a requested resize - observed happening to roughly one browser
        // out of several resized on the same tick. If the size last requested
        // via SetSize hasn't been confirmed by a matching-size OnPaint within
        // the timeout, this re-issues the resize request.
        void CheckResizeWatchdog();

        // Pulls the newest frame if one has arrived since the last call.
        bool CopyLatestFrame(std::vector<uint8_t>& dst, int& w, int& h) {
            return mPixels.CopyLatest(dst, w, h);
        }

        // Navigation transport controls - see llCefBrowserManager's methods
        // of the same name. All false/no-op if the underlying CefBrowser
        // doesn't exist yet (async creation still in flight).
        bool CanGoBack() const {
            return mCefBrowser && mCefBrowser->CanGoBack();
        }
        void GoBack() {
            if (mCefBrowser) {
                mCefBrowser->GoBack();
            }
        }
        bool CanGoForward() const {
            return mCefBrowser && mCefBrowser->CanGoForward();
        }
        void GoForward() {
            if (mCefBrowser) {
                mCefBrowser->GoForward();
            }
        }
        bool IsLoading() const {
            return mCefBrowser && mCefBrowser->IsLoading();
        }

        void Reload(bool ignoreCache) {
            if (! mCefBrowser) {
                return;
            }
            if (ignoreCache) {
                mCefBrowser->ReloadIgnoreCache();
            }
            else {
                mCefBrowser->Reload();
            }
        }

        void StopLoad() {
            if (mCefBrowser) {
                mCefBrowser->StopLoad();
            }
        }

        void Navigate(const std::string& url) {
            if (! mCefBrowser) {
                return;
            }
            CefRefPtr<CefFrame> frame = mCefBrowser->GetMainFrame();
            if (frame) {
                frame->LoadURL(url);
            }
        }

        // Edit commands - see llCefBrowserManager's methods of the same name.
        // Applied to the currently focused frame rather than the main frame,
        // since that's where an editable field actually is if it lives
        // inside an iframe.
        void Undo() {
            if (CefRefPtr<CefFrame> f = FocusedFrame()) {
                f->Undo();
            }
        }
        void Redo() {
            if (CefRefPtr<CefFrame> f = FocusedFrame()) {
                f->Redo();
            }
        }
        void Copy() {
            if (CefRefPtr<CefFrame> f = FocusedFrame()) {
                f->Copy();
            }
        }
        void Cut() {
            if (CefRefPtr<CefFrame> f = FocusedFrame()) {
                f->Cut();
            }
        }
        void Paste() {
            if (CefRefPtr<CefFrame> f = FocusedFrame()) {
                f->Paste();
            }
        }
        void Delete() {
            if (CefRefPtr<CefFrame> f = FocusedFrame()) {
                f->Delete();
            }
        }
        void SelectAll() {
            if (CefRefPtr<CefFrame> f = FocusedFrame()) {
                f->SelectAll();
            }
        }

        // See llCefBrowserManager::SetPageZoom.
        void SetPageZoom(float zoomLevel) {
            mRequestedPageZoom = std::clamp(zoomLevel, 0.25f, 4.0f);
            ApplyPageZoom();
        }

        // See llCefBrowserManager::SetOnPageChangedCallback.
        void SetOnPageChangedCallback(std::function<void()> callback) {
            mOnPageChanged = std::move(callback);
        }

        // See llCefBrowserManager::SetOnCursorChangedCallback.
        void SetOnCursorChangedCallback(std::function<void(llCefCursorType)> callback) {
            mOnCursorChanged = std::move(callback);
        }

        // See llCefBrowserManager::SetOnAddressChangeCallback.
        void SetOnAddressChangeCallback(std::function<void(const std::string&)> callback) {
            mOnAddressChange = std::move(callback);
        }

        // See llCefBrowserManager::SetOnTitleChangeCallback.
        void SetOnTitleChangeCallback(std::function<void(const std::string&)> callback) {
            mOnTitleChange = std::move(callback);
        }

        // See llCefBrowserManager::SetOnStatusMessageCallback.
        void SetOnStatusMessageCallback(std::function<void(const std::string&)> callback) {
            mOnStatusMessage = std::move(callback);
        }

        // See llCefBrowserManager::SetOnConsoleMessageCallback.
        void SetOnConsoleMessageCallback(std::function<void(const std::string&, const std::string&, int)> callback) {
            mOnConsoleMessage = std::move(callback);
        }

        // See llCefBrowserManager::SetOnTooltipCallback.
        void SetOnTooltipCallback(std::function<void(const std::string&)> callback) {
            mOnTooltip = std::move(callback);
        }

        // See llCefBrowserManager::SetOnLoadStartCallback.
        void SetOnLoadStartCallback(std::function<void()> callback) {
            mOnLoadStart = std::move(callback);
        }

        // See llCefBrowserManager::SetOnLoadEndCallback.
        void SetOnLoadEndCallback(std::function<void(int)> callback) {
            mOnLoadEnd = std::move(callback);
        }

        // See llCefBrowserManager::SetOnLoadErrorCallback.
        void SetOnLoadErrorCallback(std::function<void(int, const std::string&, const std::string&)> callback) {
            mOnLoadError = std::move(callback);
        }

        // See llCefBrowserManager::SetOnQueryCallback.
        void SetOnQueryCallback(std::function<void(const std::string&)> callback) {
            mOnQuery = std::move(callback);
        }

        // Called by llCefBrowserLib's JS bridge router (JsBridgeRouterHandler::
        // OnQuery in llCefBrowserLib.cpp) when a window.cefQuery(...) request
        // arrives for this browser - public, unlike the rest of this class's
        // notify points, because it's fired from outside llCefBrowser itself
        // rather than from one of this class's own CEF handler overrides.
        void NotifyOnQuery(const std::string& request) {
            if (mOnQuery) {
                mOnQuery(request);
            }
        }

        // See llCefBrowserManager::SetOnFileDialogCallback.
        void SetOnFileDialogCallback(std::function<void(int64_t, llCefFileDialogMode, const std::string&, const std::string&, const std::vector<std::string>&)> callback) {
            mOnFileDialog = std::move(callback);
        }

        // See llCefBrowserManager::RespondToFileDialog.
        void RespondToFileDialog(int64_t dialogId, const std::vector<std::string>& filePaths);

        // See llCefBrowserManager::SetOnAuthRequestCallback. Locked, unlike
        // every other setter here - GetAuthCredentials reads mOnAuthRequest
        // from CEF's IO thread while this can be called from the UI thread
        // at any time, an unsynchronized std::function read/write race
        // every other callback (UI-thread-only on both ends) doesn't have.
        void SetOnAuthRequestCallback(std::function<bool(const std::string&, const std::string&, int, const std::string&, const std::string&, bool, std::string&, std::string&)> callback) {
            std::lock_guard<std::mutex> lock(mOnAuthRequestMutex);
            mOnAuthRequest = std::move(callback);
        }

        // See llCefBrowserManager::SetOnCustomSchemeURLCallback.
        void SetOnCustomSchemeURLCallback(std::function<void(const std::string&, bool, bool)> callback) {
            mOnCustomSchemeURL = std::move(callback);
        }

        // See llCefBrowserManager::SetOnOpenPopupCallback.
        void SetOnOpenPopupCallback(std::function<void(const std::string&, const std::string&)> callback) {
            mOnOpenPopup = std::move(callback);
        }

        // See llCefBrowserManager::SetOnPageSourceRetrievedCallback.
        void SetOnPageSourceRetrievedCallback(std::function<void(const std::string&)> callback, size_t maxBytes) {
            mOnPageSourceRetrieved = std::move(callback);
            mMaxSourceBytes = maxBytes;
        }

        // See llCefBrowserManager::SetOnJSDialogCallback.
        void SetOnJSDialogCallback(std::function<bool(const std::string&, llCefJSDialogType, const std::string&, const std::string&)> callback) {
            mOnJSDialog = std::move(callback);
        }

        // See llCefBrowserManager::SetOnBeforeUnloadCallback.
        void SetOnBeforeUnloadCallback(std::function<bool(const std::string&, bool)> callback) {
            mOnBeforeUnload = std::move(callback);
        }

        // No-ops if the underlying CefBrowser doesn't exist yet (async creation
        // still in flight) - same "silently drop until ready" convention as the
        // rest of this class's setters.
        void SendMouseClickEvent(int x, int y, llCefMouseButton button, bool mouseUp, int clickCount);
        void SendMouseMoveEvent(int x, int y, bool mouseLeave);
        void SendMouseWheelEvent(int x, int y, int deltaY);

        // See llCefBrowserManager::SendKeyEvent - implemented for Windows only
        // (WM_KEYDOWN/WM_KEYUP/WM_CHAR + WM_SYS* equivalents); a no-op elsewhere.
        void SendKeyEvent(uint32_t message, uint64_t wParam, int64_t lParam);

        void SetFocus(bool focus);

        void ShowDevTools();
        void ExecuteJavaScript(const std::string& code);
        // On-demand, uncapped page source fetch -- unlike mOnPageSourceRetrieved (which
        // fires automatically on every main-frame load, truncated to mMaxSourceBytes),
        // this fetches the full current source once, whenever the caller actually wants
        // it. Fire-and-forget: does nothing if there's no main frame right now.
        void GetPageSource(std::function<void(const std::string&)> callback);

    private:
        llCefBrowserHandle mHandle;
        llCefBrowserManagerImpl* mManager;  // non-owning; outlives every llCefBrowser
        CefRefPtr<CefBrowser> mCefBrowser;
        llCefBrowserPixelBuffer mPixels;
        int mWidth;
        int mHeight;
        bool mCloseRequested = false;

        // Tracks a resize we're still waiting to see confirmed by a
        // matching-size OnPaint. mPendingWidth < 0 means nothing pending.
        int mPendingWidth = -1;
        int mPendingHeight = -1;
        std::chrono::steady_clock::time_point mResizeRequestedAt;

        // Bitmask of CEF EVENTFLAG_*_MOUSE_BUTTON values for buttons currently
        // held down, updated by SendMouseClickEvent and stamped onto every
        // subsequent SendMouseMoveEvent's CefMouseEvent::modifiers. Without
        // this, CEF sees move events with no button reported as down and never
        // recognizes a drag (scrollbar thumb, text selection, slider) - it just
        // sees an ordinary hover move.
        uint32_t mHeldMouseButtons = 0;

        // Last full PET_VIEW frame, kept independently of mPixels - mPixels is
        // a pull-based producer/consumer handoff with no way to peek at what
        // was last written, but compositing the popup back on top after every
        // subsequent PET_VIEW repaint (which replaces the whole frame CEF-side)
        // needs a plain, always-current copy of the raw view content to source
        // from.
        std::vector<uint8_t> mViewBuffer;
        int mViewBufferWidth = 0;
        int mViewBufferHeight = 0;

        // Last PET_POPUP frame and where it belongs, in view coordinates.
        // mPopupX/mPopupY come from OnPopupSize (clamped so the popup's bitmap
        // still lands fully inside the view); mPopupWidth/mPopupHeight are
        // captured from the actual bitmap OnPaint(PET_POPUP) delivers.
        bool mPopupVisible = false;
        std::vector<uint8_t> mPopupBuffer;
        int mPopupX = 0;
        int mPopupY = 0;
        int mPopupWidth = 0;
        int mPopupHeight = 0;

        // Composites mPopupBuffer onto a copy of mViewBuffer at
        // (mPopupX, mPopupY) and writes the result to mPixels. Called after
        // whichever of the two buffers just changed, as long as the popup is
        // currently shown.
        void CompositePopup();

        // Null if the underlying CefBrowser doesn't exist yet or there's no
        // currently focused frame - used by the edit commands above.
        CefRefPtr<CefFrame> FocusedFrame() const {
            return mCefBrowser ? mCefBrowser->GetFocusedFrame() : nullptr;
        }

        // Requested zoom, in plain scale-factor units (see SetPageZoom) -
        // kept so it can be reapplied after every navigation, since CEF
        // itself resets zoom on each new page load.
        float mRequestedPageZoom = 1.0f;

        // Converts mRequestedPageZoom to CEF's own logarithmic zoom level and
        // applies it via CefBrowserHost::SetZoomLevel - a no-op if the
        // underlying CefBrowser doesn't exist yet, or if it's already at the
        // requested level. Called both from SetPageZoom() (an explicit
        // request) and OnLoadStart() (CEF's per-navigation reset, undone).
        void ApplyPageZoom();

        // See llCefBrowserManager::SetOnPageChangedCallback. Fired by
        // WriteFrame() - the single point every OnPaint/CompositePopup path
        // funnels through to reach mPixels - so every new-frame source
        // notifies the same way without duplicating the "fire it" logic at
        // each call site.
        std::function<void()> mOnPageChanged;
        void WriteFrame(const void* buffer, int w, int h);

        std::function<void(llCefCursorType)> mOnCursorChanged;
        std::function<void(const std::string&)> mOnAddressChange;
        std::function<void(const std::string&)> mOnTitleChange;
        std::function<void(const std::string&)> mOnStatusMessage;
        std::function<void(const std::string&, const std::string&, int)> mOnConsoleMessage;
        std::function<void(const std::string&)> mOnTooltip;
        std::function<void()> mOnLoadStart;
        std::function<void(int)> mOnLoadEnd;
        std::function<void(int, const std::string&, const std::string&)> mOnLoadError;
        std::function<void(const std::string&)> mOnQuery;

        std::function<void(int64_t, llCefFileDialogMode, const std::string&, const std::string&, const std::vector<std::string>&)> mOnFileDialog;

        // Pending file dialog requests awaiting a RespondToFileDialog() call,
        // keyed by an id generated in OnFileDialog - mirrors the JS bridge's
        // gPendingQueries/RespondToQuery pattern in llCefBrowserLib.cpp, kept
        // here instead since this callback is per-browser rather than
        // process-wide.
        std::unordered_map<int64_t, CefRefPtr<CefFileDialogCallback>> mPendingFileDialogs;
        int64_t mNextFileDialogId = 1;

        // Invoked synchronously from GetAuthCredentials, on CEF's IO thread -
        // unlike every other std::function member here, which are only ever
        // invoked from the UI thread. mOnAuthRequestMutex guards read/write
        // of mOnAuthRequest itself against that cross-thread access; it does
        // not (and cannot, without risking deadlock) hold across the actual
        // app callback invocation.
        std::mutex mOnAuthRequestMutex;
        std::function<bool(const std::string&, const std::string&, int, const std::string&, const std::string&, bool, std::string&, std::string&)> mOnAuthRequest;

        std::function<void(const std::string&, bool, bool)> mOnCustomSchemeURL;
        std::function<void(const std::string&, const std::string&)> mOnOpenPopup;

        std::function<void(const std::string&)> mOnPageSourceRetrieved;
        size_t mMaxSourceBytes = 2048;

        std::function<bool(const std::string&, llCefJSDialogType, const std::string&, const std::string&)> mOnJSDialog;
        std::function<bool(const std::string&, bool)> mOnBeforeUnload;

        IMPLEMENT_REFCOUNTING(llCefBrowser);
        DISALLOW_COPY_AND_ASSIGN(llCefBrowser);
};
