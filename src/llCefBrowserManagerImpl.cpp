/**
 *
 * @file llCefBrowserManagerImpl.cpp
 * @brief Implementation of llCefBrowserManagerImpl's recycled-slot handle table and browser lifecycle.
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

#include "llCefBrowserManagerImpl.h"
#include "include/cef_browser.h"
#include "include/cef_request_context_handler.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"
#include "llCefBrowserLibInitOptionsAccess.h"

namespace {

    class SetCookieCallbackImpl : public CefSetCookieCallback {
        public:
            explicit SetCookieCallbackImpl(std::function<void(bool)> callback) : mCallback(std::move(callback)) {}

            void OnComplete(bool success) override {
                if (mCallback) {
                    mCallback(success);
                }
            }

        private:
            std::function<void(bool)> mCallback;
            IMPLEMENT_REFCOUNTING(SetCookieCallbackImpl);
    };

    class DeleteCookiesCallbackImpl : public CefDeleteCookiesCallback {
        public:
            explicit DeleteCookiesCallbackImpl(std::function<void(int)> callback) : mCallback(std::move(callback)) {}

            void OnComplete(int numDeleted) override {
                if (mCallback) {
                    mCallback(numDeleted);
                }
            }

        private:
            std::function<void(int)> mCallback;
            IMPLEMENT_REFCOUNTING(DeleteCookiesCallbackImpl);
    };

    // Accumulates cookies across repeated Visit() calls and fires the app's
    // callback once, on the last one. Per CEF's own docs, Visit() "may never
    // be called if no cookies are found" - a store that's reachable but
    // genuinely empty means this callback never fires either, a known
    // limitation of there being no synchronous cookie count to fall back on.
    class CookieVisitorImpl : public CefCookieVisitor {
        public:
            explicit CookieVisitorImpl(std::function<void(const std::vector<llCefCookie>&)> callback)
                : mCallback(std::move(callback)) {}

            bool Visit(const CefCookie& cookie, int count, int total, bool& deleteCookie) override {
                llCefCookie c;
                c.name = CefString(&cookie.name).ToString();
                c.value = CefString(&cookie.value).ToString();
                c.domain = CefString(&cookie.domain).ToString();
                c.path = CefString(&cookie.path).ToString();
                c.secure = cookie.secure != 0;
                c.httpOnly = cookie.httponly != 0;
                mCookies.push_back(std::move(c));

                if (count == total - 1 && mCallback) {
                    mCallback(mCookies);
                }
                return true;  // keep visiting
            }

        private:
            std::function<void(const std::vector<llCefCookie>&)> mCallback;
            std::vector<llCefCookie> mCookies;
            IMPLEMENT_REFCOUNTING(CookieVisitorImpl);
    };

}  // namespace

llCefBrowserManagerImpl::llCefBrowserManagerImpl(const std::string& uiCachePath, const std::string& primCachePath)
    : mCachePath(uiCachePath)
{
    // Two independent CefRequestContexts, each with its own on-disk cache /
    // cookie jar -- see the class comment in the header for why. All browsers
    // created through this manager for a given isUI value share that one
    // context, same as the old single-context design did for everything.
    CefRequestContextSettings uiSettings;
    if (! uiCachePath.empty())
    {
        CefString(&uiSettings.cache_path).FromString(uiCachePath);
    }
    mUIContext = CefRequestContext::CreateContext(uiSettings, nullptr);
    DCHECK(mUIContext);

    CefRequestContextSettings primSettings;
    if (! primCachePath.empty())
    {
        CefString(&primSettings.cache_path).FromString(primCachePath);
    }
    mPrimContext = CefRequestContext::CreateContext(primSettings, nullptr);
    DCHECK(mPrimContext);
}

const std::string& llCefBrowserManagerImpl::GetCachePath() const
{
    return mCachePath;
}

llCefBrowserManagerImpl::~llCefBrowserManagerImpl()
{
    // Expects DestroyAll() + a few CefDoMessageLoopWork() pumps to have
    // already happened; this destructor does not block or pump itself. A
    // browser still alive here would later call back into this
    // already-destroyed object via OnBeforeClose -> NotifyBrowserClosed()
    // (see llCefBrowser::OnBeforeClose) -- catch that misuse now instead.
    for (const Slot& slot : mSlots)
    {
        DCHECK(! slot.mBrowser);
    }
}

llCefBrowserHandle llCefBrowserManagerImpl::CreateBrowser(const std::string& url, int width, int height, bool isUI)
{
    CEF_REQUIRE_UI_THREAD();

    uint32_t index;
    if (! mFreeList.empty())
    {
        index = mFreeList.back();
        mFreeList.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(mSlots.size());
        mSlots.emplace_back();
    }

    Slot& slot = mSlots[index];
    slot.mGeneration++;
    slot.mClosing = false;

    llCefBrowserHandle handle{index, slot.mGeneration};

    CefRefPtr<llCefBrowser> browserClient = new llCefBrowser(handle, width, height, this, url, isUI);
    slot.mBrowser = browserClient;

    CefWindowInfo windowInfo;
    // 0, not nullptr: CefWindowHandle is a pointer (HWND/NSView*) on Windows/
    // macOS but a plain integral X11 window ID (cef_window_handle_t, unsigned
    // long) on Linux -- nullptr doesn't convert to that, 0 is a valid "no
    // parent" value for both kinds of handle.
    windowInfo.SetAsWindowless(0);  // offscreen, no native parent window

    const llCefBrowserLibInitOptions& options = LLGetInitOptions();

    // External Begin Frame mode: OFF by default (options.externalBeginFrameEnabled,
    // see that field's own comment) -- an explicit, named toggle rather than a
    // hardcoded value, kept specifically so the two repaint-driving mechanisms
    // can still be A/B tested with everything else held identical. Compositing
    // defaults to CEF's own windowless_frame_rate-paced internal timer, plus
    // on-demand llCefBrowser::RequestRepaint() calls (CefBrowserHost::Invalidate(),
    // NOT SendExternalBeginFrame() when this option is false -- see
    // RequestRepaint()'s own comment) for lower input-to-repaint latency than
    // the internal timer alone provides.
    //
    // HISTORY: this was unconditionally `true` from 2026-08-14 (added for
    // scroll latency -- CEF's internal timer runs completely decoupled from
    // when input arrives, confirmed via measurement to cluster input-to-repaint
    // gaps in discrete bands near 1x/2x/3x the timer's own period; External
    // Begin Frame mode, driven once per producer tick, measurably doubled the
    // proportion of near-instant (<2ms) responses) until 2026-11-09, when it
    // was found to break DOM hit-testing/click on WebGL canvas content --
    // Google Maps POI clicks/hover never registered with this mode on, worked
    // correctly with it off, on both Windows and macOS (legacy Dullahan-based
    // CEF, which never enables this mode, behaves correctly; this backend
    // didn't). Three different SendExternalBeginFrame() calling-pattern fixes
    // were tried and live-tested (unrestricted ~1000Hz flooding -> a flat
    // 60fps wall-clock cap -> an OnPaint-acknowledgment-based gate) and none
    // fixed the click/hover bug, arguing it's tied to the mode being enabled
    // at all, not to how well-paced the calls into it are. Filed upstream
    // (chromiumembedded/cef's GitHub issue tracker); a maintainer suggested
    // testing the official `cefclient` sample with
    // --off-screen-rendering-enabled --external-begin-frame-enabled, which
    // works correctly on Google Maps -- but reading its actual source
    // (tests/cefclient/browser/osr_render_handler_win.cc) shows it renders via
    // OnAcceleratedPaint() (GPU shared-texture path) when driving this mode,
    // not the plain CPU-buffer OnPaint() path this library uses, so this may
    // not be a fair comparison -- not confirmed either way. Switching to
    // OnAcceleratedPaint() would be the more faithful fix if the GPU-texture
    // theory holds, but isn't attempted here (shared textures don't work on
    // all hardware; on the list for later, not a near-term fix).
    //
    // Switched the DEFAULT to Invalidate()-based RequestRepaint() instead of
    // deleting the SendExternalBeginFrame()-based mode outright, since CEF's
    // own doc comment for Invalidate() states no dependency on external-
    // begin-frame mode at all, unlike SendExternalBeginFrame()'s explicit
    // "only valid when external_begin_frame_enabled is true". Confirmed
    // 2026-11-10: Maps POI click/hover now works correctly with the default
    // (highlight-on-hover and click both register), live-tested via a local
    // lib swap. Scroll/pan latency reported as "about the same, maybe
    // slightly slower" than with External Begin Frame on -- a subjective
    // read, not a hard measurement.
    //
    // GAP the default reintroduces, NOT YET RESOLVED (a fix was tried and
    // reverted): the distance/priority render-throttle feature
    // (kSetRenderRate, see llcefproducer.cpp) was built around External Begin
    // Frame being the *sole* driver of repaints -- gating how often
    // SendExternalBeginFrame() was called was the only per-browser repaint-
    // rate control that existed, so disabling this mode by default broke
    // that throttle's real CPU/GPU-saving effect (CEF's internal timer
    // composites every browser at the same rate regardless of the old
    // gating). A direct fix via llCefBrowser::SetFrameRate() (CefBrowserHost::
    // SetWindowlessFrameRate(), callable at runtime) was tried and reverted
    // the same day -- live-tested with several same-priority media instances
    // competing for a small number of "full rate" slots (so tiers flipped
    // frequently as their near-identical interest values jittered frame to
    // frame), calling that method was enough to cause a connection-churn/
    // reconnect-loop regression. See that method's own comment for the
    // caution note before trying again.
    windowInfo.external_begin_frame_enabled = options.externalBeginFrameEnabled;

    CefBrowserSettings browserSettings;
    browserSettings.windowless_frame_rate = options.windowlessFrameRate;
    browserSettings.background_color = options.backgroundColor;
    browserSettings.javascript = LLToCefState(options.javascript);
    browserSettings.javascript_access_clipboard = LLToCefState(options.javascriptAccessClipboard);
    browserSettings.image_loading = LLToCefState(options.imageLoading);
    browserSettings.local_storage = LLToCefState(options.localStorage);
    browserSettings.webgl = LLToCefState(options.webgl);

    // Empty, not url -- see llCefBrowser::OnAfterCreated()'s own comment on why
    // the real navigation happens there instead, as a follow-up LoadURL().
    if (! CefBrowserHost::CreateBrowser(windowInfo, browserClient, std::string(),
                                        browserSettings, nullptr, isUI ? mUIContext : mPrimContext))
    {
        // Failed synchronously -- OnAfterCreated will never fire for this
        // browser, so the normal OnBeforeClose -> NotifyBrowserClosed path
        // that frees the slot will never run either. Recycle it here
        // instead of handing back a handle that could never be closed.
        slot.mBrowser = nullptr;
        slot.mClosing = false;
        mFreeList.push_back(index);
        return llCefBrowserHandle::Invalid();
    }

    return handle;
}

bool llCefBrowserManagerImpl::IsValid(llCefBrowserHandle handle) const
{
    // mSlots is only ever mutated on the UI thread (CreateBrowser can
    // reallocate it); every read of it, including this one, needs the same
    // thread affinity to avoid racing that mutation.
    CEF_REQUIRE_UI_THREAD();
    if (! handle.IsValid() || handle.index >= mSlots.size())
    {
        return false;
    }
    const Slot& slot = mSlots[handle.index];
    return slot.mGeneration == handle.generation && slot.mBrowser && ! slot.mClosing;
}

llCefBrowser* llCefBrowserManagerImpl::Get(llCefBrowserHandle handle)
{
    if (! IsValid(handle))
    {
        return nullptr;
    }
    return mSlots[handle.index].mBrowser.get();
}

const llCefBrowser* llCefBrowserManagerImpl::Get(llCefBrowserHandle handle) const
{
    if (! IsValid(handle))
    {
        return nullptr;
    }
    return mSlots[handle.index].mBrowser.get();
}

void llCefBrowserManagerImpl::DestroyBrowser(llCefBrowserHandle handle)
{
    CEF_REQUIRE_UI_THREAD();
    if (! IsValid(handle))
    {
        return;
    }

    Slot& slot = mSlots[handle.index];
    slot.mClosing = true;  // blocks new lookups from succeeding immediately

    // Handles all cases uniformly: browser already exists (closes now),
    // browser is still being created asynchronously (closes itself the
    // moment OnAfterCreated fires), or CreateBrowser never got that far at
    // all. Either way, the slot is only freed later via OnBeforeClose ->
    // NotifyBrowserClosed, once CEF actually confirms the browser is gone.
    slot.mBrowser->RequestClose();
}

void llCefBrowserManagerImpl::NotifyBrowserClosed(llCefBrowserHandle handle)
{
    if (handle.index >= mSlots.size())
    {
        return;
    }
    Slot& slot = mSlots[handle.index];
    if (slot.mGeneration != handle.generation)
    {
        return;    // stale notification
    }

    slot.mBrowser = nullptr;
    slot.mClosing = false;
    mFreeList.push_back(handle.index);
}

bool llCefBrowserManagerImpl::DestroyLastBrowser()
{
    CEF_REQUIRE_UI_THREAD();
    for (uint32_t i = static_cast<uint32_t>(mSlots.size()); i-- > 0;)
    {
        Slot& slot = mSlots[i];
        if (slot.mBrowser && ! slot.mClosing)
        {
            DestroyBrowser(llCefBrowserHandle{i, slot.mGeneration});
            return true;
        }
    }
    return false;
}

void llCefBrowserManagerImpl::DestroyAll()
{
    CEF_REQUIRE_UI_THREAD();
    for (uint32_t i = 0; i < mSlots.size(); ++i)
    {
        if (mSlots[i].mBrowser)
        {
            DestroyBrowser(llCefBrowserHandle{i, mSlots[i].mGeneration});
        }
    }
}

void llCefBrowserManagerImpl::SetUserData(llCefBrowserHandle handle, void* userData)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->userData = userData;
    }
}

void* llCefBrowserManagerImpl::GetUserData(llCefBrowserHandle handle)
{
    if (llCefBrowser* b = Get(handle))
    {
        return b->userData;
    }
    return nullptr;
}

int llCefBrowserManagerImpl::GetWidth(llCefBrowserHandle handle) const
{
    const llCefBrowser* b = Get(handle);
    return b ? b->GetWidth() : 0;
}

int llCefBrowserManagerImpl::GetHeight(llCefBrowserHandle handle) const
{
    const llCefBrowser* b = Get(handle);
    return b ? b->GetHeight() : 0;
}

void llCefBrowserManagerImpl::ResizeBrowser(llCefBrowserHandle handle, int width, int height, bool clearImmediately)
{
    CEF_REQUIRE_UI_THREAD();
    if (llCefBrowser* b = Get(handle))
    {
        b->SetSize(width, height, clearImmediately);
    }
}

void llCefBrowserManagerImpl::RequestRepaint(llCefBrowserHandle handle)
{
    CEF_REQUIRE_UI_THREAD();
    if (llCefBrowser* b = Get(handle))
    {
        b->RequestRepaint();
    }
}

void llCefBrowserManagerImpl::SetFrameRate(llCefBrowserHandle handle, int fps)
{
    CEF_REQUIRE_UI_THREAD();
    if (llCefBrowser* b = Get(handle))
    {
        b->SetFrameRate(fps);
    }
}

bool llCefBrowserManagerImpl::CopyLatestFrame(llCefBrowserHandle handle, std::vector<uint8_t>& dst, int& w, int& h)
{
    if (llCefBrowser* b = Get(handle))
    {
        return b->CopyLatestFrame(dst, w, h);
    }
    return false;
}

bool llCefBrowserManagerImpl::CanGoBack(llCefBrowserHandle handle) const
{
    const llCefBrowser* b = Get(handle);
    return b && b->CanGoBack();
}

void llCefBrowserManagerImpl::GoBack(llCefBrowserHandle handle)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->GoBack();
    }
}

bool llCefBrowserManagerImpl::CanGoForward(llCefBrowserHandle handle) const
{
    const llCefBrowser* b = Get(handle);
    return b && b->CanGoForward();
}

void llCefBrowserManagerImpl::GoForward(llCefBrowserHandle handle)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->GoForward();
    }
}

bool llCefBrowserManagerImpl::IsLoading(llCefBrowserHandle handle) const
{
    const llCefBrowser* b = Get(handle);
    return b && b->IsLoading();
}

void llCefBrowserManagerImpl::Reload(llCefBrowserHandle handle, bool ignoreCache)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->Reload(ignoreCache);
    }
}

void llCefBrowserManagerImpl::StopLoad(llCefBrowserHandle handle)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->StopLoad();
    }
}

void llCefBrowserManagerImpl::Navigate(llCefBrowserHandle handle, const std::string& url)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->Navigate(url);
    }
}

void llCefBrowserManagerImpl::Undo(llCefBrowserHandle handle)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->Undo();
    }
}

void llCefBrowserManagerImpl::Redo(llCefBrowserHandle handle)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->Redo();
    }
}

void llCefBrowserManagerImpl::Copy(llCefBrowserHandle handle)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->Copy();
    }
}

void llCefBrowserManagerImpl::Cut(llCefBrowserHandle handle)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->Cut();
    }
}

void llCefBrowserManagerImpl::Paste(llCefBrowserHandle handle)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->Paste();
    }
}

void llCefBrowserManagerImpl::Delete(llCefBrowserHandle handle)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->Delete();
    }
}

void llCefBrowserManagerImpl::SelectAll(llCefBrowserHandle handle)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SelectAll();
    }
}

void llCefBrowserManagerImpl::SetPageZoom(llCefBrowserHandle handle, float zoomLevel)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetPageZoom(zoomLevel);
    }
}

void llCefBrowserManagerImpl::SetCookie(const std::string& url, const std::string& name, const std::string& value,
                                        const std::string& domain, const std::string& path, bool httpOnly, bool secure,
                                        std::function<void(bool)> callback, bool alsoPrimContext)
{
    // Mirrored into the prim context first, fire-and-forget (no separate callback -
    // the one below still only ever reports the UI context's own result, matching
    // this method's contract before alsoPrimContext existed). Deliberately opt-in
    // per call rather than a second always-on context-wide default: callers decide
    // per cookie whether it's fine to share with prim-hosted content.
    if (alsoPrimContext && mPrimContext)
    {
        if (CefRefPtr<CefCookieManager> primCookieManager = mPrimContext->GetCookieManager(nullptr))
        {
            CefCookie primCookie;
            CefString(&primCookie.name).FromString(name);
            CefString(&primCookie.value).FromString(value);
            CefString(&primCookie.domain).FromString(domain);
            CefString(&primCookie.path).FromString(path);
            primCookie.httponly = httpOnly ? 1 : 0;
            primCookie.secure = secure ? 1 : 0;
            primCookieManager->SetCookie(url, primCookie, nullptr);
        }
    }

    CefRefPtr<CefCookieManager> cookieManager = mUIContext ? mUIContext->GetCookieManager(nullptr) : nullptr;
    if (! cookieManager)
    {
        if (callback)
        {
            callback(false);
        }
        return;
    }

    CefCookie cookie;
    CefString(&cookie.name).FromString(name);
    CefString(&cookie.value).FromString(value);
    CefString(&cookie.domain).FromString(domain);
    CefString(&cookie.path).FromString(path);
    cookie.httponly = httpOnly ? 1 : 0;
    cookie.secure = secure ? 1 : 0;

    CefRefPtr<CefSetCookieCallback> cefCallback;
    if (callback)
    {
        cefCallback = new SetCookieCallbackImpl(std::move(callback));
    }
    if (! cookieManager->SetCookie(url, cookie, cefCallback) && cefCallback)
    {
        // Rejected synchronously (invalid URL, disallowed characters,
        // etc.) - CEF won't invoke the callback in that case, so resolve
        // it here instead of leaving the app waiting.
        static_cast<SetCookieCallbackImpl*>(cefCallback.get())->OnComplete(false);
    }
}

void llCefBrowserManagerImpl::GetCookies(std::function<void(const std::vector<llCefCookie>&)> callback)
{
    if (! callback)
    {
        return;
    }

    CefRefPtr<CefCookieManager> cookieManager = mUIContext ? mUIContext->GetCookieManager(nullptr) : nullptr;
    if (! cookieManager)
    {
        callback({});
        return;
    }

    CefRefPtr<CookieVisitorImpl> visitor = new CookieVisitorImpl(callback);
    if (! cookieManager->VisitAllCookies(visitor))
    {
        // Cookies could not be accessed at all - report empty rather than
        // leaving the app waiting on a callback that will never fire.
        callback({});
    }
}

void llCefBrowserManagerImpl::DeleteAllCookies(std::function<void(int)> callback)
{
    CefRefPtr<CefCookieManager> cookieManager = mUIContext ? mUIContext->GetCookieManager(nullptr) : nullptr;
    if (! cookieManager)
    {
        if (callback)
        {
            callback(0);
        }
        return;
    }

    CefRefPtr<CefDeleteCookiesCallback> cefCallback;
    if (callback)
    {
        cefCallback = new DeleteCookiesCallbackImpl(std::move(callback));
    }
    // Empty url/cookie_name deletes every cookie for every host and domain.
    if (! cookieManager->DeleteCookies(CefString(), CefString(), cefCallback) && cefCallback)
    {
        static_cast<DeleteCookiesCallbackImpl*>(cefCallback.get())->OnComplete(0);
    }
}

void llCefBrowserManagerImpl::SetOnPageChangedCallback(llCefBrowserHandle handle, std::function<void()> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnPageChangedCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnCursorChangedCallback(llCefBrowserHandle handle, std::function<void(llCefCursorType)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnCursorChangedCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnAddressChangeCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnAddressChangeCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnTitleChangeCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnTitleChangeCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnStatusMessageCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnStatusMessageCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnConsoleMessageCallback(llCefBrowserHandle handle, std::function<void(const std::string&, const std::string&, int)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnConsoleMessageCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnTooltipCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnTooltipCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnLoadStartCallback(llCefBrowserHandle handle, std::function<void()> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnLoadStartCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnLoadEndCallback(llCefBrowserHandle handle, std::function<void(int)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnLoadEndCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnLoadingStateChangeCallback(llCefBrowserHandle handle, std::function<void(bool, bool, bool)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnLoadingStateChangeCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnLoadErrorCallback(llCefBrowserHandle handle, std::function<void(int, const std::string&, const std::string&)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnLoadErrorCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnQueryCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnQueryCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnFileDialogCallback(llCefBrowserHandle handle, std::function<void(int64_t, llCefFileDialogMode, const std::string&, const std::string&, const std::vector<std::string>&)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnFileDialogCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::RespondToFileDialog(llCefBrowserHandle handle, int64_t dialogId, const std::vector<std::string>& filePaths)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->RespondToFileDialog(dialogId, filePaths);
    }
}

void llCefBrowserManagerImpl::SetOnAuthRequestCallback(llCefBrowserHandle handle, std::function<void(int64_t, const std::string&, const std::string&, int, const std::string&, const std::string&, bool)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnAuthRequestCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::RespondToAuthRequest(llCefBrowserHandle handle, int64_t requestId, bool proceed,
                                                    const std::string& username, const std::string& password)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->RespondToAuthRequest(requestId, proceed, username, password);
    }
}

void llCefBrowserManagerImpl::SetOnCustomSchemeURLCallback(llCefBrowserHandle handle, std::function<void(const std::string&, bool, bool)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnCustomSchemeURLCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnOpenPopupCallback(llCefBrowserHandle handle, std::function<void(const std::string&, const std::string&)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnOpenPopupCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnPageSourceRetrievedCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback, size_t maxBytes)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnPageSourceRetrievedCallback(std::move(callback), maxBytes);
    }
}

void llCefBrowserManagerImpl::SetOnJSDialogCallback(llCefBrowserHandle handle, std::function<bool(const std::string&, llCefJSDialogType, const std::string&, const std::string&)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnJSDialogCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnBeforeUnloadCallback(llCefBrowserHandle handle, std::function<bool(const std::string&, bool)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnBeforeUnloadCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnAudioStreamStartedCallback(llCefBrowserHandle handle, std::function<void(int, int, int)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnAudioStreamStartedCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnAudioStreamPacketCallback(llCefBrowserHandle handle, std::function<void(const float* const*, int, int64_t, int)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnAudioStreamPacketCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnAudioStreamStoppedCallback(llCefBrowserHandle handle, std::function<void()> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnAudioStreamStoppedCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SetOnAudioStreamErrorCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnAudioStreamErrorCallback(std::move(callback));
    }
}

void llCefBrowserManagerImpl::SendMouseClickEvent(llCefBrowserHandle handle, int x, int y, llCefMouseButton button, bool mouseUp, int clickCount)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SendMouseClickEvent(x, y, button, mouseUp, clickCount);
    }
}

void llCefBrowserManagerImpl::SendMouseMoveEvent(llCefBrowserHandle handle, int x, int y, bool mouseLeave)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SendMouseMoveEvent(x, y, mouseLeave);
    }
}

void llCefBrowserManagerImpl::SendMouseWheelEvent(llCefBrowserHandle handle, int x, int y, int deltaY)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SendMouseWheelEvent(x, y, deltaY);
    }
}

void llCefBrowserManagerImpl::SendKeyEvent(llCefBrowserHandle handle, llCefKeyEventType type, uint32_t modifiers,
                                            int windows_key_code, int native_key_code, uint32_t character,
                                            uint32_t unmodified_character, bool is_system_key)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SendKeyEvent(type, modifiers, windows_key_code, native_key_code, character,
                         unmodified_character, is_system_key);
    }
}

void llCefBrowserManagerImpl::SetFocus(llCefBrowserHandle handle, bool focus)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetFocus(focus);
    }
}

void llCefBrowserManagerImpl::SetAudioMuted(llCefBrowserHandle handle, bool mute)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetAudioMuted(mute);
    }
}

void llCefBrowserManagerImpl::ShowDevTools(llCefBrowserHandle handle)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->ShowDevTools();
    }
}

void llCefBrowserManagerImpl::ExecuteJavaScript(llCefBrowserHandle handle, const std::string& code)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->ExecuteJavaScript(code);
    }
}

size_t llCefBrowserManagerImpl::LiveBrowserCount() const
{
    size_t count = 0;
    for (const auto& slot : mSlots)
    {
        // Excludes browsers mid-close so this always matches how many
        // browsers ForEachBrowser() will actually visit.
        if (slot.mBrowser && ! slot.mClosing)
        {
            ++count;
        }
    }
    return count;
}

void llCefBrowserManagerImpl::ForEachBrowser(const std::function<void(llCefBrowserHandle)>& fn)
{
    CEF_REQUIRE_UI_THREAD();
    for (uint32_t i = 0; i < mSlots.size(); ++i)
    {
        Slot& slot = mSlots[i];
        if (slot.mBrowser && ! slot.mClosing)
        {
            fn(llCefBrowserHandle{i, slot.mGeneration});
        }
    }
}

void llCefBrowserManagerImpl::Tick()
{
    CEF_REQUIRE_UI_THREAD();
    for (auto& slot : mSlots)
    {
        if (slot.mBrowser && ! slot.mClosing)
        {
            slot.mBrowser->CheckResizeWatchdog();

            // Whether this is strictly mandatory depends on
            // llCefBrowserLibInitOptions::externalBeginFrameEnabled (see that
            // field's and CreateBrowser()'s own comments): with it false (the
            // default), CEF's own windowless_frame_rate-paced internal timer
            // still composites frames on its own regardless of whether this
            // is ever called, so this is just an optional latency
            // improvement; with it true, CEF does NOT composite on its own
            // at all, so this becomes the only thing driving any repaint,
            // same as the old SendExternalBeginFrame()-only-mode behavior.
            // Either way it's kept here as a baseline, folded in because
            // Tick() is already unconditionally called every tick by
            // everything that uses this library: a call here costs nothing
            // when nothing changed, and an explicit call right after
            // processing input (see SLCefProducer.cpp's own call site) still
            // gets the repaint sooner than wherever in the caller's own loop
            // Tick() happens to run.
            slot.mBrowser->RequestRepaint();
        }
    }
}
