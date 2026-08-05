/**
 *
 * @file llCefBrowserLibInitOptions.h
 * @brief CEF-free mirror of the CefSettings fields exposed by llCefBrowserLib::Initialize().
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
#include <cstdint>
#include <string>

// CEF-free mirror of the CefSettings fields this library exposes. Every
// field's default matches what CEF itself does when left unset (empty
// string / zero), so callers only need to set the values they actually
// want to override.
//
// A few CefSettings fields are intentionally not exposed here:
// - windowless_rendering_enabled and multi_threaded_message_loop are
// fixed by this library's own off-screen-rendering, single-threaded
// pump model; making them configurable would let a caller break
// invariants the rest of the API assumes.
// - browser_subprocess_path, framework_dir_path, main_bundle_path are
// macOS app-bundle paths, not relevant to this build.
// - chrome_policy_id, chrome_app_icon_id, use_views_default_popup are
// Chrome-style-window/enterprise-policy specific.
// - disable_signal_handlers is POSIX-only.
enum class llCefLogSeverity
{
    Default,
    Verbose,
    Info,
    Warning,
    Error,
    Fatal,
    Disable,  // no log file output; FATAL messages still go to stderr
};

// Mirrors CEF's own tri-state cef_state_t: Default defers to CEF's own
// behavior for the setting, Enabled/Disabled force it either way.
enum class llCefFeatureState
{
    Default,
    Enabled,
    Disabled,
};

struct llCefBrowserLibInitOptions
{
    bool noSandbox = true;
    bool commandLineArgsDisabled = false;

    // root_cache_path: parent directory for all profile data; empty = CEF's
    // own platform-specific default.
    std::string rootCachePath;
    // cache_path: the actual profile directory within rootCachePath; empty
    // = "incognito mode" (in-memory only, nothing persisted to disk).
    std::string cachePath;
    bool persistSessionCookies = false;

    // Full user agent string; if set, userAgentProduct below is ignored.
    std::string userAgent;
    // Product token only (e.g. "MyApp/1.0"); ignored if userAgent is set.
    std::string userAgentProduct;
    std::string locale;              // empty = "en-US"
    std::string acceptLanguageList;  // comma-delimited; empty = CEF default ("en-US,en")

    std::string logFile;  // empty = CEF's own default log file location
    llCefLogSeverity logSeverity = llCefLogSeverity::Default;

    std::string javascriptFlags;         // raw V8 flags, e.g. "--expose-gc"
    int remoteDebuggingPort = 1964;         // 0 = disabled; 1024-65535 to enable chrome://inspect
    int uncaughtExceptionStackSize = 0;  // 0 = OnUncaughtException() disabled

    // ARGB background color shown before a document loads. Alpha must be
    // 0x00 (transparent) or 0xFF (opaque); 0 = CEF's own default behavior.
    // Applied to every browser this library creates (CefBrowserSettings) as
    // well as to CefSettings itself - the CefSettings copy only matters for
    // windowed browsers, which this library never creates, but is set for
    // completeness.
    uint32_t backgroundColor = 0xffffffff;  // default is opaque white for pages that do not set it themselves

    std::string cookieableSchemesList;  // comma-delimited; empty + exclude=false = http/https/ws/wss only
    bool cookieableSchemesExcludeDefaults = false;

    // "host:port" (or a scheme-qualified form like "socks5://host:port");
    // applied via the "proxy-server" command-line switch, so it's fixed for
    // the lifetime of the process and must be set before Initialize() is
    // called. Empty = system/OS proxy settings, no switch appended. For a
    // proxy that can change at runtime or differ per llCefBrowserManager,
    // use CefRequestContext::SetPreference("proxy", ...) instead -- not
    // exposed here since it needs a CEF dictionary value, not a plain string.
    std::string proxyServer;

    // Forces direct connections, ignoring even the system/OS proxy config --
    // for when you explicitly want no proxy at all rather than deferring to
    // whatever the system has configured. Applied via the "no-proxy-server"
    // command-line switch, which per Chromium's own docs overrides
    // proxyServer above if both are set.
    bool noProxyServer = true;

    // The following apply per-browser (CefBrowserSettings) rather than
    // process-wide, but are read from this same, single options instance -
    // this library has no per-browser settings override, so every browser
    // it creates shares one configuration.
    int windowlessFrameRate = 60;  // fps for CefRenderHandler::OnPaint; CEF's own default is 30
    llCefFeatureState javascript = llCefFeatureState::Default;
    llCefFeatureState javascriptAccessClipboard = llCefFeatureState::Default;
    llCefFeatureState imageLoading = llCefFeatureState::Default;
    llCefFeatureState localStorage = llCefFeatureState::Default;
    llCefFeatureState webgl = llCefFeatureState::Default;
};
