/**
 *
 * @file llCefBrowserLib.h
 * @brief Process-wide CEF bootstrap, message loop pump, and JavaScript bridge registration.
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
#include "llCefBrowserLibInitOptions.h"
#include <cstdint>
#include <string>

class llCefBrowserJavaScriptBridge;

// Wraps CEF's process bootstrap and message loop so consuming applications
// never need to include a CEF header or know about CefMainArgs/CefSettings/
// CefApp/CefMessageRouter themselves.

namespace llCefBrowserLib {

    // Call as the FIRST thing in main(), before any other CEF or windowing
    // code. Returns >= 0 if this process is a CEF subprocess (renderer/GPU/
    // utility/etc.) - the caller must return that value from main()
    // immediately in that case. Returns -1 if this is the main browser process
    // and startup should continue normally.
    int ExecuteSubProcess(int argc, char** argv);

    // Initializes CEF for the main browser process. Call once, only after
    // ExecuteSubProcess() has returned -1.
    bool Initialize(const llCefBrowserLibInitOptions& options);

    // Pumps CEF's internal message loop. Call once per frame from your own
    // render/main loop (this library uses CEF's single-threaded message loop
    // model, so nothing happens on CEF's behalf unless you call this).
    void DoMessageLoopWork();

    // Shuts CEF down. Call after every llCefBrowserManager has been destroyed (or
    // at minimum after llCefBrowserManager::DestroyAll() plus a few
    // DoMessageLoopWork() pumps to let the async close handshakes finish) and
    // immediately before process exit.
    void Shutdown();

    // Registers the handler for window.cefQuery(...) calls from JS content
    // running in any browser this process creates. Applies process-wide.
    // Pass nullptr to unregister. Not owned - the caller keeps bridge alive
    // for as long as it stays registered (typically until Shutdown()).
    void SetJavaScriptBridge(llCefBrowserJavaScriptBridge* bridge);

    // Responds to a pending query previously received via
    // llCefBrowserJavaScriptBridge::OnQuery. Safe to call from within OnQuery itself
    // (synchronous response) or later, on the CEF UI thread, for an
    // asynchronous one. A no-op if queryId is unknown (e.g. already responded
    // to, or canceled via llCefBrowserJavaScriptBridge::OnQueryCanceled).
    void RespondToQuery(int64_t queryId, bool success,
                        const std::string& response, int errorCode = 0);

    // Version queries. Each returns a compile-time constant, so all three are
    // safe to call at any point, including before Initialize().
    std::string GetVersion();           // this library's own version, e.g. "0.35 (8926996)"
    std::string GetCefVersion();        // CEF version built against, e.g. "151.3.14"
    std::string GetChromiumVersion();   // Chromium version built against, e.g. "151.0.7922.72"

    // Echoes back the process-wide paths passed to Initialize() via
    // llCefBrowserLibInitOptions, for diagnostics/UI (e.g. an About box)
    // where the original llCefBrowserLibInitOptions is no longer in scope.
    // Exactly what was passed in, not CEF's resolved default when left
    // empty. Empty if Initialize() hasn't been called yet.
    std::string GetRootCachePath();
    std::string GetLogFilePath();

}  // namespace llCefBrowserLib
