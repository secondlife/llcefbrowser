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

    CefRefPtr<llCefBrowser> browserClient = new llCefBrowser(handle, width, height, this);
    slot.mBrowser = browserClient;

    CefWindowInfo windowInfo;
    windowInfo.SetAsWindowless(nullptr);  // offscreen, no native parent window

    // Compositing is driven explicitly (llCefBrowser::SendExternalBeginFrame(), called
    // once per producer main-loop tick) rather than CEF's own windowless_frame_rate-
    // paced internal timer -- that timer runs completely decoupled from when input
    // actually arrives, so every interaction has to wait for CEF's own independently-
    // scheduled next tick before any repaint even begins (confirmed via measurement:
    // input-to-repaint gaps clustered in discrete bands near 1x/2x/3x the timer's own
    // period, the signature of a fixed-rate scheduler rather than input-driven
    // rendering). Driving it ourselves, right after processing each tick's input,
    // removes that scheduling gap.
    windowInfo.external_begin_frame_enabled = true;

    const llCefBrowserLibInitOptions& options = LLGetInitOptions();

    CefBrowserSettings browserSettings;
    browserSettings.windowless_frame_rate = options.windowlessFrameRate;
    browserSettings.background_color = options.backgroundColor;
    browserSettings.javascript = LLToCefState(options.javascript);
    browserSettings.javascript_access_clipboard = LLToCefState(options.javascriptAccessClipboard);
    browserSettings.image_loading = LLToCefState(options.imageLoading);
    browserSettings.local_storage = LLToCefState(options.localStorage);
    browserSettings.webgl = LLToCefState(options.webgl);

    if (! CefBrowserHost::CreateBrowser(windowInfo, browserClient, url,
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

void llCefBrowserManagerImpl::SendExternalBeginFrame(llCefBrowserHandle handle)
{
    CEF_REQUIRE_UI_THREAD();
    if (llCefBrowser* b = Get(handle))
    {
        b->SendExternalBeginFrame();
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
                                        std::function<void(bool)> callback)
{
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

void llCefBrowserManagerImpl::SetOnAuthRequestCallback(llCefBrowserHandle handle, std::function<bool(const std::string&, const std::string&, int, const std::string&, const std::string&, bool, std::string&, std::string&)> callback)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetOnAuthRequestCallback(std::move(callback));
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

void llCefBrowserManagerImpl::SendKeyEvent(llCefBrowserHandle handle, uint32_t message, uint64_t wParam, int64_t lParam)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SendKeyEvent(message, wParam, lParam);
    }
}

void llCefBrowserManagerImpl::SetFocus(llCefBrowserHandle handle, bool focus)
{
    if (llCefBrowser* b = Get(handle))
    {
        b->SetFocus(focus);
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
        }
    }
}
