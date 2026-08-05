/**
 *
 * @file multiple-browsers-example.h
 * @brief App class and JavaScript bridge for the multiple-browsers example.
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
#include "llCefBrowserHandle.h"

#include <nlohmann/json.hpp>

class llCefBrowserManager;

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

class multipleBrowsers {
    public:
        multipleBrowsers();
        ~multipleBrowsers();

        void init();
        void initGLFWCallbacks();

        // Registers every llCefBrowserManager callback for one browser.
        // Unlike the single-browser example (where this ran once at
        // startup against the one browser that ever existed), callbacks
        // are per-handle - see include/llCefBrowserManager.h - so this
        // has to run once per handle, called from createBrowserTab()
        // right after CreateBrowser() succeeds, not once globally.
        void initCEFCallbacks(llCefBrowserHandle handle);

        void run();
        void reset();
        void update();
        void draw();

        void createBrowserTab(const std::string& url, int width, int height);
        void destroyBrowserTab();

        // Ray/plane hit test against every live browser's quad, using the
        // exact same camera and grid-layout math as draw(). Returns the
        // browser under (mouseX, mouseY) - in GLFW window coordinates - and
        // the corresponding browser-local pixel coordinates, or false if the
        // ray hits no tile. Used only to decide what a click selects - once
        // a browser is selected, mouse input is routed to it regardless of
        // where the cursor physically is (see localCoordsForHandle below).
        bool pickBrowser(double mouseX, double mouseY, llCefBrowserHandle& outHandle, int& outLocalX, int& outLocalY) const;

        // Where (mouseX, mouseY) projects to in a specific browser's local
        // pixel space, even if that point falls outside the browser's own
        // tile - the returned coordinates can be negative or beyond the
        // browser's width/height, same as real OS mouse capture delivering
        // out-of-bounds coordinates while dragging outside a view. Returns
        // false only if the mouse ray doesn't hit the scene's ground plane
        // at all, or the handle isn't currently live.
        bool localCoordsForHandle(llCefBrowserHandle handle, double mouseX, double mouseY, int& outLocalX, int& outLocalY) const;

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

    private:
        GLFWwindow* mWindow;
        int mWindowWidth;
        int mWindowHeight;
        const std::string mWindowTitle = "llCefBrowser: Multiple Browsers Example";
        const std::string mAppVersionStr = "1.0.23";
        const std::string mHomeUrl = "https://sl-viewer-media-system.s3.amazonaws.com/bookmarks/index.html";
        //const std::string mHomeUrl = "https://secondlife.com";
        GLuint mDefaultBrowserWidth;
        GLuint mDefaultBrowserHeight;

        // Hard cap on live browser instances - each one is a real CEF
        // browser process/renderer, so spamming Ctrl+Numpad+ (or the New
        // Tab menu item) shouldn't be able to spawn an unbounded number.
        const size_t mMaxBrowsers = 25;

        // Perspective projection parameters - shared between resizeCallback()
        // (which builds the projection matrix) and pickBrowser() (which
        // rebuilds the same frustum on the CPU to unproject the mouse), so
        // the two can never drift out of sync with each other.
        const double mFov = 60.0;
        const double mNearPlane = 0.1;
        const double mFarPlane = 100.0;

        std::unique_ptr<llCefBrowserManager> mCefBrowserManager;
        AppJavaScriptBridge mBridge;

        // The browser tab that gets the red selection border, that
        // destroyBrowserTab() deletes, and that all mouse input is routed
        // to (regardless of where the cursor physically is on screen).
        // Set by pickBrowser() on click. Defaults to whichever tab was
        // most recently created.
        llCefBrowserHandle mSelectedHandle = llCefBrowserHandle::Invalid();

        // Toggled by the Help > About menu item in updateUI().
        bool mShowAbout = false;

        // Per-tile placement computed by draw()'s grid layout, reused by
        // pickBrowser() so hit testing always matches what's on screen.
        struct TileLayout
        {
            GLfloat x = 0.0f;
            GLfloat y = 0.0f;
            GLfloat z = 0.0f;
            GLfloat width_offset = 0.0f;
            GLfloat height_offset = 0.0f;
        };
        TileLayout computeTileLayout(size_t index, size_t num_x, size_t num_y, GLfloat spacing, llCefBrowserHandle handle) const;

        // Finds handle's current tile (its on-screen position/size can shift
        // frame to frame as browsers are added/removed and the grid
        // reflows), returning false if handle is no longer live.
        bool computeTileLayoutForHandle(llCefBrowserHandle handle, TileLayout& outTile) const;

        // Casts the mouse ray and intersects it with the scene's z=0 ground
        // plane (the plane every tile sits in, in object space) - shared by
        // pickBrowser() and localCoordsForHandle(). Returns false if the ray
        // is parallel to the plane or points away from it.
        bool computeGroundHit(double mouseX, double mouseY, double& outHitX, double& outHitY) const;

        // Camera controls
        double mCameraDist = -1.3;
        double mMouseOffsetX = 0.0;
        double mMouseOffsetY = 0.0;
        double mMouseOffsetStartX = 0.0;
        double mMouseOffsetStartY = 0.0;
        double mXRotationStart = 0;
        double mXRotation = 0;
        double mYRotationStart = 0;
        double mYRotation = 0;
        double mXPanStart = 0;
        double mXPan = 0;
        double mYPanStart = 0;
        double mYPan = 0;
        const double mZoomSensitivity = 10.0;
        const double mZoomMin = -20.0;
        const double mZoomMax = -0.2;

        // Set by the OnPageChanged callback registered in
        // initCEFCallbacks(); update() clears it after pulling the frame.
        //bool mPageChanged = false;

        // Tracked here rather than queried back from the library - the
        // API only takes a zoom level (SetPageZoom), it doesn't hand one
        // back, so incremental in/out/reset needs a local record of the
        // last value requested.
        float mPageZoom = 1.0f;

        // Used to marshall static function callbacks to a instance of the app class
        static void resizeCallbackStatic(GLFWwindow* window, int width, int height) {
            static_cast<multipleBrowsers*>(glfwGetWindowUserPointer(window))->resizeCallback(width, height);
        }
        static void keyCallbackStatic(GLFWwindow* window, int key, int scancode, int action, int mods) {
            static_cast<multipleBrowsers*>(glfwGetWindowUserPointer(window))->keyCallback(key, scancode, action, mods);
        }
        static void mouseButtonCallbackStatic(GLFWwindow* window, int button, int action, int mods) {
            static_cast<multipleBrowsers*>(glfwGetWindowUserPointer(window))->mouseButtonCallback(button, action, mods);
        }
        static void mouseMoveCallbackStatic(GLFWwindow* window, double xpos, double ypos) {
            static_cast<multipleBrowsers*>(glfwGetWindowUserPointer(window))->mouseMoveCallback(xpos, ypos);
        }
        static void mouseScrollCallbackStatic(GLFWwindow* window, double xoffset, double yoffset) {
            static_cast<multipleBrowsers*>(glfwGetWindowUserPointer(window))->mouseScrollCallback(xoffset, yoffset);
        }
        static void windowFocusCallbackStatic(GLFWwindow* window, int focused) {
            static_cast<multipleBrowsers*>(glfwGetWindowUserPointer(window))->windowFocusCallback(focused);
        }

#if defined(WIN32)
        // Windows specific handler for keyboard input - CEF needs raw OS keyboard messages
        static LRESULT CALLBACK keyEventSubClassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
#endif
};
