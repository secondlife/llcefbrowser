/**
 *
 * @file llCefBrowser.cpp
 * @brief Implementation of llCefBrowser's CEF client/handler callbacks for a single offscreen browser instance.
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

#include "llCefBrowser.h"
#include "llCefBrowserManagerImpl.h"
#include "llCefBrowserMessageRouterAccess.h"
#include "include/cef_parser.h"
#include "include/cef_string_visitor.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(WIN32)
#include <windows.h>
#endif

namespace {

    // Plain CefTask instead of a closure helper - the closure-wrapping APIs
    // (base::Bind/BindOnce/OnceClosure) have shifted shape across CEF releases,
    // while the raw CefTask interface has stayed stable for a long time.
    class InvalidateTask : public CefTask {
        public:
            explicit InvalidateTask(CefRefPtr<CefBrowser> browser) : mBrowser(browser) {}

            void Execute() override {
                if (mBrowser && mBrowser->GetHost()) {
                    mBrowser->GetHost()->Invalidate(PET_VIEW);
                }
            }

        private:
            CefRefPtr<CefBrowser> mBrowser;
            IMPLEMENT_REFCOUNTING(InvalidateTask);
    };

}  // namespace

llCefBrowser::llCefBrowser(llCefBrowserHandle handle, int width, int height, llCefBrowserManagerImpl* manager)
    // Clamped to a minimum of 1: a 0/negative size here would flow straight
    // into mPixels' w*h*4 byte-count calculation and underflow to a huge
    // allocation.
    : mHandle(handle), mManager(manager), mWidth(std::max(width, 1)), mHeight(std::max(height, 1))
{
    mPixels.Resize(mWidth, mHeight);
}

void llCefBrowser::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect)
{
    CEF_REQUIRE_UI_THREAD();
    rect = CefRect(0, 0, mWidth > 0 ? mWidth : 1, mHeight > 0 ? mHeight : 1);
}

void llCefBrowser::OnPaint(CefRefPtr<CefBrowser> browser,
                           PaintElementType type,
                           const RectList& dirtyRects,
                           const void* buffer,
                           int width, int height)
{
    CEF_REQUIRE_UI_THREAD();
    if (type == PET_POPUP)
    {
        const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        const uint8_t* src = static_cast<const uint8_t*>(buffer);
        mPopupBuffer.assign(src, src + bytes);
        mPopupWidth = width;
        mPopupHeight = height;

        if (mPopupVisible && ! mViewBuffer.empty())
        {
            CompositePopup();
        }
        return;
    }

    // PET_VIEW: buffer is always the full current frame at width x height,
    // regardless of what dirtyRects reports changed.
    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    const uint8_t* src = static_cast<const uint8_t*>(buffer);
    mViewBuffer.assign(src, src + bytes);
    mViewBufferWidth = width;
    mViewBufferHeight = height;

    if (mPopupVisible && ! mPopupBuffer.empty())
    {
        CompositePopup();
    }
    else
    {
        WriteFrame(buffer, width, height);
    }

    if (width == mPendingWidth && height == mPendingHeight)
    {
        // The resize we were waiting on has been confirmed - stop the
        // watchdog from retrying it.
        mPendingWidth = -1;
        mPendingHeight = -1;
    }
}

void llCefBrowser::OnPopupShow(CefRefPtr<CefBrowser> browser, bool show)
{
    CEF_REQUIRE_UI_THREAD();
    mPopupVisible = show;
    if (! show)
    {
        // The next PET_VIEW repaint (which CEF triggers itself once the
        // popup closes) will naturally show a popup-free frame via the
        // direct WriteFrame() path above, now that mPopupVisible is false
        // - nothing further to do here.
        mPopupBuffer.clear();
        mPopupWidth = 0;
        mPopupHeight = 0;
    }
}

void llCefBrowser::OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect)
{
    CEF_REQUIRE_UI_THREAD();
    if (rect.width <= 0 || rect.height <= 0)
    {
        return;
    }

    // CEF reports where the popup would ideally sit, not necessarily a
    // position that keeps it fully inside the view - clamp the origin so
    // compositing later never writes past the view buffer's edge.
    int x = rect.x;
    int y = rect.y;
    if (x < 0)
    {
        x = 0;
    }
    if (y < 0)
    {
        y = 0;
    }
    if (x + rect.width > mViewBufferWidth)
    {
        x = mViewBufferWidth - rect.width;
    }
    if (y + rect.height > mViewBufferHeight)
    {
        y = mViewBufferHeight - rect.height;
    }
    if (x < 0)
    {
        x = 0;
    }
    if (y < 0)
    {
        y = 0;
    }

    mPopupX = x;
    mPopupY = y;
}

void llCefBrowser::CompositePopup()
{
    std::vector<uint8_t> composited = mViewBuffer;

    // Clip against the view bounds in case mPopupX/mPopupY (from the last
    // OnPopupSize) and mPopupWidth/mPopupHeight (from the last
    // OnPaint(PET_POPUP)) ever disagree with each other or with the
    // current view size - e.g. a resize landing between the two calls.
    const int copyWidth = std::min(mPopupWidth, mViewBufferWidth - mPopupX);
    const int copyHeight = std::min(mPopupHeight, mViewBufferHeight - mPopupY);

    if (copyWidth > 0 && copyHeight > 0 &&
            static_cast<size_t>(mPopupWidth) * mPopupHeight * 4 <= mPopupBuffer.size())
    {
        for (int row = 0; row < copyHeight; ++row)
        {
            const uint8_t* src = mPopupBuffer.data() + static_cast<size_t>(row) * mPopupWidth * 4;
            uint8_t* dst = composited.data() +
                           (static_cast<size_t>(mPopupY + row) * mViewBufferWidth + mPopupX) * 4;
            std::memcpy(dst, src, static_cast<size_t>(copyWidth) * 4);
        }
    }

    WriteFrame(composited.data(), mViewBufferWidth, mViewBufferHeight);
}

void llCefBrowser::WriteFrame(const void* buffer, int w, int h)
{
    mPixels.Write(buffer, w, h);
    if (mOnPageChanged)
    {
        mOnPageChanged();
    }
}

bool llCefBrowser::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
        CefProcessId sourceProcess,
        CefRefPtr<CefProcessMessage> message)
{
    CEF_REQUIRE_UI_THREAD();
    CefRefPtr<CefMessageRouterBrowserSide> router = LLGetSharedMessageRouter();
    if (router && router->OnProcessMessageReceived(browser, frame, sourceProcess, message))
    {
        return true;
    }
    return false;
}

namespace {

    // Hardcoded rather than exposed via the public API - this set rarely
    // changes, so there's no need for callers to be able to register their
    // own schemes.
    bool IsCustomScheme(const std::string& url)
    {
        static const char* kCustomSchemes[] = { "secondlife" };

        CefURLParts parts;
        if (! CefParseURL(url, parts))
        {
            return false;
        }

        const std::string scheme = CefString(&parts.scheme).ToString();
        for (const char* candidate : kCustomSchemes)
        {
            if (scheme == candidate)
            {
                return true;
            }
        }
        return false;
    }

}  // namespace

bool llCefBrowser::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefRequest> request, bool userGesture,
                                  bool isRedirect)
{
    CEF_REQUIRE_UI_THREAD();
    CefRefPtr<CefMessageRouterBrowserSide> router = LLGetSharedMessageRouter();
    if (router)
    {
        router->OnBeforeBrowse(browser, frame);
    }

    const std::string url = request->GetURL().ToString();
    if (IsCustomScheme(url))
    {
        if (mOnCustomSchemeURL)
        {
            mOnCustomSchemeURL(url, userGesture, isRedirect);
        }
        return true;  // stop CEF from following this link
    }

    return false;  // don't block navigation
}

bool llCefBrowser::GetAuthCredentials(CefRefPtr<CefBrowser> browser, const CefString& originUrl,
                                      bool isProxy, const CefString& host, int port,
                                      const CefString& realm, const CefString& scheme,
                                      CefRefPtr<CefAuthCallback> callback)
{
    // Unlike every other override in this class, CEF really does call
    // this one on its own IO thread rather than the UI thread - this
    // assert (matching Dullahan's own GetAuthCredentials, a known-working
    // reference for this exact callback) makes that explicit rather than
    // silently assuming it.
    CEF_REQUIRE_IO_THREAD();

    // Copy under the lock rather than holding it across the call below -
    // mOnAuthRequest can be reassigned from the UI thread at any time (see
    // SetOnAuthRequestCallback), and the app's own callback body is
    // arbitrary code that shouldn't run while this lock is held.
    std::function<bool(const std::string&, const std::string&, int, const std::string&, const std::string&, bool, std::string&, std::string&)> onAuthRequest;
    {
        std::lock_guard<std::mutex> lock(mOnAuthRequestMutex);
        onAuthRequest = mOnAuthRequest;
    }

    if (! onAuthRequest)
    {
        callback->Cancel();
        return false;  // no app handler registered - let CEF show its own dialog
    }

    std::string username;
    std::string password;
    const bool proceed = onAuthRequest(originUrl.ToString(), host.ToString(), port,
                                       realm.ToString(), scheme.ToString(), isProxy,
                                       username, password);
    if (proceed)
    {
        callback->Continue(username, password);
        return true;
    }

    callback->Cancel();
    return false;
}

void llCefBrowser::OnAfterCreated(CefRefPtr<CefBrowser> browser)
{
    CEF_REQUIRE_UI_THREAD();
    if (mCefBrowser)
    {
        // Not our own browser -- ShowDevTools() reuses this object as the
        // CefClient for its DevTools popup, so CEF calls this a second time
        // for that browser too. The first call (mCefBrowser still null) is
        // always the real one; anything after that must be foreign, and
        // must never touch mCefBrowser/mCloseRequested bookkeeping.
        return;
    }
    mCefBrowser = browser;
    if (mCloseRequested)
    {
        // A destroy request arrived while creation was still in flight;
        // honor it now instead of leaving this browser running forever.
        mCefBrowser->GetHost()->CloseBrowser(true);
    }
}

void llCefBrowser::RequestClose()
{
    if (mCefBrowser)
    {
        mCefBrowser->GetHost()->CloseBrowser(true);
    }
    else
    {
        mCloseRequested = true;
    }
}

void llCefBrowser::OnBeforeClose(CefRefPtr<CefBrowser> browser)
{
    CEF_REQUIRE_UI_THREAD();
    if (! mCefBrowser || ! browser->IsSame(mCefBrowser))
    {
        // Not our own browser closing (the DevTools popup, or a stray
        // notification after our own browser already closed) -- ignore.
        // Letting this fall through to NotifyBrowserClosed() below would
        // recycle this handle's slot while the real browser is still alive,
        // orphaning it permanently.
        return;
    }
    if (CefRefPtr<CefMessageRouterBrowserSide> router = LLGetSharedMessageRouter())
    {
        router->OnBeforeClose(browser);
    }
    mCefBrowser = nullptr;
    // Phase 2 of destruction: CEF guarantees no further callbacks into this
    // object after OnBeforeClose returns, so it is now safe to recycle the
    // slot and let the CefRefPtr drop to zero.
    mManager->NotifyBrowserClosed(mHandle);
}

namespace {

    CefBrowserHost::MouseButtonType ToCefMouseButton(llCefMouseButton button)
    {
        switch (button)
        {
            case llCefMouseButton::Middle:
                return MBT_MIDDLE;
            case llCefMouseButton::Right:
                return MBT_RIGHT;
            case llCefMouseButton::Left:
            default:
                return MBT_LEFT;
        }
    }

    uint32_t ToCefMouseButtonFlag(llCefMouseButton button)
    {
        switch (button)
        {
            case llCefMouseButton::Middle:
                return EVENTFLAG_MIDDLE_MOUSE_BUTTON;
            case llCefMouseButton::Right:
                return EVENTFLAG_RIGHT_MOUSE_BUTTON;
            case llCefMouseButton::Left:
            default:
                return EVENTFLAG_LEFT_MOUSE_BUTTON;
        }
    }

    // CEF -> ours, the opposite direction from the two mouse-button helpers
    // above: CEF hands us its enum, we translate to the CEF-free public type.
    // Anything not explicitly listed (a newer/older CEF version's addition, or
    // a genuinely custom cursor image) falls through to Custom rather than
    // failing to compile against a different cef_cursor_type_t.
    llCefCursorType ToCursorType(cef_cursor_type_t type)
    {
        switch (type)
        {
            case CT_POINTER:
                return llCefCursorType::Pointer;
            case CT_CROSS:
                return llCefCursorType::Cross;
            case CT_HAND:
                return llCefCursorType::Hand;
            case CT_IBEAM:
                return llCefCursorType::IBeam;
            case CT_WAIT:
                return llCefCursorType::Wait;
            case CT_HELP:
                return llCefCursorType::Help;
            case CT_EASTRESIZE:
                return llCefCursorType::EastResize;
            case CT_NORTHRESIZE:
                return llCefCursorType::NorthResize;
            case CT_NORTHEASTRESIZE:
                return llCefCursorType::NorthEastResize;
            case CT_NORTHWESTRESIZE:
                return llCefCursorType::NorthWestResize;
            case CT_SOUTHRESIZE:
                return llCefCursorType::SouthResize;
            case CT_SOUTHEASTRESIZE:
                return llCefCursorType::SouthEastResize;
            case CT_SOUTHWESTRESIZE:
                return llCefCursorType::SouthWestResize;
            case CT_WESTRESIZE:
                return llCefCursorType::WestResize;
            case CT_NORTHSOUTHRESIZE:
                return llCefCursorType::NorthSouthResize;
            case CT_EASTWESTRESIZE:
                return llCefCursorType::EastWestResize;
            case CT_NORTHEASTSOUTHWESTRESIZE:
                return llCefCursorType::NorthEastSouthWestResize;
            case CT_NORTHWESTSOUTHEASTRESIZE:
                return llCefCursorType::NorthWestSouthEastResize;
            case CT_COLUMNRESIZE:
                return llCefCursorType::ColumnResize;
            case CT_ROWRESIZE:
                return llCefCursorType::RowResize;
            case CT_MIDDLEPANNING:
                return llCefCursorType::MiddlePanning;
            case CT_EASTPANNING:
                return llCefCursorType::EastPanning;
            case CT_NORTHPANNING:
                return llCefCursorType::NorthPanning;
            case CT_NORTHEASTPANNING:
                return llCefCursorType::NorthEastPanning;
            case CT_NORTHWESTPANNING:
                return llCefCursorType::NorthWestPanning;
            case CT_SOUTHPANNING:
                return llCefCursorType::SouthPanning;
            case CT_SOUTHEASTPANNING:
                return llCefCursorType::SouthEastPanning;
            case CT_SOUTHWESTPANNING:
                return llCefCursorType::SouthWestPanning;
            case CT_WESTPANNING:
                return llCefCursorType::WestPanning;
            case CT_MOVE:
                return llCefCursorType::Move;
            case CT_VERTICALTEXT:
                return llCefCursorType::VerticalText;
            case CT_CELL:
                return llCefCursorType::Cell;
            case CT_CONTEXTMENU:
                return llCefCursorType::ContextMenu;
            case CT_ALIAS:
                return llCefCursorType::Alias;
            case CT_PROGRESS:
                return llCefCursorType::Progress;
            case CT_NODROP:
                return llCefCursorType::NoDrop;
            case CT_COPY:
                return llCefCursorType::Copy;
            case CT_NONE:
                return llCefCursorType::None;
            case CT_NOTALLOWED:
                return llCefCursorType::NotAllowed;
            case CT_ZOOMIN:
                return llCefCursorType::ZoomIn;
            case CT_ZOOMOUT:
                return llCefCursorType::ZoomOut;
            case CT_GRAB:
                return llCefCursorType::Grab;
            case CT_GRABBING:
                return llCefCursorType::Grabbing;
            default:
                return llCefCursorType::Custom;
        }
    }

    llCefFileDialogMode ToFileDialogMode(cef_file_dialog_mode_t mode)
    {
        switch (mode)
        {
            case FILE_DIALOG_OPEN_MULTIPLE:
                return llCefFileDialogMode::OpenMultiple;
            case FILE_DIALOG_OPEN_FOLDER:
                return llCefFileDialogMode::OpenFolder;
            case FILE_DIALOG_SAVE:
                return llCefFileDialogMode::Save;
            case FILE_DIALOG_OPEN:
            default:
                return llCefFileDialogMode::Open;
        }
    }

    std::vector<std::string> ToStringVector(const std::vector<CefString>& in)
    {
        std::vector<std::string> out;
        out.reserve(in.size());
        for (const auto& s : in)
        {
            out.push_back(s.ToString());
        }
        return out;
    }

    llCefJSDialogType ToJSDialogType(cef_jsdialog_type_t type)
    {
        switch (type)
        {
            case JSDIALOGTYPE_CONFIRM:
                return llCefJSDialogType::Confirm;
            case JSDIALOGTYPE_PROMPT:
                return llCefJSDialogType::Prompt;
            case JSDIALOGTYPE_ALERT:
            default:
                return llCefJSDialogType::Alert;
        }
    }

}  // namespace

bool llCefBrowser::OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor,
                                  cef_cursor_type_t type, const CefCursorInfo& customCursorInfo)
{
    CEF_REQUIRE_UI_THREAD();
    if (mOnCursorChanged)
    {
        mOnCursorChanged(ToCursorType(type));
    }
    return false;
}

void llCefBrowser::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                   const CefString& url)
{
    CEF_REQUIRE_UI_THREAD();
    if (! frame || ! frame->IsMain())
    {
        return;    // ignore iframe/subframe navigation
    }
    if (mOnAddressChange)
    {
        mOnAddressChange(url.ToString());
    }
}

void llCefBrowser::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title)
{
    CEF_REQUIRE_UI_THREAD();
    if (mOnTitleChange)
    {
        mOnTitleChange(title.ToString());
    }
}

void llCefBrowser::OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value)
{
    CEF_REQUIRE_UI_THREAD();
    if (mOnStatusMessage)
    {
        mOnStatusMessage(value.ToString());
    }
}

bool llCefBrowser::OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level,
                                    const CefString& message, const CefString& source, int line)
{
    CEF_REQUIRE_UI_THREAD();
    if (mOnConsoleMessage)
    {
        mOnConsoleMessage(message.ToString(), source.ToString(), line);
    }
    return false;  // still let it reach CEF's own console/log output
}

bool llCefBrowser::OnTooltip(CefRefPtr<CefBrowser> browser, CefString& text)
{
    CEF_REQUIRE_UI_THREAD();
    if (mOnTooltip)
    {
        mOnTooltip(text.ToString());
    }
    return false;  // still let CEF display the tooltip itself
}

void llCefBrowser::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                               TransitionType transitionType)
{
    CEF_REQUIRE_UI_THREAD();
    if (! frame || ! frame->IsMain())
    {
        return;    // ignore iframe/subframe loads
    }

    // CEF resets zoom on every navigation - undo that by reapplying
    // whatever was last requested via SetPageZoom() (1.0/100% by default,
    // a no-op) right as the new page starts loading.
    ApplyPageZoom();

    if (mOnLoadStart)
    {
        mOnLoadStart();
    }
}

void llCefBrowser::ApplyPageZoom()
{
    if (! mCefBrowser)
    {
        return;
    }

    if (mRequestedPageZoom == 1.0f)
    {
        // Reset to CEF's own default exactly, rather than relying on the
        // formula below to land on precisely 0.0 despite floating-point
        // rounding.
        if (mCefBrowser->GetHost()->GetZoomLevel() != 0.0)
        {
            mCefBrowser->GetHost()->SetZoomLevel(0.0);
        }
        return;
    }

    // CEF/Chromium's zoom level is logarithmic, not a direct percentage -
    // this is the standard conversion (same one Dullahan's own
    // requestPageZoom() uses) mapping a 1.0-centered scale factor onto
    // it, empirically tuned so 1.0 lands on zoom level 0.0.
    const double cefZoomLevel = 5.46149645 * std::log(mRequestedPageZoom * 100.0) - 25.1511206;
    if (std::fabs(mCefBrowser->GetHost()->GetZoomLevel() - cefZoomLevel) > 0.001)
    {
        mCefBrowser->GetHost()->SetZoomLevel(cefZoomLevel);
    }
}

namespace {

    // Captures the app's callback (and byte limit) by value rather than
    // reaching back into llCefBrowser when Visit() fires - GetSource() is
    // asynchronous, so this outlives the OnLoadEnd call that created it, and
    // stays valid even if the browser itself is torn down in the meantime.
    class PageSourceVisitor : public CefStringVisitor {
        public:
            PageSourceVisitor(std::function<void(const std::string&)> callback, size_t maxBytes)
                : mCallback(std::move(callback)), mMaxBytes(maxBytes) {}

            void Visit(const CefString& string) override {
                CEF_REQUIRE_UI_THREAD();
                std::string source = string.ToString();
                if (source.size() > mMaxBytes) {
                    source.resize(mMaxBytes);
                }
                mCallback(source);
            }

        private:
            std::function<void(const std::string&)> mCallback;
            size_t mMaxBytes;
            IMPLEMENT_REFCOUNTING(PageSourceVisitor);
    };

}  // namespace

void llCefBrowser::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                             int httpStatusCode)
{
    CEF_REQUIRE_UI_THREAD();
    if (! frame || ! frame->IsMain())
    {
        return;    // ignore iframe/subframe loads
    }
    if (mOnLoadEnd)
    {
        mOnLoadEnd(httpStatusCode);
    }

    if (mOnPageSourceRetrieved)
    {
        frame->GetSource(new PageSourceVisitor(mOnPageSourceRetrieved, mMaxSourceBytes));
    }
}

void llCefBrowser::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                               ErrorCode errorCode, const CefString& errorText,
                               const CefString& failedUrl)
{
    CEF_REQUIRE_UI_THREAD();
    if (! frame || ! frame->IsMain())
    {
        return;    // ignore iframe/subframe failures
    }

    // ERR_ABORTED fires here for every canceled navigation, not just
    // genuine failures - including the exact case where OnBeforeBrowse
    // itself returned true (e.g. a recognized custom scheme URL). Per
    // CefRequestHandler::OnBeforeBrowse's own docs: "If the navigation is
    // canceled CefLoadHandler::OnLoadError will be called with an
    // errorCode value of ERR_ABORTED" - expected, not an error worth
    // surfacing, and standard practice to filter out in every CEF client.
    if (errorCode == ERR_ABORTED)
    {
        return;
    }

    if (mOnLoadError)
    {
        mOnLoadError(static_cast<int>(errorCode), errorText.ToString(), failedUrl.ToString());
    }
}

bool llCefBrowser::OnFileDialog(CefRefPtr<CefBrowser> browser, FileDialogMode mode,
                                const CefString& title, const CefString& defaultFilePath,
                                const std::vector<CefString>& acceptFilters,
                                const std::vector<CefString>& acceptExtensions,
                                const std::vector<CefString>& acceptDescriptions,
                                CefRefPtr<CefFileDialogCallback> callback)
{
    CEF_REQUIRE_UI_THREAD();
    if (! mOnFileDialog)
    {
        return false;    // no app handler registered - let CEF show its own dialog
    }

    // Registered before invoking the app's callback, same reasoning as the
    // JS bridge's pending-query map: an app that responds inline/
    // synchronously (call RespondToFileDialog before returning from its
    // callback) needs this entry to already exist to find anything.
    const int64_t dialogId = mNextFileDialogId++;
    mPendingFileDialogs[dialogId] = callback;

    mOnFileDialog(dialogId, ToFileDialogMode(mode), title.ToString(),
                  defaultFilePath.ToString(), ToStringVector(acceptFilters));
    return true;  // we're handling this ourselves
}

void llCefBrowser::RespondToFileDialog(int64_t dialogId, const std::vector<std::string>& filePaths)
{
    auto it = mPendingFileDialogs.find(dialogId);
    if (it == mPendingFileDialogs.end())
    {
        return;    // unknown, or already responded to
    }

    CefRefPtr<CefFileDialogCallback> callback = it->second;
    mPendingFileDialogs.erase(it);

    if (filePaths.empty())
    {
        callback->Cancel();
        return;
    }

    std::vector<CefString> paths;
    paths.reserve(filePaths.size());
    for (const auto& p : filePaths)
    {
        paths.push_back(p);
    }
    callback->Continue(paths);
}

bool llCefBrowser::OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString& originUrl,
                              JSDialogType dialogType, const CefString& messageText,
                              const CefString& defaultPromptText, CefRefPtr<CefJSDialogCallback> callback,
                              bool& suppressMessage)
{
    CEF_REQUIRE_UI_THREAD();
    if (! mOnJSDialog)
    {
        return false;    // no app handler registered - let CEF show its own dialog
    }

    suppressMessage = mOnJSDialog(originUrl.ToString(), ToJSDialogType(dialogType),
                                  messageText.ToString(), defaultPromptText.ToString());
    return false;  // never take over rendering ourselves - only veto via suppressMessage
}

bool llCefBrowser::OnBeforeUnloadDialog(CefRefPtr<CefBrowser> browser, const CefString& messageText,
                                        bool isReload, CefRefPtr<CefJSDialogCallback> callback)
{
    CEF_REQUIRE_UI_THREAD();
    if (! mOnBeforeUnload)
    {
        return false;    // no app handler registered - let CEF show its own dialog
    }

    if (mOnBeforeUnload(messageText.ToString(), isReload))
    {
        callback->Continue(true, CefString());
        return true;  // handled - auto-proceed with the unload/reload, no dialog shown
    }
    return false;  // let CEF show its own default dialog
}

void llCefBrowser::SendMouseClickEvent(int x, int y, llCefMouseButton button, bool mouseUp, int clickCount)
{
    if (! mCefBrowser)
    {
        return;
    }

    const uint32_t buttonFlag = ToCefMouseButtonFlag(button);
    if (mouseUp)
    {
        mHeldMouseButtons &= ~buttonFlag;
    }
    else
    {
        mHeldMouseButtons |= buttonFlag;
    }

    CefMouseEvent event;
    event.x = x;
    event.y = y;
    event.modifiers = mHeldMouseButtons;
    mCefBrowser->GetHost()->SendMouseClickEvent(event, ToCefMouseButton(button), mouseUp, clickCount);
}

void llCefBrowser::SendMouseMoveEvent(int x, int y, bool mouseLeave)
{
    if (! mCefBrowser)
    {
        return;
    }

    // Stamping the currently-held button(s) onto every move event is what
    // lets CEF recognize a drag (scrollbar thumb, text selection, slider) -
    // without it, each move looks like an ordinary hover with nothing
    // pressed, no matter what SendMouseClickEvent last reported.
    CefMouseEvent event;
    event.x = x;
    event.y = y;
    event.modifiers = mHeldMouseButtons;
    mCefBrowser->GetHost()->SendMouseMoveEvent(event, mouseLeave);
}

void llCefBrowser::SendMouseWheelEvent(int x, int y, int deltaY)
{
    if (! mCefBrowser)
    {
        return;
    }

    CefMouseEvent event;
    event.x = x;
    event.y = y;
    mCefBrowser->GetHost()->SendMouseWheelEvent(event, 0, deltaY);
}

#if defined(WIN32)
namespace {

    bool IsKeyDown(WPARAM key)
    {
        return (::GetKeyState(static_cast<int>(key)) & 0x8000) != 0;
    }

    // Standard translation from a Win32 keyboard message's wParam/lParam to a
    // CEF modifier bitmask - the same logic every CEF-on-Windows embedder
    // (cefclient, Dullahan, CEF Python, etc.) uses, since CEF itself never sees
    // the raw Windows message.
    uint32_t GetCefKeyboardModifiers(WPARAM wParam, LPARAM lParam)
    {
        uint32_t modifiers = 0;
        if (::GetKeyState(VK_SHIFT) & 0x8000)
        {
            modifiers |= EVENTFLAG_SHIFT_DOWN;
        }
        if (::GetKeyState(VK_CONTROL) & 0x8000)
        {
            modifiers |= EVENTFLAG_CONTROL_DOWN;
        }
        if (::GetKeyState(VK_MENU) & 0x8000)
        {
            modifiers |= EVENTFLAG_ALT_DOWN;
        }
        // Low bit of GetKeyState indicates a toggled (not held) state.
        if (::GetKeyState(VK_NUMLOCK) & 1)
        {
            modifiers |= EVENTFLAG_NUM_LOCK_ON;
        }
        if (::GetKeyState(VK_CAPITAL) & 1)
        {
            modifiers |= EVENTFLAG_CAPS_LOCK_ON;
        }

        switch (wParam)
        {
            case VK_RETURN:
                if ((lParam >> 16) & KF_EXTENDED)
                {
                    modifiers |= EVENTFLAG_IS_KEY_PAD;
                }
                break;
            case VK_INSERT:
            case VK_DELETE:
            case VK_HOME:
            case VK_END:
            case VK_PRIOR:
            case VK_NEXT:
            case VK_UP:
            case VK_DOWN:
            case VK_LEFT:
            case VK_RIGHT:
                if (!((lParam >> 16) & KF_EXTENDED))
                {
                    modifiers |= EVENTFLAG_IS_KEY_PAD;
                }
                break;
            case VK_NUMLOCK:
            case VK_NUMPAD0:
            case VK_NUMPAD1:
            case VK_NUMPAD2:
            case VK_NUMPAD3:
            case VK_NUMPAD4:
            case VK_NUMPAD5:
            case VK_NUMPAD6:
            case VK_NUMPAD7:
            case VK_NUMPAD8:
            case VK_NUMPAD9:
            case VK_DIVIDE:
            case VK_MULTIPLY:
            case VK_SUBTRACT:
            case VK_ADD:
            case VK_DECIMAL:
            case VK_CLEAR:
                modifiers |= EVENTFLAG_IS_KEY_PAD;
                break;
            case VK_SHIFT:
                if (IsKeyDown(VK_LSHIFT))
                {
                    modifiers |= EVENTFLAG_IS_LEFT;
                }
                else if (IsKeyDown(VK_RSHIFT))
                {
                    modifiers |= EVENTFLAG_IS_RIGHT;
                }
                break;
            case VK_CONTROL:
                if (IsKeyDown(VK_LCONTROL))
                {
                    modifiers |= EVENTFLAG_IS_LEFT;
                }
                else if (IsKeyDown(VK_RCONTROL))
                {
                    modifiers |= EVENTFLAG_IS_RIGHT;
                }
                break;
            case VK_MENU:
                if (IsKeyDown(VK_LMENU))
                {
                    modifiers |= EVENTFLAG_IS_LEFT;
                }
                else if (IsKeyDown(VK_RMENU))
                {
                    modifiers |= EVENTFLAG_IS_RIGHT;
                }
                break;
            case VK_LWIN:
                modifiers |= EVENTFLAG_IS_LEFT;
                break;
            case VK_RWIN:
                modifiers |= EVENTFLAG_IS_RIGHT;
                break;
        }
        return modifiers;
    }

}  // namespace
#endif  // defined(WIN32)

void llCefBrowser::SendKeyEvent(uint32_t message, uint64_t wParam, int64_t lParam)
{
    if (! mCefBrowser)
    {
        return;
    }

#if defined(WIN32)
    CefKeyEvent event;
    event.windows_key_code = static_cast<int>(wParam);
    event.native_key_code = static_cast<int>(lParam);
    event.is_system_key = (message == WM_SYSCHAR || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP);

    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
    {
        event.type = KEYEVENT_RAWKEYDOWN;
    }
    else if (message == WM_KEYUP || message == WM_SYSKEYUP)
    {
        event.type = KEYEVENT_KEYUP;
    }
    else
    {
        event.type = KEYEVENT_CHAR;
    }

    event.modifiers = GetCefKeyboardModifiers(static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam));
    mCefBrowser->GetHost()->SendKeyEvent(event);
#endif  // defined(WIN32)
}

void llCefBrowser::SetFocus(bool focus)
{
    if (! mCefBrowser)
    {
        return;
    }
    mCefBrowser->GetHost()->SetFocus(focus);
}

void llCefBrowser::ShowDevTools()
{
    if (! mCefBrowser)
    {
        return;
    }

    CefWindowInfo windowInfo;
#if defined(WIN32)
    // An ordinary top-level popup, not windowless - DevTools gets its own
    // natively-rendered window rather than being folded into this
    // browser's offscreen texture.
    windowInfo.SetAsPopup(nullptr, "Developer Tools");
#endif
    CefBrowserSettings settings;
    // Reusing this object as the DevTools browser's CefClient is safe even
    // though it also implements CefRenderHandler: GetRenderHandler() is
    // only ever consulted for windowless browsers, and windowInfo above
    // makes the DevTools browser a normal windowed one.
    mCefBrowser->GetHost()->ShowDevTools(windowInfo, this, settings, CefPoint());
}

void llCefBrowser::ExecuteJavaScript(const std::string& code)
{
    if (! mCefBrowser)
    {
        return;
    }

    CefRefPtr<CefFrame> frame = mCefBrowser->GetMainFrame();
    if (frame)
    {
        frame->ExecuteJavaScript(code, frame->GetURL(), 0);
    }
}

void llCefBrowser::CheckResizeWatchdog()
{
    if (mPendingWidth < 0)
    {
        return;    // nothing pending - last resize was confirmed
    }
    if (! mCefBrowser)
    {
        return;
    }

    const auto elapsed = std::chrono::steady_clock::now() - mResizeRequestedAt;
    if (elapsed > std::chrono::milliseconds(500))
    {
        mResizeRequestedAt = std::chrono::steady_clock::now();
        CefRefPtr<CefBrowser> browser = mCefBrowser;
        browser->GetHost()->WasResized();
        CefPostTask(TID_UI, new InvalidateTask(browser));
    }
}

void llCefBrowser::SetSize(int w, int h, bool clearImmediately)
{
    // Same underflow hazard as the constructor - see there.
    w = std::max(w, 1);
    h = std::max(h, 1);
    mWidth = w;
    mHeight = h;
    mPendingWidth = w;
    mPendingHeight = h;
    mResizeRequestedAt = std::chrono::steady_clock::now();
    if (clearImmediately)
    {
        // Opt-in: forces a blank frame at the new size right away so no
        // old-size content can ever be mistakenly displayed at the wrong
        // dimensions. Trade-off: this is what shows up as a black flash
        // until CEF's real repaint lands, since it happens on every call
        // regardless of how soon that repaint arrives.
        mPixels.ResetForResize(w, h);
    }
    // Otherwise: mWidth/mHeight are updated for GetViewRect, but the pixel
    // buffer is left untouched - CopyLatestFrame() keeps returning the old
    // frame (at the old size) until CEF's OnPaint delivers a real one at
    // the new dimensions, at which point llCefBrowserPixelBuffer::Write() resizes
    // itself automatically. Your GL-side code must still detect that size
    // change and reallocate texture storage rather than relying solely on
    // glTexSubImage2D.
    if (mCefBrowser)
    {
        CefRefPtr<CefBrowser> browser = mCefBrowser;
        browser->GetHost()->WasResized();
        // Deferred rather than called immediately: firing Invalidate() in
        // the same call stack as WasResized() races CEF's internal resize
        // IPC. If the render process hasn't processed the resize yet when
        // it services this invalidate, it repaints at the OLD size - and
        // that repaint consumes CEF's "one repaint owed" bookkeeping, so
        // the real resize's own repaint (still in flight from
        // WasResized()) never gets separately triggered. Posting this to
        // run on the next UI-thread tick gives the resize IPC a chance to
        // land first. browser is captured by the task (CefRefPtr) so it
        // stays alive until Execute() runs even if the browser starts
        // closing in the meantime.
        CefPostTask(TID_UI, new InvalidateTask(browser));
    }
}
