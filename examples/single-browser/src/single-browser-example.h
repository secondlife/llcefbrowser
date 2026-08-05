/**
 *
 * @file single-browser-example.h
 * @brief App class and JavaScript bridge for the single-browser example.
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

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"

#include <glad/glad.h>
#if defined(WIN32)
#undef APIENTRY
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <commctrl.h>
#else
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

#include <iostream>
#include <memory>
#include <string>

#include "llCefBrowserLib.h"
#include "llCefBrowserJavaScriptBridge.h"

#include <nlohmann/json.hpp>

class llCefBrowserManager;
class llCefBrowserHandle;

// JS bridge for this app: web content in any browser can call
// window.cefQuery({
//     request: 'ping',
//     onSuccess: (response) => console.log(response),
//     onFailure: (code, msg) => console.error(msg),
// });
// and this responds synchronously. Real bridges will often store queryId
// and respond later (e.g. once an async operation completes) via
// llCefBrowserLib::RespondToQuery() instead of responding inline here.
//
// request/response are plain strings as far as the bridge is concerned -
// the JSON handling below is entirely this app's choice, not something the
// library imposes. A page can send a JSON-encoded request such as
// window.cefQuery({ request: JSON.stringify({ op: 'add', a: 3, b: 4 }), ... })
// and this parses it, computes a result, and replies with a JSON string
// that the page then JSON.parse()s in onSuccess.
class AppJavaScriptBridge : public llCefBrowserJavaScriptBridge {
    public:
        bool OnQuery(llCefBrowserHandle handle, int64_t queryId,
                     const std::string& request, bool persistent) override {

            std::cout << "JS query " << queryId << ": " << request << std::endl;
            if (request == "ping") {
                llCefBrowserLib::RespondToQuery(queryId, true, "pong");
                return true;
            }

            nlohmann::json parsed = nlohmann::json::parse(request, nullptr, false);
            if (parsed.is_discarded() || ! parsed.is_object()) {
                return false;  // not JSON we understand - CEF reports failure to the page
            }

            const std::string op = parsed.value("op", "");
            if (op == "add" && parsed.contains("a") && parsed.contains("b")) {
                nlohmann::json result = {{"sum", parsed["a"].get<double>() + parsed["b"].get<double>()}};
                llCefBrowserLib::RespondToQuery(queryId, true, result.dump());
                return true;
            }

            return false;  // not handled - CEF reports failure to the page
        }

        void OnQueryCanceled(llCefBrowserHandle handle, int64_t queryId) override {
            std::cout << "JS query " << queryId << " was canceled" << std::endl;
        }
};

class singleBrowser {
    public:
        singleBrowser();
        ~singleBrowser();

        void init();
        void initGLFWCallbacks();
        void initCEFCallbacks();
        void run();
        void reset();
        void update();
        void draw();

        // GLFW callbacks
        void resizeCallback(int width, int height);
        void keyCallback(int key, int scancode, int action, int mods);
        void mouseButtonCallback(int button, int action, int mods);
        void mouseMoveCallback(double xpos, double ypos);
        void mouseScrollCallback(double xoffset, double yoffset);
        void windowFocusCallback(int focused);

        // IMGUI based UI
        void initUI();
        void updateUI();
        void resetUI();
        int getUIHeight();
        int scaleMouseYforUI(double raw_y);

    private:
        GLFWwindow* mWindow;
        const std::string mWindowTitle = "llCefBrowser: Single Browser Example";
        const std::string mAppVersionStr = "1.0.53";
        const std::string mHomeUrl = "https://sl-viewer-media-system.s3.amazonaws.com/bookmarks/index.html";
        GLuint mTextureWidth;
        GLuint mTextureHeight;
        GLuint mTextureDepth;
        GLuint mTextureId;
        bool mShowAbout;

        // The size the GL texture named by mTextureId was actually last
        // allocated at - not necessarily mTextureWidth/mTextureHeight, which
        // resizeCallback() updates immediately on a window resize, ahead of
        // CEF actually delivering a frame at the new size. update() compares
        // an incoming frame's size against these to decide whether it needs
        // a full glTexImage2D reallocation or just a glTexSubImage2D update.
        int mTexAllocWidth = 0;
        int mTexAllocHeight = 0;

        std::unique_ptr<llCefBrowserManager> mCefBrowserManager;
        llCefBrowserHandle mCefBrowser;
        AppJavaScriptBridge mBridge;

        // Set by the OnPageChanged callback registered in
        // initCEFCallbacks(); update() clears it after pulling the frame.
        bool mPageChanged = false;

        // Used to marshall static function callbacks to a instance of the app class
        static void resizeCallbackStatic(GLFWwindow* window, int width, int height) {
            static_cast<singleBrowser*>(glfwGetWindowUserPointer(window))->resizeCallback(width, height);
        }
        static void keyCallbackStatic(GLFWwindow* window, int key, int scancode, int action, int mods) {
            static_cast<singleBrowser*>(glfwGetWindowUserPointer(window))->keyCallback(key, scancode, action, mods);
        }
        static void mouseButtonCallbackStatic(GLFWwindow* window, int button, int action, int mods) {
            static_cast<singleBrowser*>(glfwGetWindowUserPointer(window))->mouseButtonCallback(button, action, mods);
        }
        static void mouseMoveCallbackStatic(GLFWwindow* window, double xpos, double ypos) {
            static_cast<singleBrowser*>(glfwGetWindowUserPointer(window))->mouseMoveCallback(xpos, ypos);
        }
        static void mouseScrollCallbackStatic(GLFWwindow* window, double xoffset, double yoffset) {
            static_cast<singleBrowser*>(glfwGetWindowUserPointer(window))->mouseScrollCallback(xoffset, yoffset);
        }
        static void windowFocusCallbackStatic(GLFWwindow* window, int focused) {
            static_cast<singleBrowser*>(glfwGetWindowUserPointer(window))->windowFocusCallback(focused);
        }

#if defined(WIN32)
        // Windows specific handler for keyboard input - CEF needs raw OS keyboard messages
        static LRESULT CALLBACK keyEventSubClassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
#endif
};
