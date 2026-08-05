/**
 *
 * @file llCefBrowserManagerImpl.h
 * @brief Internal implementation class behind llCefBrowserManager's pimpl interface.
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
#include "include/cef_request_context.h"
#include "llCefBrowser.h"
#include "llCefBrowserHandle.h"
#include <functional>
#include <string>
#include <vector>

// Internal implementation behind llCefBrowserManager's public pimpl
// interface. Never included by consuming applications - owns every
// offscreen browser created through it and one shared CefRequestContext,
// so all browsers share a single on-disk cache / cookie jar. Designed for
// rapid create/destroy churn: handles are (index, generation) pairs into a
// recycled slot vector.
class llCefBrowserManagerImpl {
    public:
        explicit llCefBrowserManagerImpl(const std::string& cachePath);
        ~llCefBrowserManagerImpl();

        llCefBrowserHandle CreateBrowser(const std::string& url, int width, int height);
        void DestroyBrowser(llCefBrowserHandle handle);
        bool DestroyLastBrowser();
        void DestroyAll();

        bool IsValid(llCefBrowserHandle handle) const;
        llCefBrowser* Get(llCefBrowserHandle handle);
        const llCefBrowser* Get(llCefBrowserHandle handle) const;

        void SetUserData(llCefBrowserHandle handle, void* userData);
        void* GetUserData(llCefBrowserHandle handle);

        // The cache directory this manager was constructed with, exactly as
        // passed to the constructor (empty for in-memory-only mode).
        const std::string& GetCachePath() const;

        int GetWidth(llCefBrowserHandle handle) const;
        int GetHeight(llCefBrowserHandle handle) const;

        void ResizeBrowser(llCefBrowserHandle handle, int width, int height, bool clearImmediately);

        bool CopyLatestFrame(llCefBrowserHandle handle, std::vector<uint8_t>& dst, int& w, int& h);

        bool CanGoBack(llCefBrowserHandle handle) const;
        void GoBack(llCefBrowserHandle handle);
        bool CanGoForward(llCefBrowserHandle handle) const;
        void GoForward(llCefBrowserHandle handle);
        bool IsLoading(llCefBrowserHandle handle) const;
        void Reload(llCefBrowserHandle handle, bool ignoreCache);
        void StopLoad(llCefBrowserHandle handle);
        void Navigate(llCefBrowserHandle handle, const std::string& url);

        void Undo(llCefBrowserHandle handle);
        void Redo(llCefBrowserHandle handle);
        void Copy(llCefBrowserHandle handle);
        void Cut(llCefBrowserHandle handle);
        void Paste(llCefBrowserHandle handle);
        void Delete(llCefBrowserHandle handle);
        void SelectAll(llCefBrowserHandle handle);

        void SetPageZoom(llCefBrowserHandle handle, float zoomLevel);

        void SetCookie(const std::string& url, const std::string& name, const std::string& value,
                       const std::string& domain, const std::string& path, bool httpOnly, bool secure,
                       std::function<void(bool)> callback);
        void GetCookies(std::function<void(const std::vector<llCefCookie>&)> callback);
        void DeleteAllCookies(std::function<void(int)> callback);

        void SetOnPageChangedCallback(llCefBrowserHandle handle, std::function<void()> callback);
        void SetOnCursorChangedCallback(llCefBrowserHandle handle, std::function<void(llCefCursorType)> callback);
        void SetOnAddressChangeCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback);
        void SetOnTitleChangeCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback);
        void SetOnStatusMessageCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback);
        void SetOnConsoleMessageCallback(llCefBrowserHandle handle, std::function<void(const std::string&, const std::string&, int)> callback);
        void SetOnTooltipCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback);
        void SetOnLoadStartCallback(llCefBrowserHandle handle, std::function<void()> callback);
        void SetOnLoadEndCallback(llCefBrowserHandle handle, std::function<void(int)> callback);
        void SetOnLoadErrorCallback(llCefBrowserHandle handle, std::function<void(int, const std::string&, const std::string&)> callback);
        void SetOnQueryCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback);
        void SetOnFileDialogCallback(llCefBrowserHandle handle, std::function<void(int64_t, llCefFileDialogMode, const std::string&, const std::string&, const std::vector<std::string>&)> callback);
        void RespondToFileDialog(llCefBrowserHandle handle, int64_t dialogId, const std::vector<std::string>& filePaths);
        void SetOnAuthRequestCallback(llCefBrowserHandle handle, std::function<bool(const std::string&, const std::string&, int, const std::string&, const std::string&, bool, std::string&, std::string&)> callback);
        void SetOnCustomSchemeURLCallback(llCefBrowserHandle handle, std::function<void(const std::string&, bool, bool)> callback);
        void SetOnPageSourceRetrievedCallback(llCefBrowserHandle handle, std::function<void(const std::string&)> callback, size_t maxBytes);
        void SetOnJSDialogCallback(llCefBrowserHandle handle, std::function<bool(const std::string&, llCefJSDialogType, const std::string&, const std::string&)> callback);
        void SetOnBeforeUnloadCallback(llCefBrowserHandle handle, std::function<bool(const std::string&, bool)> callback);

        void SendMouseClickEvent(llCefBrowserHandle handle, int x, int y, llCefMouseButton button, bool mouseUp, int clickCount);
        void SendMouseMoveEvent(llCefBrowserHandle handle, int x, int y, bool mouseLeave);
        void SendMouseWheelEvent(llCefBrowserHandle handle, int x, int y, int deltaY);
        void SendKeyEvent(llCefBrowserHandle handle, uint32_t message, uint64_t wParam, int64_t lParam);
        void SetFocus(llCefBrowserHandle handle, bool focus);
        void ShowDevTools(llCefBrowserHandle handle);
        void ExecuteJavaScript(llCefBrowserHandle handle, const std::string& code);

        size_t LiveBrowserCount() const;

        // Note the narrower callback vs. the internal llCefBrowser type: the
        // public llCefBrowserManager::ForEachBrowser only ever hands out a
        // llCefBrowserHandle, never a reference to this CEF-facing class.
        void ForEachBrowser(const std::function<void(llCefBrowserHandle)>& fn);

        void Tick();

        // Called by llCefBrowser::OnBeforeClose to complete phase 2 of
        // destruction.
        void NotifyBrowserClosed(llCefBrowserHandle handle);

    private:
        struct Slot
        {
            CefRefPtr<llCefBrowser> mBrowser;
            uint32_t mGeneration = 0;
            bool mClosing = false;
        };

        CefRefPtr<CefRequestContext> mSharedContext;
        std::vector<Slot> mSlots;
        std::vector<uint32_t> mFreeList;
        std::string mCachePath;
};
