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
// - chrome_policy_id, chrome_app_icon_id, use_views_default_popup are
// Chrome-style-window/enterprise-policy specific.
// - disable_signal_handlers is POSIX-only.
//
// resourcesDirPath/frameworkDirPath/mainBundlePath/browserSubprocessPath ARE
// exposed (unlike the macOS-only fields above) even though CEF's own docs
// describe them purely in app-bundle terms: left empty, CEF requires
// *.pak/icudtl.dat to live in the app bundle's own Resources directory and
// the framework at "Contents/Frameworks/Chromium Embedded Framework.framework"
// -- neither applies to a consumer that isn't (or doesn't want to be) a real
// .app bundle, e.g. a plain command-line tool. Empty (the default) keeps
// CEF's own bundle-relative auto-detection for a caller that IS bundled.
// browserSubprocessPath specifically: on Windows/Linux, an empty value
// already means "re-exec this same executable" (the single-executable
// model llCefBrowserLib::ExecuteSubProcess() itself relies on) -- but on
// macOS, empty instead means "look for Contents/Frameworks/<app>
// Helper.app", so a non-bundled mac caller wanting that same single-
// executable behavior must set this explicitly to its own executable path
// (confirmed the hard way: every CEF sub-process -- GPU, renderer, network
// -- fails to launch at all without it, on a binary with no bundled
// Helper.app).
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

    // macOS-relevant paths, all absolute if set, all empty by default (CEF's
    // own bundle-relative auto-detection) -- see this file's own top comment
    // for why these are exposed at all. Meaningless/ignored on other
    // platforms.
    std::string resourcesDirPath;  // dir containing *.pak files and icudtl.dat
    std::string frameworkDirPath;  // path to "...Chromium Embedded Framework.framework" itself
    std::string mainBundlePath;    // defaults to the top-level app bundle if empty
    std::string browserSubprocessPath;  // see this file's own top comment

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
    int remoteDebuggingPort = 0;         // 0 = disabled; 1024-65535 to enable chrome://inspect
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
    // A/B TESTED AND REFUTED (2026-11-11): temporarily dropped 60 -> 10 (a 6x
    // cut) to test whether DoMessageLoopWork()'s 50ms-1000ms+ stalls (once
    // ~9-10 browsers are live) scale with total composite-events/sec across
    // all live browsers. This is a creation-time CefBrowserSettings value
    // (llCefBrowserManagerImpl::CreateBrowser(), set once, never touched
    // again) -- NOT the same code path as the runtime SetFrameRate()/
    // SetWindowlessFrameRate() call that caused the connection-churn
    // regression reverted in bff3b8d825. Result: no meaningful improvement --
    // same live-tested repro (walk to the 6-prim area) still showed
    // continuous [phase2]/[iteration total] costs in the 50-200ms range at
    // 9-10 active slots, just smoothed (lower peak, higher floor) rather than
    // reduced. Composite/paint rate is not the driver. Left at 60 (the
    // shipped default).
    int windowlessFrameRate = 60;  // fps for CefRenderHandler::OnPaint; CEF's own default is 30

    // Which mechanism llCefBrowser::RequestRepaint() uses to drive repainting,
    // and whether CefWindowInfo::external_begin_frame_enabled is set at
    // browser creation (see llCefBrowserManagerImpl::CreateBrowser()'s own
    // comment for the full history). true (the default again as of
    // 2026-11-12, reverting the 2026-11-10 change below): RequestRepaint()
    // calls CefBrowserHost::SendExternalBeginFrame(), and CEF never composites
    // except in response to that call -- this is what lets the distance/
    // priority render-throttle (kSetRenderRate) actually reduce a background
    // browser's real CEF-internal paint rate, by simply calling this less
    // often, with no further runtime API needed. Known, accepted, already-
    // filed-upstream cost: this mode breaks DOM hit-testing/click on WebGL
    // canvas content (confirmed repro: Google Maps POI markers).
    //
    // false (the default 2026-11-10 through 2026-11-12, to fix that Maps
    // bug): RequestRepaint() calls CefBrowserHost::Invalidate() instead, and
    // CEF's own internal windowless_frame_rate timer composites every live
    // browser regardless of priority or how often Invalidate() is called --
    // this silently disabled the render-throttle's real CPU/GPU-saving effect
    // (kSetRenderRate's tier labels became cosmetic-only), and without a
    // working per-tier substitute (SetFrameRate()/SetHidden() were each tried
    // and reverted, see their own now-deleted history in this file and in
    // llCefBrowserManager.h), every live browser compositing at full rate
    // regardless of relevance turned out to be what actually destabilized the
    // producer once ~9-10 browsers are alive (CefRenderWidgetHostViewOSR::
    // OnPaint serializing on the single browser-process UI thread -- see the
    // Viewer-side investigation notes). A clean A/B test checking out the
    // Viewer from 2026-11-04 (before this mode was ever touched, EBF simply
    // on throughout) confirmed that build is completely stable under the
    // same repro that destabilizes this one with the mode off -- i.e. this
    // mode being on was never the problem; losing the render-throttle's real
    // effect when it went off, with nothing adequate replacing it, was.
    //
    // Exists as an explicit, named toggle (not just deleted outright) so this
    // can still be A/B tested -- e.g. against a future CEF version, or to
    // help narrow down the upstream hit-test bug further -- with everything
    // else (pacing, call site) held identical between the two modes.
    bool externalBeginFrameEnabled = true;
    llCefFeatureState javascript = llCefFeatureState::Default;
    llCefFeatureState javascriptAccessClipboard = llCefFeatureState::Default;
    llCefFeatureState imageLoading = llCefFeatureState::Default;
    llCefFeatureState localStorage = llCefFeatureState::Default;
    llCefFeatureState webgl = llCefFeatureState::Default;
};
