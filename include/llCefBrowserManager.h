/**
 *
 * @file llCefBrowserManager.h
 * @brief Public API for creating, controlling, and destroying offscreen browser instances via opaque handles.
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
#include "llCefBrowserHandle.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Public API. Every method here is safe to call from an application that
// has never included a single CEF header - all CEF types and includes are
// confined to this library's own .cpp files via the pimpl pattern below.
class llCefBrowserManager {
    public:
        // cachePath: shared cache directory used by every browser this manager
        // creates. Pass an empty string for an in-memory-only, non-persistent
        // shared context.
        explicit llCefBrowserManager(const std::string& cachePath);
        ~llCefBrowserManager();

        llCefBrowserManager(const llCefBrowserManager&) = delete;
        llCefBrowserManager& operator=(const llCefBrowserManager&) = delete;

        // Must be called on the CEF UI thread (the main thread, in
        // single-threaded-message-loop mode, alongside llCefBrowserLib::
        // DoMessageLoopWork()). Returns a handle immediately; the real browser
        // is created asynchronously underneath.
        llCefBrowserHandle CreateBrowser(const std::string& url, int width, int height);

        // Requests destruction. Safe to call rapidly and repeatedly.
        void DestroyBrowser(llCefBrowserHandle handle);

        // Destroys the most recently created still-active browser. Returns
        // false if there are no active browsers left.
        bool DestroyLastBrowser();

        // Destroys every currently-live browser. Call before
        // llCefBrowserLib::Shutdown().
        void DestroyAll();

        bool IsValid(llCefBrowserHandle handle) const;

        // Opaque per-browser scratch storage, not read or written by this
        // library itself - a place to stash whatever the caller needs to find
        // again given just a handle (a GL/graphics-API texture id, a pointer to
        // your own per-browser struct, etc.), instead of maintaining a separate
        // side table keyed by handle. Not owned: if it points to allocated
        // memory, freeing it is the caller's responsibility, including before
        // the browser is destroyed if that would otherwise leak it. Starts as
        // nullptr for every newly created handle, including a recycled slot.
        void SetUserData(llCefBrowserHandle handle, void* userData);
        void* GetUserData(llCefBrowserHandle handle);

        // The cache directory this manager was constructed with, exactly as
        // passed to the constructor above (empty for in-memory-only mode).
        // Useful for diagnostics/UI (e.g. an About box) where the original
        // value isn't in scope anymore.
        const std::string& GetCachePath() const;

        // The size last requested via CreateBrowser/ResizeBrowser - not
        // necessarily what's currently rendered; see CopyLatestFrame.
        int GetWidth(llCefBrowserHandle handle) const;
        int GetHeight(llCefBrowserHandle handle) const;

        // Requests a new size for an existing browser.
        //
        // clearImmediately: false (default) leaves the old frame in place
        // until a real repaint at the new size arrives - avoids a black flash,
        // at the cost of possibly showing old-size content briefly if your
        // texture-upload code doesn't reallocate storage on a size mismatch.
        // true forces an immediate blank frame at the new size instead.
        void ResizeBrowser(llCefBrowserHandle handle, int width, int height, bool clearImmediately = false);

        // Pulls the newest rendered frame (tightly-packed BGRA32) if one has
        // arrived since the last successful call. Returns false (dst
        // untouched) otherwise.
        bool CopyLatestFrame(llCefBrowserHandle handle, std::vector<uint8_t>& dst, int& w, int& h);

        // Navigation transport controls, mirroring CefBrowser's own
        // CanGoBack/GoBack/CanGoForward/GoForward/IsLoading/Reload/
        // ReloadIgnoreCache/StopLoad. All are no-ops (false/no-op) if the
        // underlying CefBrowser doesn't exist yet (async creation still in
        // flight) or the handle is invalid.
        bool CanGoBack(llCefBrowserHandle handle) const;
        void GoBack(llCefBrowserHandle handle);
        bool CanGoForward(llCefBrowserHandle handle) const;
        void GoForward(llCefBrowserHandle handle);
        bool IsLoading(llCefBrowserHandle handle) const;

        // ignoreCache: false reloads normally (may serve cached resources);
        // true bypasses the cache entirely, like a hard refresh.
        void Reload(llCefBrowserHandle handle, bool ignoreCache);

        void StopLoad(llCefBrowserHandle handle);

        // Navigates the main frame to a new URL, as if the user had typed it
        // into an address bar. A no-op under the same conditions as the
        // transport controls above.
        void Navigate(llCefBrowserHandle handle, const std::string& url);

        // Edit commands, mirroring CefFrame's Undo/Redo/Cut/Copy/Paste/Delete/
        // SelectAll - applied to the browser's currently focused frame (not
        // always the main frame; matters when an editable field lives inside
        // an iframe). No-ops if the underlying CefBrowser doesn't exist yet
        // or there's no focused frame.
        //
        // The CanX() checks always return true: CEF exposes no actual
        // capability query for any of these (Dullahan's own editCan*()
        // methods do the same, for the same reason) - they're fire-and-forget
        // commands the renderer applies to whatever's focused, so e.g. Copy
        // with nothing selected or Paste with an empty clipboard is already a
        // safe no-op on CEF's side regardless of what CanCopy()/CanPaste()
        // would report.
        bool CanUndo(llCefBrowserHandle handle) const;
        void Undo(llCefBrowserHandle handle);
        bool CanRedo(llCefBrowserHandle handle) const;
        void Redo(llCefBrowserHandle handle);
        bool CanCopy(llCefBrowserHandle handle) const;
        void Copy(llCefBrowserHandle handle);
        bool CanCut(llCefBrowserHandle handle) const;
        void Cut(llCefBrowserHandle handle);
        bool CanPaste(llCefBrowserHandle handle) const;
        void Paste(llCefBrowserHandle handle);
        bool CanDelete(llCefBrowserHandle handle) const;
        void Delete(llCefBrowserHandle handle);
        bool CanSelectAll(llCefBrowserHandle handle) const;
        void SelectAll(llCefBrowserHandle handle);

        // zoomLevel is a plain scale factor, not CEF's own logarithmic "zoom
        // level" units - 1.0 is 100% (default), 0.25 is 25%, 4.0 is 400%;
        // clamped to that [0.25, 4.0] range.
        //
        // CEF resets zoom on every navigation, so this is remembered and
        // silently reapplied after every subsequent page load for this
        // browser (right as it starts loading) rather than needing to be set
        // again yourself each time.
        void SetPageZoom(llCefBrowserHandle handle, float zoomLevel);

        // Cookies belong to the shared cookie store used by every browser
        // this manager creates (see the constructor's cachePath) rather than
        // to any individual browser, so these don't take a handle.

        // Sets a cookie on the shared store. Fire-and-forget by default;
        // callback (optional) reports whether CEF actually accepted it - it
        // can fail for a malformed URL or disallowed characters (e.g. ';' in
        // the value). Always fires exactly once if provided, even when CEF
        // rejects the request synchronously.
        void SetCookie(const std::string& url, const std::string& name, const std::string& value,
                       const std::string& domain, const std::string& path, bool httpOnly, bool secure,
                       std::function<void(bool success)> callback = nullptr);

        // Fetches every cookie in the shared store. Asynchronous - CEF's own
        // underlying API (VisitAllCookies) has no synchronous form. callback
        // fires once with the complete list, ordered by longest path then
        // earliest creation date (CEF's own documented order). One known
        // limitation inherited from CEF: if the store is reachable but
        // genuinely has zero cookies, CEF may never invoke the visitor at
        // all, in which case this callback won't fire either.
        void GetCookies(std::function<void(const std::vector<llCefCookie>& cookies)> callback);

        // Deletes every cookie in the shared store. Fire-and-forget by
        // default; callback (optional) reports how many were actually
        // deleted. Always fires exactly once if provided, even when CEF
        // rejects the request synchronously.
        void DeleteAllCookies(std::function<void(int numDeleted)> callback = nullptr);

        // Fired once whenever a new frame (view or popup) has just been
        // written and is ready for CopyLatestFrame() - a signal only, no pixel
        // data attached, so registering this doesn't cost you an extra copy of
        // the frame on top of what CopyLatestFrame() already gives you. Lets
        // an application move from polling CopyLatestFrame() every tick to
        // pulling it only when there's actually something new. Overwrites any
        // previously registered callback for this handle; pass nullptr to
        // unregister. Invoked on the CEF UI thread, same as
        // llCefBrowserLib::DoMessageLoopWork().
        void SetOnPageChangedCallback(llCefBrowserHandle handle, std::function<void()> callback);

        // Fired whenever CEF changes the cursor shape the page wants shown
        // (e.g. hovering a link switches to Hand, a text field to IBeam).
        // Overwrites any previously registered callback for this handle; pass
        // nullptr to unregister.
        void SetOnCursorChangedCallback(llCefBrowserHandle handle, std::function<void(llCefCursorType)> callback);

        // Fired whenever the main frame's URL changes (navigation, redirect,
        // pushState/replaceState, etc.) - not for sub-frame/iframe navigation.
        // Overwrites any previously registered callback for this handle; pass
        // nullptr to unregister.
        void SetOnAddressChangeCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback);

        // Fired when the main frame's page title changes.
        void SetOnTitleChangeCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback);

        // Fired when CEF wants to show a status-bar-style message (e.g. a
        // hovered link's target URL).
        void SetOnStatusMessageCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback);

        // Fired for every console.log/warn/error call from page JS. Always
        // lets the message continue to CEF's own console/log output as well -
        // this is an additional observer, not a replacement.
        void SetOnConsoleMessageCallback(llCefBrowserHandle handle, std::function<void(const std::string& message, const std::string& source, int line)> callback);

        // Fired when the page is about to show a tooltip. Always lets CEF
        // display the tooltip itself - this is notification only, it can't
        // suppress or change the text.
        void SetOnTooltipCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback);

        // Fired once the main frame has committed a navigation and starts
        // loading - not for sub-frame/iframe loads, and not for same-page
        // navigations (fragments, history state, etc.).
        void SetOnLoadStartCallback(llCefBrowserHandle handle, std::function<void()> callback);

        // Fired once the main frame finishes loading, with the HTTP status
        // code. Not for sub-frame/iframe loads.
        void SetOnLoadEndCallback(llCefBrowserHandle handle, std::function<void(int httpStatusCode)> callback);

        // Fired when the main frame's navigation fails or is canceled.
        // errorCode is CEF/Chromium's raw net error code (see
        // net/base/net_error_list.h in Chromium's source for the meaning of
        // specific values) - exposed as a plain int rather than a full
        // CEF-free mirror of every net error, since most callers only care
        // about a handful of them. Not for sub-frame/iframe failures.
        void SetOnLoadErrorCallback(llCefBrowserHandle handle, std::function<void(int errorCode, const std::string& errorText, const std::string& failedUrl)> callback);

        // Fired whenever page JS calls window.cefQuery(...), with the request
        // string as payload. Independent of llCefBrowserJavaScriptBridge/
        // SetJavaScriptBridge entirely - fires regardless of whether a bridge
        // is registered, or whether it ends up handling the request. Useful
        // for simple observation/logging without implementing a full bridge.
        void SetOnQueryCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback);

        // Called when the page wants to show a file chooser (<input
        // type=file>, a save dialog via the File System Access API, etc.).
        // Registering this callback tells CEF "we'll show our own UI" for
        // every file dialog this browser requests; leave it unregistered to
        // let CEF display its own default native dialog instead.
        // acceptFilters are MIME types and/or file extensions (e.g.
        // "image/*", ".png") the page wants to restrict selection to.
        //
        // This one is asynchronous rather than signal-only: once the user has
        // picked file(s) or canceled, call RespondToFileDialog with the same
        // dialogId - inline before returning from this callback (the common
        // case), or later, e.g. once your own async dialog UI closes.
        void SetOnFileDialogCallback(llCefBrowserHandle handle, std::function<void(int64_t dialogId, llCefFileDialogMode mode, const std::string& title, const std::string& defaultFilePath, const std::vector<std::string>& acceptFilters)> callback);

        // Completes a pending file dialog request from
        // SetOnFileDialogCallback. Pass an empty filePaths to indicate the
        // user canceled. A no-op if dialogId is unknown (e.g. already
        // responded to).
        void RespondToFileDialog(llCefBrowserHandle handle, int64_t dialogId, const std::vector<std::string>& filePaths);

        // Called when a server (or proxy) requests HTTP authentication
        // (Basic/Digest/NTLM), rather than CEF showing its own native
        // credentials dialog. Registering this callback tells CEF "we'll
        // supply credentials ourselves" for every auth request this browser
        // encounters; leave it unregistered to let CEF display its own
        // default dialog instead. realm/scheme describe what the server is
        // asking for (e.g. scheme "basic", realm "My Realm") - typically
        // shown to the user in a credentials prompt alongside host:port.
        //
        // Unlike every other callback in this API, CEF invokes this one on
        // its own IO thread rather than the UI thread - and it must be
        // answered synchronously, inline, right here: set username/password
        // and return true to proceed, or return false to cancel. There is no
        // deferred/respond-later option (unlike SetOnFileDialogCallback),
        // because resolving this later from the UI thread would mean
        // reaching back into this browser's state from the IO thread, which
        // nothing else in this API is built to do safely.
        void SetOnAuthRequestCallback(llCefBrowserHandle handle, std::function<bool(const std::string& originUrl, const std::string& host, int port, const std::string& realm, const std::string& scheme, bool isProxy, std::string& username, std::string& password)> callback);

        // Fired when navigation targets a URL whose scheme is one of a
        // small, fixed set this library recognizes as "custom" (e.g.
        // "secondlife://") - that set is hardcoded in llCefBrowser.cpp
        // rather than exposed here, since it rarely changes. When it fires,
        // CEF is told to stop following the link (userGesture/isRedirect
        // describe how the navigation was triggered) - the app is expected
        // to handle the URL itself.
        void SetOnCustomSchemeURLCallback(llCefBrowserHandle handle, std::function<void(const std::string& url, bool userGesture, bool isRedirect)> callback);

        // Fired once per main-frame load with the page's HTML source,
        // truncated to at most maxBytes bytes. Retrieved via
        // CefFrame::GetSource() right after the main frame's OnLoadEnd fires
        // - see SetOnLoadEndCallback, which fires first for the same load.
        // Overwrites any previously registered callback (and maxBytes) for
        // this handle; pass nullptr to unregister.
        void SetOnPageSourceRetrievedCallback(llCefBrowserHandle handle, std::function<void(const std::string& source)> callback, size_t maxBytes = 2048);

        // Fired when the page calls alert()/confirm()/prompt(). Return true
        // to suppress the dialog entirely - CEF shows nothing, useful for
        // detecting/blocking spammy repeated dialogs (e.g. an alert() loop in
        // onbeforeunload) - or false to let CEF display its own default
        // dialog. Unlike SetOnFileDialogCallback, this library never takes
        // over rendering a custom dialog itself; the only choice offered is
        // suppress-or-default, matching this callback's own documented
        // purpose in CEF.
        void SetOnJSDialogCallback(llCefBrowserHandle handle, std::function<bool(const std::string& originUrl, llCefJSDialogType dialogType, const std::string& messageText, const std::string& defaultPromptText)> callback);

        // Fired when the page is about to navigate away or reload with
        // unsaved changes (the "Leave site? Changes you made may not be
        // saved" prompt). Return true to suppress that dialog and proceed
        // with the navigation/reload automatically; return false to let CEF
        // display its own default dialog.
        void SetOnBeforeUnloadCallback(llCefBrowserHandle handle, std::function<bool(const std::string& messageText, bool isReload)> callback);

        // x/y are in browser view pixels (the same space as width/height passed
        // to CreateBrowser/ResizeBrowser), origin top-left. mouseUp: false for
        // button-down, true for button-up. clickCount lets multi-click (e.g.
        // double-click) selection work on the page.
        void SendMouseClickEvent(llCefBrowserHandle handle, int x, int y, llCefMouseButton button, bool mouseUp, int clickCount = 1);

        // x/y are in browser view pixels, same space as SendMouseClickEvent.
        // Pass mouseLeave = true once when the cursor exits the browser's view
        // so CEF can clear hover state; x/y are ignored by CEF in that case.
        void SendMouseMoveEvent(llCefBrowserHandle handle, int x, int y, bool mouseLeave = false);

        // x/y are in browser view pixels, same space as SendMouseClickEvent.
        // deltaY is in the same units as CEF's own wheel delta (a multiple of
        // ~30-120 per notch is typical, positive = scroll up/content moves down)
        // - scale your platform's raw wheel offset up before passing it in.
        void SendMouseWheelEvent(llCefBrowserHandle handle, int x, int y, int deltaY);

        // Windows-only for now (no-op on other platforms): translates a native
        // Win32 keyboard message into the corresponding CEF key event and
        // forwards it to the browser. Pass message/wParam/lParam straight from
        // your WndProc or SetWindowSubclass callback - this covers
        // WM_KEYDOWN/WM_KEYUP/WM_CHAR and their WM_SYS* equivalents.
        void SendKeyEvent(llCefBrowserHandle handle, uint32_t message, uint64_t wParam, int64_t lParam);

        // Tells CEF whether this browser has keyboard focus. Call with true
        // when your window/control gains focus and false when it loses it -
        // CEF uses this to drive things like caret blink and focus-dependent
        // page JS (focus/blur events), independent of whether you're also
        // sending it key events.
        void SetFocus(llCefBrowserHandle handle, bool focus);

        // Opens the CEF DevTools in their own separate, ordinarily-rendered
        // native window (not routed through this browser's offscreen texture).
        // A no-op if the browser doesn't exist yet or DevTools are already open
        // for it.
        void ShowDevTools(llCefBrowserHandle handle);

        // Runs script in the browser's main frame. Fire-and-forget - there is
        // no return value; use the existing JS-bridge (window.cefQuery, see
        // llCefBrowserJavaScriptBridge) if the page needs to report a result
        // back.
        void ExecuteJavaScript(llCefBrowserHandle handle, const std::string& code);

        size_t LiveBrowserCount() const;

        // Invokes fn(handle) once for every currently active browser. Use the
        // handle with the accessors above - the public API never hands out a
        // reference to any CEF-facing type.
        void ForEachBrowser(const std::function<void(llCefBrowserHandle)>& fn);

        // Call once per frame, alongside llCefBrowserLib::DoMessageLoopWork().
        // Detects and recovers browsers whose most recent resize request never
        // got a confirming repaint from CEF within a timeout.
        void Tick();

    private:
        class Impl;
        std::unique_ptr<Impl> mImpl;
};
