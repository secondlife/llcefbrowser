/**
 *
 * @file llCefBrowserLib.cpp
 * @brief Implementation of llCefBrowserLib's process-wide CEF bootstrap and message loop pump.
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

#include "llCefBrowserLib.h"
#include "llCefBrowser.h"
#include "llCefBrowserJavaScriptBridge.h"
#include "llCefBrowserLibInitOptionsAccess.h"
#include "llCefBrowserMessageRouterAccess.h"
#include "llCefBrowserVersion.h"
#include "include/cef_app.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_v8.h"
#include "include/wrapper/cef_message_router.h"

#if defined(WIN32)
#include <windows.h>
#endif

#include <memory>
#include <unordered_map>

namespace {

    CefRefPtr<CefMessageRouterBrowserSide> mMessageRouter;
    llCefBrowserJavaScriptBridge* mJavaScriptBridge = nullptr;
    std::unordered_map<int64_t, CefRefPtr<CefMessageRouterBrowserSide::Callback>> mPendingQueries;
    llCefBrowserLibInitOptions mInitOptions;

    llCefBrowser* GetLLBrowser(CefRefPtr<CefBrowser> browser)
    {
        if (browser && browser->GetHost())
        {
            if (CefRefPtr<CefClient> client = browser->GetHost()->GetClient())
            {
                // Safe: every browser this library creates is handed an
                // llCefBrowser as its CefClient (see llCefBrowserManagerImpl::
                // CreateBrowser) - this cast never sees any other CefClient
                // implementation.
                return static_cast<llCefBrowser*>(client.get());
            }
        }
        return nullptr;
    }

    cef_log_severity_t ToCefLogSeverity(llCefLogSeverity severity)
    {
        switch (severity)
        {
            case llCefLogSeverity::Verbose:
                return LOGSEVERITY_VERBOSE;
            case llCefLogSeverity::Info:
                return LOGSEVERITY_INFO;
            case llCefLogSeverity::Warning:
                return LOGSEVERITY_WARNING;
            case llCefLogSeverity::Error:
                return LOGSEVERITY_ERROR;
            case llCefLogSeverity::Fatal:
                return LOGSEVERITY_FATAL;
            case llCefLogSeverity::Disable:
                return LOGSEVERITY_DISABLE;
            case llCefLogSeverity::Default:
            default:
                return LOGSEVERITY_DEFAULT;
        }
    }

    llCefBrowserHandle HandleForBrowser(CefRefPtr<CefBrowser> browser)
    {
        if (llCefBrowser* llBrowser = GetLLBrowser(browser))
        {
            return llBrowser->GetHandle();
        }
        return llCefBrowserHandle::Invalid();
    }

    // Browser-process side of the JS bridge. Forwards window.cefQuery(...)
    // calls to whatever llCefBrowserJavaScriptBridge the application has registered via
    // llCefBrowserLib::SetJavaScriptBridge().
    class JsBridgeRouterHandler : public CefMessageRouterBrowserSide::Handler
    {
        public:
            bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         int64_t queryId, const CefString & request, bool persistent,
                         CefRefPtr<Callback> callback) override {
                llCefBrowser* llBrowser = GetLLBrowser(browser);

                // Fires regardless of whether a llCefBrowserJavaScriptBridge is
                // registered or ends up handling this request - an independent,
                // simpler observation path alongside the full bridge.
                if (llBrowser)
                {
                    llBrowser->NotifyOnQuery(request.ToString());
                }

                if (! mJavaScriptBridge) return false;

                // Registered before invoking the application's bridge - a bridge
                // that responds synchronously (the common case; see
                // llCefBrowserJavaScriptBridge::OnQuery) calls RespondToQuery()
                // from inside the OnQuery() call below, which needs to find this
                // entry already present to do anything.
                mPendingQueries[queryId] = callback;

                const llCefBrowserHandle handle = llBrowser ? llBrowser->GetHandle() : llCefBrowserHandle::Invalid();
                const bool handled = mJavaScriptBridge->OnQuery(handle, queryId, request.ToString(), persistent);
                if (! handled)
                {
                    // Never responded to and never will be - CEF's own router
                    // reports the failure to the page, so nothing here needs to
                    // outlive this call.
                    mPendingQueries.erase(queryId);
                }
                return handled;
            }

            void OnQueryCanceled(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                 int64_t queryId) override {
                mPendingQueries.erase(queryId);
                if (mJavaScriptBridge)
                {
                    mJavaScriptBridge->OnQueryCanceled(HandleForBrowser(browser), queryId);
                }
            }
    };

    std::unique_ptr<JsBridgeRouterHandler> mHandler;

    // Renderer-process side of the JS bridge. CEF re-executes the same
    // executable as a renderer (and GPU/utility) subprocess, and queries
    // LLDefaultApp::GetRenderProcessHandler() in that context - this class is
    // where window.cefQuery actually gets injected into the page's JS context.
    // It's inert (never touched) when running as the main browser process.
    class LLRenderProcessHandler : public CefRenderProcessHandler
    {
        public:
            void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefV8Context> context) override {
                EnsureRouter();
                mRendererRouter->OnContextCreated(browser, frame, context);
            }

            void OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefV8Context> context) override {
                if (mRendererRouter)
                {
                    mRendererRouter->OnContextReleased(browser, frame, context);
                }
            }

            bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                          CefProcessId sourceProcess,
                                          CefRefPtr<CefProcessMessage> message) override {
                return mRendererRouter &&
                mRendererRouter->OnProcessMessageReceived(browser, frame, sourceProcess, message);
            }

        private:
            void EnsureRouter()
            {
                if (! mRendererRouter)
                {
                    // Default CefMessageRouterConfig (js_query_function=
                    // "cefQuery", js_cancel_function="cefQueryCancel") - must
                    // match the browser-side config in Initialize() below. Both
                    // sides use the same untouched defaults, so they agree
                    // automatically without needing to share state across the
                    // process boundary.
                    CefMessageRouterConfig config;
                    mRendererRouter = CefMessageRouterRendererSide::Create(config);
                }
            }

            CefRefPtr<CefMessageRouterRendererSide> mRendererRouter;
            IMPLEMENT_REFCOUNTING(LLRenderProcessHandler);
    };

    // Shared by both ExecuteSubProcess() (renderer/GPU/utility processes) and
    // Initialize() (main browser process) - CEF only ever calls
    // GetRenderProcessHandler() in a process where it's meaningful.
    class LLDefaultApp : public CefApp
    {
        public:
            CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
                if (! mRenderProcessHandler)
                {
                    mRenderProcessHandler = new LLRenderProcessHandler();
                }
                return mRenderProcessHandler;
            }

            void OnBeforeCommandLineProcessing(const CefString & processType,
                                               CefRefPtr<CefCommandLine> commandLine) override {
                // Windowless/OSR browsers have no real OS window, so this host never calls
                // WasHidden(false) or anything else that would tell Chromium's occlusion/
                // backgrounding heuristics the view is actually on-screen and in use. Left
                // alone, Chromium can judge the renderer "backgrounded" and heavily throttle
                // requestAnimationFrame/JS timers/CSS animations to save power - which shows
                // up as smooth-scroll and other animations stalling for hundreds of ms to
                // multiple seconds, exactly as if the page itself were slow, even though a
                // real windowed browser tab (always unambiguously visible) never throttles
                // and runs the identical page instantly. disable-background-timer-throttling
                // in particular is read by Blink's own scheduler inside the renderer process,
                // so unlike the browser-process-only switches below, these three must be
                // applied for every process type, not just processType.empty().
                commandLine->AppendSwitch("disable-backgrounding-occluded-windows");
                commandLine->AppendSwitch("disable-renderer-backgrounding");
                commandLine->AppendSwitch("disable-background-timer-throttling");

                if (processType.empty())
                {
                    // Chrome's own built-in login prompt UI intercepts HTTP auth challenges
                    // before CefRequestHandler::GetAuthCredentials ever gets a chance to
                    // run - a known CEF behavior (see chromiumembedded/cef#3603) that
                    // otherwise makes that callback silently never fire, regardless of
                    // whether the browser itself renders windowless/Alloy-style. Disabling
                    // it here, once, for the browser process only (process_type is empty
                    // there - this switch would be meaningless, and per the docs
                    // potentially unsafe, to apply to renderer/GPU/utility processes).
                    commandLine->AppendSwitch("disable-chrome-login-prompt");

                    // Hides the browser permission infobar or popup dialog asking for camera or microphone access.
                    // Uses Defaults: Automatically selects your system's default media input devices.
                    commandLine->AppendSwitch("use-fake-ui-for-media-stream");

                    // macOS: Prevents Password Prompts: It stops the OS from throwing biometric or
                    // password authentication prompts during automated tests.
                    commandLine->AppendSwitch("use-mock-keychain");

                    // Disables browser media restrictions, allowing audio and video to play automatically
                    // without requiring a user click or tap - required for media which cannot be
                    // interacted with
                    commandLine->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");

                    // Fixed for the process lifetime -- mInitOptions is already
                    // populated here since Initialize() sets it before calling
                    // CefInitialize(), which is what triggers this callback.
                    if (! mInitOptions.proxyServer.empty())
                    {
                        commandLine->AppendSwitchWithValue("proxy-server", mInitOptions.proxyServer);
                    }
                    if (mInitOptions.noProxyServer)
                    {
                        commandLine->AppendSwitch("no-proxy-server");
                    }

                    if (mInitOptions.remoteDebuggingPort > 0)
                    {
                        // Chromium's DevTools frontend enforces an Origin check on its
                        // remote-debugging WebSocket connections (a real fix, not specific
                        // to us - see chromiumembedded/cef#3852 and the underlying Chromium
                        // change). Without this, the DevTools HTML page itself loads fine at
                        // http://localhost:<port>/... but every actual protocol message is
                        // rejected, which is indistinguishable from the outside from "nothing
                        // rendered" - a blank/black page, no error shown. "*" is fine here:
                        // this only ever binds to localhost (CEF's remote_debugging_port has
                        // no option to bind elsewhere), so it's not exposing anything over
                        // the network that wasn't already reachable by anyone on this machine.
                        commandLine->AppendSwitchWithValue("remote-allow-origins", "*");
                    }
                }
            }

        private:
            CefRefPtr<CefRenderProcessHandler> mRenderProcessHandler;
            IMPLEMENT_REFCOUNTING(LLDefaultApp);
    };

}  // namespace

CefRefPtr<CefMessageRouterBrowserSide> LLGetSharedMessageRouter()
{
    return mMessageRouter;
}

const llCefBrowserLibInitOptions& LLGetInitOptions()
{
    return mInitOptions;
}

namespace llCefBrowserLib {

    int ExecuteSubProcess(int argc, char** argv)
    {
#if defined(WIN32)
        CefMainArgs mainArgs(::GetModuleHandle(nullptr));
#else
        CefMainArgs mainArgs(argc, argv);
#endif
        CefRefPtr<CefApp> app = new LLDefaultApp();
        return CefExecuteProcess(mainArgs, app, nullptr);
    }

    bool Initialize(const llCefBrowserLibInitOptions& options)
    {
        mInitOptions = options;  // retained for llCefBrowserManagerImpl::CreateBrowser; see LLGetInitOptions()

#if defined(WIN32)
        CefMainArgs mainArgs(::GetModuleHandle(nullptr));
#else
        CefMainArgs mainArgs(0, nullptr);
#endif

        CefSettings settings;
        settings.windowless_rendering_enabled = true;
        settings.multi_threaded_message_loop = false;  // single-threaded pump model
        settings.no_sandbox = options.noSandbox;
        settings.command_line_args_disabled = options.commandLineArgsDisabled;

        if (! options.rootCachePath.empty())
        {
            CefString(&settings.root_cache_path).FromString(options.rootCachePath);
        }
        if (! options.cachePath.empty())
        {
            CefString(&settings.cache_path).FromString(options.cachePath);
        }
        settings.persist_session_cookies = options.persistSessionCookies;

        if (! options.userAgent.empty())
        {
            CefString(&settings.user_agent).FromString(options.userAgent);
        }
        else if (! options.userAgentProduct.empty())
        {
            CefString(&settings.user_agent_product).FromString(options.userAgentProduct);
        }
        if (! options.locale.empty())
        {
            CefString(&settings.locale).FromString(options.locale);
        }
        if (! options.acceptLanguageList.empty())
        {
            CefString(&settings.accept_language_list).FromString(options.acceptLanguageList);
        }

        if (! options.logFile.empty())
        {
            CefString(&settings.log_file).FromString(options.logFile);
        }
        settings.log_severity = ToCefLogSeverity(options.logSeverity);

        if (! options.javascriptFlags.empty())
        {
            CefString(&settings.javascript_flags).FromString(options.javascriptFlags);
        }
        settings.remote_debugging_port = options.remoteDebuggingPort;
        settings.uncaught_exception_stack_size = options.uncaughtExceptionStackSize;
        settings.background_color = options.backgroundColor;

        if (! options.cookieableSchemesList.empty())
        {
            CefString(&settings.cookieable_schemes_list).FromString(options.cookieableSchemesList);
        }
        settings.cookieable_schemes_exclude_defaults = options.cookieableSchemesExcludeDefaults;

        CefRefPtr<CefApp> app = new LLDefaultApp();
        if (! CefInitialize(mainArgs, settings, app, nullptr))
        {
            return false;
        }

        // Same defaults as LLRenderProcessHandler::EnsureRouter() above.
        CefMessageRouterConfig config;
        mMessageRouter = CefMessageRouterBrowserSide::Create(config);
        mHandler = std::make_unique<JsBridgeRouterHandler>();
        mMessageRouter->AddHandler(mHandler.get(), false);

        return true;
    }

    void DoMessageLoopWork()
    {
        CefDoMessageLoopWork();
    }

    void Shutdown()
    {
        if (mMessageRouter && mHandler)
        {
            mMessageRouter->RemoveHandler(mHandler.get());
        }
        mHandler.reset();
        mMessageRouter = nullptr;
        mJavaScriptBridge = nullptr;
        mPendingQueries.clear();
        CefShutdown();
    }

    void SetJavaScriptBridge(llCefBrowserJavaScriptBridge* bridge)
    {
        mJavaScriptBridge = bridge;
    }

    void RespondToQuery(int64_t queryId, bool success, const std::string& response, int errorCode)
    {
        auto it = mPendingQueries.find(queryId);
        if (it == mPendingQueries.end())
        {
            return;    // unknown, already-answered, or canceled
        }

        CefRefPtr<CefMessageRouterBrowserSide::Callback> callback = it->second;
        mPendingQueries.erase(it);

        if (success)
        {
            callback->Success(response);
        }
        else
        {
            callback->Failure(errorCode, response);
        }
    }

    std::string GetVersion()
    {
        return std::to_string(LLCEFBROWSER_VERSION_MAJOR) + "." +
               std::to_string(LLCEFBROWSER_VERSION_MINOR) + " (" +
               LLCEFBROWSER_VERSION_GITHASH + ")";
    }

    std::string GetCefVersion()
    {
        return std::to_string(CEF_VERSION_MAJOR) + "." +
               std::to_string(CEF_VERSION_MINOR) + "." +
               std::to_string(CEF_VERSION_PATCH);
    }

    std::string GetChromiumVersion()
    {
        return std::to_string(CHROME_VERSION_MAJOR) + "." +
               std::to_string(CHROME_VERSION_MINOR) + "." +
               std::to_string(CHROME_VERSION_BUILD) + "." +
               std::to_string(CHROME_VERSION_PATCH);
    }

    std::string GetRootCachePath()
    {
        return mInitOptions.rootCachePath;
    }

    std::string GetLogFilePath()
    {
        return mInitOptions.logFile;
    }

}  // namespace llCefBrowserLib
