/**
 *
 * @file llCefBrowserManager.cpp
 * @brief Thin pimpl forwarder from the public llCefBrowserManager API to llCefBrowserManagerImpl.
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

#include "llCefBrowserManager.h"
#include "llCefBrowserManagerImpl.h"

// llCefBrowserManager::Impl (forward-declared in the header) is just
// llCefBrowserManagerImpl under a name that fits the pimpl idiom cleanly -
// inheriting constructors keeps this a one-line adapter.
class llCefBrowserManager::Impl : public llCefBrowserManagerImpl {
    public:
        using llCefBrowserManagerImpl::llCefBrowserManagerImpl;
};

llCefBrowserManager::llCefBrowserManager(const std::string& uiCachePath, const std::string& primCachePath)
    : mImpl(std::make_unique<Impl>(uiCachePath, primCachePath)) {}

// Out-of-line, needed because Impl is an incomplete type in the header -
// std::unique_ptr<Impl>'s default destructor would otherwise require the
// complete type at every call site that includes llCefBrowserManager.h.
llCefBrowserManager::~llCefBrowserManager() = default;

llCefBrowserHandle llCefBrowserManager::CreateBrowser(const std::string& url, int width, int height, bool isUI)
{
    return mImpl->CreateBrowser(url, width, height, isUI);
}

void llCefBrowserManager::DestroyBrowser(llCefBrowserHandle handle)
{
    mImpl->DestroyBrowser(handle);
}

bool llCefBrowserManager::DestroyLastBrowser()
{
    return mImpl->DestroyLastBrowser();
}

void llCefBrowserManager::DestroyAll()
{
    mImpl->DestroyAll();
}

bool llCefBrowserManager::IsValid(llCefBrowserHandle handle) const
{
    return mImpl->IsValid(handle);
}

void llCefBrowserManager::SetUserData(llCefBrowserHandle handle, void* userData)
{
    mImpl->SetUserData(handle, userData);
}

void* llCefBrowserManager::GetUserData(llCefBrowserHandle handle)
{
    return mImpl->GetUserData(handle);
}

const std::string& llCefBrowserManager::GetCachePath() const
{
    return mImpl->GetCachePath();
}

int llCefBrowserManager::GetWidth(llCefBrowserHandle handle) const
{
    return mImpl->GetWidth(handle);
}

int llCefBrowserManager::GetHeight(llCefBrowserHandle handle) const
{
    return mImpl->GetHeight(handle);
}

void llCefBrowserManager::ResizeBrowser(llCefBrowserHandle handle, int width, int height, bool clearImmediately)
{
    mImpl->ResizeBrowser(handle, width, height, clearImmediately);
}

void llCefBrowserManager::SendExternalBeginFrame(llCefBrowserHandle handle)
{
    mImpl->SendExternalBeginFrame(handle);
}

bool llCefBrowserManager::CopyLatestFrame(llCefBrowserHandle handle, std::vector<uint8_t>& dst, int& w, int& h)
{
    return mImpl->CopyLatestFrame(handle, dst, w, h);
}

bool llCefBrowserManager::CanGoBack(llCefBrowserHandle handle) const
{
    return mImpl->CanGoBack(handle);
}

void llCefBrowserManager::GoBack(llCefBrowserHandle handle)
{
    mImpl->GoBack(handle);
}

bool llCefBrowserManager::CanGoForward(llCefBrowserHandle handle) const
{
    return mImpl->CanGoForward(handle);
}

void llCefBrowserManager::GoForward(llCefBrowserHandle handle)
{
    mImpl->GoForward(handle);
}

bool llCefBrowserManager::IsLoading(llCefBrowserHandle handle) const
{
    return mImpl->IsLoading(handle);
}

void llCefBrowserManager::Reload(llCefBrowserHandle handle, bool ignoreCache)
{
    mImpl->Reload(handle, ignoreCache);
}

void llCefBrowserManager::StopLoad(llCefBrowserHandle handle)
{
    mImpl->StopLoad(handle);
}

void llCefBrowserManager::Navigate(llCefBrowserHandle handle, const std::string& url)
{
    mImpl->Navigate(handle, url);
}

// See the header: CEF has no real capability query for any of these, so
// these are deliberately just constants rather than routed through
// mImpl/llCefBrowser - there's no per-browser state to check.
bool llCefBrowserManager::CanUndo(llCefBrowserHandle handle) const
{
    return true;
}
bool llCefBrowserManager::CanRedo(llCefBrowserHandle handle) const
{
    return true;
}
bool llCefBrowserManager::CanCopy(llCefBrowserHandle handle) const
{
    return true;
}
bool llCefBrowserManager::CanCut(llCefBrowserHandle handle) const
{
    return true;
}
bool llCefBrowserManager::CanPaste(llCefBrowserHandle handle) const
{
    return true;
}
bool llCefBrowserManager::CanDelete(llCefBrowserHandle handle) const
{
    return true;
}
bool llCefBrowserManager::CanSelectAll(llCefBrowserHandle handle) const
{
    return true;
}

void llCefBrowserManager::Undo(llCefBrowserHandle handle)
{
    mImpl->Undo(handle);
}

void llCefBrowserManager::Redo(llCefBrowserHandle handle)
{
    mImpl->Redo(handle);
}

void llCefBrowserManager::Copy(llCefBrowserHandle handle)
{
    mImpl->Copy(handle);
}

void llCefBrowserManager::Cut(llCefBrowserHandle handle)
{
    mImpl->Cut(handle);
}

void llCefBrowserManager::Paste(llCefBrowserHandle handle)
{
    mImpl->Paste(handle);
}

void llCefBrowserManager::Delete(llCefBrowserHandle handle)
{
    mImpl->Delete(handle);
}

void llCefBrowserManager::SelectAll(llCefBrowserHandle handle)
{
    mImpl->SelectAll(handle);
}

void llCefBrowserManager::SetPageZoom(llCefBrowserHandle handle, float zoomLevel)
{
    mImpl->SetPageZoom(handle, zoomLevel);
}

void llCefBrowserManager::SetCookie(const std::string& url, const std::string& name, const std::string& value,
                                    const std::string& domain, const std::string& path, bool httpOnly, bool secure,
                                    std::function<void(bool)> callback, bool alsoPrimContext)
{
    mImpl->SetCookie(url, name, value, domain, path, httpOnly, secure, std::move(callback), alsoPrimContext);
}

void llCefBrowserManager::GetCookies(std::function<void(const std::vector<llCefCookie>&)> callback)
{
    mImpl->GetCookies(std::move(callback));
}

void llCefBrowserManager::DeleteAllCookies(std::function<void(int)> callback)
{
    mImpl->DeleteAllCookies(std::move(callback));
}

void llCefBrowserManager::SetOnPageChangedCallback(llCefBrowserHandle handle, std::function<void()> callback)
{
    mImpl->SetOnPageChangedCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnCursorChangedCallback(llCefBrowserHandle handle, std::function<void(llCefCursorType)> callback)
{
    mImpl->SetOnCursorChangedCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnAddressChangeCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback)
{
    mImpl->SetOnAddressChangeCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnTitleChangeCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback)
{
    mImpl->SetOnTitleChangeCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnStatusMessageCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback)
{
    mImpl->SetOnStatusMessageCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnConsoleMessageCallback(llCefBrowserHandle handle, std::function<void(const std::string&, const std::string&, int)> callback)
{
    mImpl->SetOnConsoleMessageCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnTooltipCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback)
{
    mImpl->SetOnTooltipCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnLoadStartCallback(llCefBrowserHandle handle, std::function<void()> callback)
{
    mImpl->SetOnLoadStartCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnLoadEndCallback(llCefBrowserHandle handle, std::function<void(int)> callback)
{
    mImpl->SetOnLoadEndCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnLoadErrorCallback(llCefBrowserHandle handle, std::function<void(int, const std::string&, const std::string&)> callback)
{
    mImpl->SetOnLoadErrorCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnQueryCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback)
{
    mImpl->SetOnQueryCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnFileDialogCallback(llCefBrowserHandle handle, std::function<void(int64_t, llCefFileDialogMode, const std::string&, const std::string&, const std::vector<std::string>&)> callback)
{
    mImpl->SetOnFileDialogCallback(handle, std::move(callback));
}

void llCefBrowserManager::RespondToFileDialog(llCefBrowserHandle handle, int64_t dialogId, const std::vector<std::string>& filePaths)
{
    mImpl->RespondToFileDialog(handle, dialogId, filePaths);
}

void llCefBrowserManager::SetOnAuthRequestCallback(llCefBrowserHandle handle, std::function<bool(const std::string&, const std::string&, int, const std::string&, const std::string&, bool, std::string&, std::string&)> callback)
{
    mImpl->SetOnAuthRequestCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnCustomSchemeURLCallback(llCefBrowserHandle handle, std::function<void(const std::string&, bool, bool)> callback)
{
    mImpl->SetOnCustomSchemeURLCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnOpenPopupCallback(llCefBrowserHandle handle, std::function<void(const std::string&, const std::string&)> callback)
{
    mImpl->SetOnOpenPopupCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnPageSourceRetrievedCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback, size_t maxBytes)
{
    mImpl->SetOnPageSourceRetrievedCallback(handle, std::move(callback), maxBytes);
}

void llCefBrowserManager::SetOnJSDialogCallback(llCefBrowserHandle handle, std::function<bool(const std::string&, llCefJSDialogType, const std::string&, const std::string&)> callback)
{
    mImpl->SetOnJSDialogCallback(handle, std::move(callback));
}

void llCefBrowserManager::SetOnBeforeUnloadCallback(llCefBrowserHandle handle, std::function<bool(const std::string&, bool)> callback)
{
    mImpl->SetOnBeforeUnloadCallback(handle, std::move(callback));
}

void llCefBrowserManager::SendMouseClickEvent(llCefBrowserHandle handle, int x, int y, llCefMouseButton button, bool mouseUp, int clickCount)
{
    mImpl->SendMouseClickEvent(handle, x, y, button, mouseUp, clickCount);
}

void llCefBrowserManager::SendMouseMoveEvent(llCefBrowserHandle handle, int x, int y, bool mouseLeave)
{
    mImpl->SendMouseMoveEvent(handle, x, y, mouseLeave);
}

void llCefBrowserManager::SendMouseWheelEvent(llCefBrowserHandle handle, int x, int y, int deltaY)
{
    mImpl->SendMouseWheelEvent(handle, x, y, deltaY);
}

void llCefBrowserManager::SendKeyEvent(llCefBrowserHandle handle, uint32_t message, uint64_t wParam, int64_t lParam)
{
    mImpl->SendKeyEvent(handle, message, wParam, lParam);
}

void llCefBrowserManager::SetFocus(llCefBrowserHandle handle, bool focus)
{
    mImpl->SetFocus(handle, focus);
}

void llCefBrowserManager::ShowDevTools(llCefBrowserHandle handle)
{
    mImpl->ShowDevTools(handle);
}

void llCefBrowserManager::ExecuteJavaScript(llCefBrowserHandle handle, const std::string& code)
{
    mImpl->ExecuteJavaScript(handle, code);
}

void llCefBrowserManager::GetPageSource(llCefBrowserHandle handle, std::function<void(const std::string&)> callback)
{
    mImpl->GetPageSource(handle, std::move(callback));
}

size_t llCefBrowserManager::LiveBrowserCount() const
{
    return mImpl->LiveBrowserCount();
}

void llCefBrowserManager::ForEachBrowser(const std::function<void(llCefBrowserHandle)>& fn)
{
    mImpl->ForEachBrowser(fn);
}

void llCefBrowserManager::Tick()
{
    mImpl->Tick();
}
