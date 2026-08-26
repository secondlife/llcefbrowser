/**
 *
 * @file multiple-browsers-example.cpp
 * @brief Implementation of the multiple-browsers example: several CEF browsers rendered as tiles in an orbitable OpenGL scene.
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

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <vector>
#include <random>

#include "llCefBrowserManager.h"
#include "llCefBrowserLib.h"
#include "llCefBrowserJavaScriptBridge.h"
#include "llCefBrowserLibDebug.h"

#include "multiple-browsers-example.h"

namespace {
    // Minimal vector/rotation helpers for pickBrowser()'s ray/plane test -
    // just enough to invert the exact transform draw() applies with
    // glTranslatef/glRotatef, without pulling in a full math library for a
    // handful of operations.
    struct Vec3
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    // Matches glRotatef(angleDeg, 1, 0, 0).
    Vec3 RotateX(const Vec3& v, double angleDeg)
    {
        const double rad = angleDeg * std::acos(-1.0) / 180.0;
        const double c = std::cos(rad);
        const double s = std::sin(rad);
        return Vec3{ v.x, v.y* c - v.z * s, v.y* s + v.z * c };
    }

    // Matches glRotatef(angleDeg, 0, 1, 0).
    Vec3 RotateY(const Vec3& v, double angleDeg)
    {
        const double rad = angleDeg * std::acos(-1.0) / 180.0;
        const double c = std::cos(rad);
        const double s = std::sin(rad);
        return Vec3{ v.x* c + v.z * s, v.y, -v.x* s + v.z * c };
    }

    // What this app stashes in each browser's opaque llCefBrowserManager
    // userData slot - the library itself never looks at this, it's purely
    // this app's own per-browser bookkeeping (see createBrowserTab(), which
    // allocates one of these per handle, and destroyBrowserTab()/reset(),
    // which free it).
    struct BrowserTileData
    {
        GLuint textureId = 0;
        int textureWidth = 0;
        int textureHeight = 0;
    };

    BrowserTileData* GetTileData(llCefBrowserManager& manager, llCefBrowserHandle handle)
    {
        return static_cast<BrowserTileData*>(manager.GetUserData(handle));
    }
}

multipleBrowsers::multipleBrowsers() :
    mWindow(nullptr),
    mWindowWidth(1280),
    mWindowHeight(1024),
    mDefaultBrowserWidth(1024),
    mDefaultBrowserHeight(1024),
    mCefBrowserManager(nullptr)
{
}

multipleBrowsers::~multipleBrowsers() = default;

static void errorCallback(int error, const char* description)
{
    LLCB_OUT_APP_INFO(description << " - code: " << error)
}

void multipleBrowsers::keyCallback(int key, int scancode, int action, int mods)
{
    // GetCurrentContext() must be checked before GetIO(): this callback is
    // registered in initGLFWCallbacks(), which runs before initUI() creates
    // the ImGui context, and Windows can synchronously deliver a queued
    // key/mouse message during window setup (e.g. glfwSetWindowPos) before
    // that context exists - GetIO() on a null context is undefined
    // behavior in a Release build, since IM_ASSERT compiles out with NDEBUG.
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard)
    {
        return;
    }

    if (action == GLFW_PRESS)
    {
        if (key == GLFW_KEY_ESCAPE)
        {
            glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
        }
        else if (mods & GLFW_MOD_CONTROL)
        {
            if (key == GLFW_KEY_KP_ADD)
            {
                LLCB_OUT_APP_INFO("Add a new browser surface")
                createBrowserTab(mHomeUrl, mDefaultBrowserWidth, mDefaultBrowserHeight);
            }
            else if (key == GLFW_KEY_KP_SUBTRACT)
            {
                LLCB_OUT_APP_INFO("Delete the selected browser surface")
                destroyBrowserTab();
            }
        }
    }
}

#if defined(WIN32)
// Windows subclass procedure for handling keyboard events using native
// Windows messages and parameters which is what CEF requires.
LRESULT CALLBACK multipleBrowsers::keyEventSubClassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_CHAR || uMsg == WM_KEYDOWN || uMsg == WM_KEYUP ||
            uMsg == WM_SYSCHAR || uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP)
    {
        // This hook operates on raw Win32 messages, entirely outside GLFW's
        // (and therefore ImGui's) callback system, so it has no other way
        // to know a text field like the URL bar currently wants the
        // keystroke instead of the browser - without this check, typing
        // into any ImGui widget also gets sent to the selected browser as
        // a key event. GetCurrentContext() guards against this firing
        // during reset(), after resetUI() has destroyed ImGui's context
        // but before RemoveWindowSubclass() has run.
        const bool imguiWantsKeyboard = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
        if (! imguiWantsKeyboard)
        {
            multipleBrowsers* parent = (multipleBrowsers*)dwRefData;
            if (parent->mSelectedHandle.IsValid())
            {
                parent->mCefBrowserManager->SendKeyEvent(parent->mSelectedHandle, uMsg, (uint64_t)wParam, (int64_t)lParam);
            }
        }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}
#endif

void multipleBrowsers::mouseButtonCallback(int button, int action, int mods)
{
    // See the comment in keyCallback() - GetCurrentContext() must be
    // checked before GetIO(), since this can fire before initUI() runs.
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    int width;
    int height;
    glfwGetWindowSize(mWindow, &width, &height);

    double xpos;
    double ypos;
    glfwGetCursorPos(mWindow, &xpos, &ypos);

    if (glfwGetKey(mWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
    {
        mMouseOffsetStartX = xpos / (double)width;

        if (button == GLFW_MOUSE_BUTTON_LEFT)
        {
            if (action == GLFW_PRESS)
            {
                mMouseOffsetStartY = ypos / (double)height;
                mXRotationStart = mXRotation;
                mYRotationStart = mYRotation;
            }
        }
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            if (action == GLFW_PRESS)
            {
                mMouseOffsetStartY = ((double)height - ypos) / (double)height;
                mXPanStart = mXPan;
                mYPanStart = mYPan;
            }
        }

        return;
    }

    // A left-button press can change which browser is selected - pick
    // whatever tile is actually under the cursor for that purpose only.
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        llCefBrowserHandle picked;
        int pickedX, pickedY;
        if (pickBrowser(xpos, ypos, picked, pickedX, pickedY))
        {
            if (picked != mSelectedHandle)
            {
                if (mSelectedHandle.IsValid())
                {
                    // Let the browser losing focus clear any hover/link-
                    // highlight state and caret blink before it stops
                    // receiving mouse events.
                    mCefBrowserManager->SendMouseMoveEvent(mSelectedHandle, 0, 0, /*mouseLeave=*/true);
                    mCefBrowserManager->SetFocus(mSelectedHandle, false);
                }
                LLCB_OUT_APP_INFO("Selected browser " << picked.index)
                mSelectedHandle = picked;
            }
            mCefBrowserManager->SetFocus(mSelectedHandle, true);
        }
    }

    // All mouse input - regardless of button, and regardless of where the
    // cursor physically is - is locked to whichever browser is selected.
    if (! mSelectedHandle.IsValid())
    {
        return;
    }

    llCefMouseButton cefButton;
    switch (button)
    {
        case GLFW_MOUSE_BUTTON_LEFT:
            cefButton = llCefMouseButton::Left;
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            cefButton = llCefMouseButton::Right;
            break;
        case GLFW_MOUSE_BUTTON_MIDDLE:
            cefButton = llCefMouseButton::Middle;
            break;
        default:
            return; // some other/extra button GLFW reports - nothing CEF understands
    }

    if (action != GLFW_PRESS && action != GLFW_RELEASE)
    {
        return;
    }

    int localX, localY;
    if (localCoordsForHandle(mSelectedHandle, xpos, ypos, localX, localY))
    {
        mCefBrowserManager->SendMouseClickEvent(mSelectedHandle, localX, localY, cefButton, action == GLFW_RELEASE);
    }
}

void multipleBrowsers::mouseMoveCallback(double xpos, double ypos)
{
    // See the comment in keyCallback() - GetCurrentContext() must be
    // checked before GetIO(), since this can fire before initUI() runs.
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    int width;
    int height;
    glfwGetWindowSize(mWindow, &width, &height);

    if (glfwGetKey(mWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
    {
        mMouseOffsetX = xpos / (double)width;

        if (glfwGetMouseButton(mWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        {
            mMouseOffsetY = ypos / (double)height;
            mYRotation = mYRotationStart + (mMouseOffsetX - mMouseOffsetStartX) * 360.0f;
            mXRotation = mXRotationStart + (mMouseOffsetY - mMouseOffsetStartY) * 360.0f;
        }
        if (glfwGetMouseButton(mWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
        {
            mMouseOffsetY = ((double)height - ypos) / (double)height;
            mXPan = mXPanStart + (mMouseOffsetX - mMouseOffsetStartX) * 5.0f;
            mYPan = mYPanStart + (mMouseOffsetY - mMouseOffsetStartY) * 5.0f;
        }

        return;
    }

    // Mouse moves are locked to the selected browser regardless of where the
    // cursor physically is - it can land outside that browser's own tile
    // (e.g. while dragging a selection), which just produces out-of-bounds
    // local coordinates, same as real OS mouse capture.
    if (! mSelectedHandle.IsValid())
    {
        return;
    }

    int localX, localY;
    if (localCoordsForHandle(mSelectedHandle, xpos, ypos, localX, localY))
    {
        mCefBrowserManager->SendMouseMoveEvent(mSelectedHandle, localX, localY);
    }
}

void multipleBrowsers::mouseScrollCallback(double xoffset, double yoffset)
{
    // See the comment in keyCallback() - GetCurrentContext() must be
    // checked before GetIO(), since this can fire before initUI() runs.
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    double mx, my;
    glfwGetCursorPos(mWindow, &mx, &my);

    if (glfwGetKey(mWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
    {
        mCameraDist += yoffset / mZoomSensitivity;

        if (mCameraDist < mZoomMin)
        {
            mCameraDist = mZoomMin;
        }
        if (mCameraDist > mZoomMax)
        {
            mCameraDist = mZoomMax;
        }

        return;
    }

    if (! mSelectedHandle.IsValid())
    {
        return;
    }

    int localX, localY;
    if (localCoordsForHandle(mSelectedHandle, mx, my, localX, localY))
    {
        // GLFW reports yoffset as a small unitless step (usually +/-1 per
        // notch, forward/up = positive); CEF's wheel delta expects roughly
        // the same scale as a native WM_MOUSEWHEEL notch (WHEEL_DELTA=120),
        // and shares the same sign convention (positive = scroll up).
        mCefBrowserManager->SendMouseWheelEvent(mSelectedHandle, localX, localY, (int)(yoffset * 120.0));
    }
}

void multipleBrowsers::windowFocusCallback(int focused)
{
    if (mSelectedHandle.IsValid())
    {
        mCefBrowserManager->SetFocus(mSelectedHandle, focused == GLFW_TRUE);
    }
}

void multipleBrowsers::resizeCallback(int width, int height)
{
    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    const double pi = std::acos(-1);
    double frustum_height = tan(mFov / 360.0 * pi) * mNearPlane;
    double frustum_width = frustum_height * (double)width / (double)height;
    glFrustum(-frustum_width, frustum_width, -frustum_height, frustum_height, mNearPlane, mFarPlane);

    glMatrixMode(GL_MODELVIEW);
}

void multipleBrowsers::initGLFWCallbacks()
{
    glfwSetKeyCallback(mWindow, keyCallbackStatic);
    glfwSetMouseButtonCallback(mWindow, mouseButtonCallbackStatic);
    glfwSetCursorPosCallback(mWindow, mouseMoveCallbackStatic);
    glfwSetScrollCallback(mWindow, mouseScrollCallbackStatic);
    glfwSetWindowFocusCallback(mWindow, windowFocusCallbackStatic);

    int width, height;
    glfwSetFramebufferSizeCallback(mWindow, resizeCallbackStatic);
    glfwGetFramebufferSize(mWindow, &width, &height);
    resizeCallback(width, height);
}

void multipleBrowsers::initCEFCallbacks(llCefBrowserHandle handle)
{
    // One registration set per handle instead of once globally. Everything
    // below is diagnostic logging only, tagged with the handle so multiple
    // browsers logging concurrently don't blur together - none of it
    // touches shared UI state (the real OS cursor, the window title bar,
    // etc.), so there's nothing here that needs gating to mSelectedHandle.
    // If this app ever does reflect e.g. OnCursorChanged/OnTitleChange into
    // real shared UI, that reflection is what should check
    // handle == mSelectedHandle, not the logging itself.

    mCefBrowserManager->SetOnAddressChangeCallback(handle, [handle](const std::string & url)
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onAddressChangeCallback: " <<
                          "URL changed to " << url
                         )
    });

    mCefBrowserManager->SetOnConsoleMessageCallback(handle, [handle](const std::string & message, const std::string & source, int line)
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onConsoleMessageCallback: " <<
                          message << " " <<
                          "in file " << source << " " <<
                          "at line " << line
                         )
    });

    mCefBrowserManager->SetOnCursorChangedCallback(handle, [handle](llCefCursorType type)
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onCursorChangedCallback: " <<
                          "cursor changed to: " << static_cast<int>(type)
                         )
    });

    mCefBrowserManager->SetOnCustomSchemeURLCallback(handle, [handle](const std::string & url, bool user_gesture, bool is_redirect)
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onCustomSchemeURLCallback: " <<
                          "URL: " << url << " " <<
                          "user_gesture: " << user_gesture << " " <<
                          "is_redirect: " << is_redirect
                         )
    });

    mCefBrowserManager->SetOnLoadEndCallback(handle, [handle](int status)
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onLoadEndCallback: " <<
                          "status " << status
                         )
    });

    mCefBrowserManager->SetOnPageSourceRetrievedCallback(handle, [handle](const std::string & source)
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onPageSourceRetrievedCallback: " <<
                          source.size() << " bytes: " <<
                          source
                         )
    }, 64);

    // The next four are functional, not diagnostic: CEF is blocked waiting
    // on an actual answer regardless of which browser fired them, so unlike
    // everything else in this function these must never be gated on
    // mSelectedHandle - a "background" tab's dialog/auth/file-picker still
    // has to be answered or that tab's page hangs.

    mCefBrowserManager->SetOnJSDialogCallback(handle, [handle](const std::string & origin_url, llCefJSDialogType dialog_type, const std::string & message_text, const std::string & default_prompt_text) -> bool
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onJSDialogCallback: " <<
                          "origin_url: " << origin_url << " " <<
                          "dialog_type: " << static_cast<int>(dialog_type) << " " <<
                          "message_text: " << message_text << " " <<
                          "default_prompt_text: " << default_prompt_text
                         )

        return false;  // don't suppress - let CEF show its own default dialog
    });

    mCefBrowserManager->SetOnBeforeUnloadCallback(handle, [handle](const std::string & message_text, bool is_reload) -> bool
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onBeforeUnloadCallback: " <<
                          "message_text: " << message_text << " " <<
                          "is_reload: " << is_reload
                         )

        return false;  // don't suppress - let CEF show its own default dialog
    });

    mCefBrowserManager->SetOnLoadErrorCallback(handle, [handle](int status, const std::string & error_text, const std::string & error_url)
    {
        LLCB_OUT_APP_ERR("browser " << handle.index << " onLoadErrorCallback: " <<
                         "Error URL: " << error_url << " " <<
                         "Error text: " << error_text << " " <<
                         "(error status: " << status << ")"
                        )
    });

    mCefBrowserManager->SetOnLoadStartCallback(handle, [handle]()
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onLoadStartCallback")
    });

    // Signal only, no pixel data attached, and can fire many times per
    // second on an animated page - deliberately not logged (would flood
    // the console) and not used to gate CopyLatestFrame() either.
    // CopyLatestFrame() already has its own per-browser dirty flag
    // internally (see llCefBrowserPixelBuffer::CopyLatest()) and returns
    // false cheaply when nothing changed, so draw() polling it
    // unconditionally every frame for every browser is already correct
    // and cheap - there'd be nothing left for this callback to gate.
    mCefBrowserManager->SetOnPageChangedCallback(handle, []()
    {
    });

    mCefBrowserManager->SetOnQueryCallback(handle, [handle](const std::string & request)
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onQueryCallback: " <<
                          "request " << request
                         )
    });

    mCefBrowserManager->SetOnStatusMessageCallback(handle, [handle](const std::string & message)
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onStatusMessageCallback: " <<
                          "message " << message
                         )

    });

    mCefBrowserManager->SetOnTitleChangeCallback(handle, [handle](const std::string & title)
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onTitleChangeCallback: " <<
                          "title " << title
                         )
    });

    mCefBrowserManager->SetOnTooltipCallback(handle, [handle](const std::string & text)
    {
        LLCB_OUT_APP_INFO("browser " << handle.index << " onTooltipCallback: " <<
                          "text " << text
                         )
    });

    mCefBrowserManager->SetOnFileDialogCallback(handle, [this, handle](int64_t dialogId, llCefFileDialogMode mode, const std::string & title, const std::string & defaultFile, const std::vector<std::string>& acceptFilters)
    {
        // Do not ask CEF to display a dialog - instead, we would use
        // our own and then respond with the selected file(s).
        LLCB_OUT_APP_INFO("browser " << handle.index << " onFileDialogCallback: " <<
                          "mode: " << static_cast<int>(mode) << " " <<
                          "title: " << title << " " <<
                          "default_file: " << defaultFile << " " <<
                          "accept_filters: " << acceptFilters.size()
                         )

        // fake a file selection, responding inline, for THIS callback's own
        // handle - not mSelectedHandle, since whichever browser asked is the
        // one CEF is waiting on, regardless of what's currently selected.
        mCefBrowserManager->RespondToFileDialog(handle, dialogId, { "C:\\foo\\bar\\flasm.txt" });
    });

    mCefBrowserManager->SetOnAuthRequestCallback(handle, [handle](const std::string & originUrl, const std::string & host, int port, const std::string & realm, const std::string & scheme, bool isProxy, std::string & username, std::string & password) -> bool
    {
        // Do not ask CEF to display a credentials dialog - instead, we
        // would show our own and answer with what the user entered. Called
        // on CEF's IO thread, so must answer synchronously, right here.
        LLCB_OUT_APP_INFO("browser " << handle.index << " onAuthRequestCallback: " <<
                          "origin_url: " << originUrl << " " <<
                          "host: " << host << " " <<
                          "port: " << port << " " <<
                          "realm: " << realm << " " <<
                          "scheme: " << scheme << " " <<
                          "is_proxy: " << isProxy
                         )

        // fake credentials
        username = "user";
        password = "passwd";
        return true;
    });
}

void multipleBrowsers::createBrowserTab(const std::string& url, int width, int height)
{
    if (mCefBrowserManager->LiveBrowserCount() >= mMaxBrowsers)
    {
        LLCB_OUT_APP_INFO("At max browser count (" << mMaxBrowsers << ") - ignoring request for a new one")
        return;
    }

    llCefBrowserHandle handle = mCefBrowserManager->CreateBrowser(url, width, height);
    if (! handle.IsValid())
    {
        LLCB_OUT_APP_ERR("CreateBrowser failed - not adding a tab for it")
        return;
    }
    initCEFCallbacks(handle);

    // Owned by this app, not the library - freed in destroyBrowserTab()/
    // reset(), whichever destroys this handle first.
    BrowserTileData* data = new BrowserTileData();
    glGenTextures(1, &data->textureId);

    glBindTexture(GL_TEXTURE_2D, data->textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);

    // Seed the tracked size immediately so draw()'s aspect-ratio math has a
    // valid width/height to work with before the first CopyLatestFrame()
    // succeeds - otherwise it divides 0/0 for a newly created browser.
    data->textureWidth = width;
    data->textureHeight = height;

    mCefBrowserManager->SetUserData(handle, data);

    mCefBrowserManager->ResizeBrowser(handle, width, height);

    // The most recently created tab becomes the selected one (also settable
    // by clicking a tile - see pickBrowser()/mouseButtonCallback()).
    mSelectedHandle = handle;
}

void multipleBrowsers::destroyBrowserTab()
{
    if (! mSelectedHandle.IsValid())
    {
        return;
    }

    // Fetch and free this browser's userData before destroying it - once
    // destroyed, the handle is no longer valid to query the manager with.
    if (BrowserTileData* data = GetTileData(*mCefBrowserManager, mSelectedHandle))
    {
        glDeleteTextures(1, &data->textureId);
        delete data;
    }

    mCefBrowserManager->DestroyBrowser(mSelectedHandle);

    // Fall back to whichever tab remains that was created last, if any.
    mSelectedHandle = llCefBrowserHandle::Invalid();
    mCefBrowserManager->ForEachBrowser([this](llCefBrowserHandle handle)
    {
        mSelectedHandle = handle;
    });
}

void multipleBrowsers::init()
{
    char exe_path[MAX_PATH + 1];

    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
    std::filesystem::path default_root_cache_path(exe_dir / "cef_profile");
    std::filesystem::path default_cache_path(exe_dir / "cef_profile" / "Default");
    std::filesystem::path default_log_file(exe_dir / "cef_log.txt");

    llCefBrowserLibInitOptions initOptions;
    initOptions.rootCachePath = default_root_cache_path.string();
    initOptions.logFile = default_log_file.string();
    initOptions.userAgentProduct = "llCefBrowser: MultipleBrowsers";

    if (! llCefBrowserLib::Initialize(initOptions))
    {
        LLCB_OUT_CEF_ERR("LLCefBrowser was unable to initialize");
        exit(EXIT_FAILURE);
    }
    LLCB_OUT_CEF_INFO("LLCefBrowser was initialized");

    std::cout << "-------------------------------------------------------------" << std::endl;
    std::cout << "  LLCefBrowser version: " << llCefBrowserLib::GetVersion() << std::endl;
    std::cout << "           CEF version: " << llCefBrowserLib::GetCefVersion() << std::endl;
    std::cout << "      Chromium version: " << llCefBrowserLib::GetChromiumVersion() << std::endl;
    std::cout << "       root cache path: " << default_root_cache_path.string() << std::endl;
    std::cout << "            cache path: " << default_cache_path.string() << std::endl;
    std::cout << "   Application version: " << mAppVersionStr << std::endl;
    std::cout << "-------------------------------------------------------------" << std::endl;

    llCefBrowserLib::SetJavaScriptBridge(&mBridge);

    if (! glfwInit())
    {
        exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    mWindow = glfwCreateWindow(mWindowWidth, mWindowHeight, mWindowTitle.c_str(), nullptr, nullptr);
    if (! mWindow)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    glfwSetWindowPos(mWindow, 64, 100);

    glfwSetWindowUserPointer(mWindow, this);

    // Create a Windows subclass procedure for handling keyboard events using
    // native Windows messages and parameters which is what Dullahan requires.
#if defined(WIN32)
    HWND hwnd = glfwGetWin32Window(mWindow);
    SetWindowSubclass(hwnd, keyEventSubClassProc, 0x01, (DWORD_PTR)this);
#endif

    glfwSetErrorCallback(errorCallback);
    glfwMakeContextCurrent(mWindow);
    glfwSwapInterval(1);
    gladLoadGL();

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    // Each tile's wireframe border shares the exact vertex positions of its
    // textured quad (same z), so GL_LESS would reject the border as
    // equal-depth once depth testing is on - GL_LEQUAL lets same-depth
    // fragments drawn later still win.
    glDepthFunc(GL_LEQUAL);

    initGLFWCallbacks();

    // one cache for all browsers -- this example doesn't care about the UI-
    // vs-prim cookie isolation, so use the same path for both contexts
    mCefBrowserManager = std::make_unique<llCefBrowserManager>(default_cache_path.string(), default_cache_path.string());

    // Start with one browser tab, the home page.
    createBrowserTab(mHomeUrl, mDefaultBrowserWidth, mDefaultBrowserHeight);

    // Must run after initGLFWCallbacks(): ImGui_ImplGlfw_InitForOpenGL's
    // install_callbacks=true chain-calls whatever GLFW callback was already
    // registered on mWindow when it installs its own (see
    // imgui_impl_glfw.h) - so the app's own callbacks have to be in place
    // first, or there's nothing for ImGui to chain to and its callback
    // registration simply overwrites the app's instead.
    initUI();
}

multipleBrowsers::TileLayout multipleBrowsers::computeTileLayout(size_t index, size_t num_x, size_t num_y, GLfloat spacing, llCefBrowserHandle handle) const
{
    const size_t sx = index % num_x;
    const size_t sy = std::floor(index / num_x);

    const BrowserTileData* data = GetTileData(*mCefBrowserManager, handle);
    const int texWidth = data ? data->textureWidth : 0;
    const int texHeight = data ? data->textureHeight : 0;

    GLfloat sw = 1.0;
    GLfloat sh = 1.0;
    if (texWidth >= texHeight)
    {
        sw = 1.0;
        sh = (GLfloat)texHeight / (GLfloat)texWidth;
    }
    else
    {
        sw = (GLfloat)texWidth / (GLfloat)texHeight;
        sh = 1.0;
    }

    TileLayout tile;
    tile.width_offset = sw / 2.0f;
    tile.height_offset = sh / 2.0f;

    GLfloat sw1 = 1.0;
    GLfloat sh1 = 1.0;
    GLfloat total_width = (GLfloat)(num_x * sw1 + (num_x - 1) * spacing);
    GLfloat total_height = (GLfloat)(num_y * sh1 + (num_y - 1) * spacing);
    tile.x = (GLfloat)(sx * (sw1 + spacing)) - total_width / 2.0f + (GLfloat)(sw1) / 2.0f;
    tile.y = - ((GLfloat)(sy * (sh1 + spacing)) - total_height / 2.0f + (GLfloat)(sh1) / 2.0f);
    tile.z = 0.0f;

    return tile;
}

bool multipleBrowsers::computeGroundHit(double mouseX, double mouseY, double& outHitX, double& outHitY) const
{
    int width, height;
    glfwGetWindowSize(mWindow, &width, &height);
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    // GLFW window coordinates -> normalized device coordinates. GLFW's y
    // grows downward from the top; NDC's y grows upward, hence the flip.
    const double ndcX = (2.0 * mouseX / (double)width) - 1.0;
    const double ndcY = 1.0 - (2.0 * mouseY / (double)height);

    // Same frustum math as resizeCallback(), evaluated for the current
    // window size so it matches whatever's actually on screen.
    const double pi = std::acos(-1.0);
    const double frustum_height = tan(mFov / 360.0 * pi) * mNearPlane;
    const double frustum_width = frustum_height * (double)width / (double)height;

    // Ray in eye space: the eye sits at the origin looking down -Z, so the
    // ray direction is just the near-plane point the mouse projects to.
    const Vec3 dirEye{ ndcX * frustum_width, ndcY * frustum_height, -mNearPlane };

    // draw() builds the scene transform as M = T * Rx * Ry (glTranslatef
    // then glRotatef(x) then glRotatef(y), each multiplying on the right of
    // the current matrix). Undo it in reverse to bring the eye-space ray
    // into the same object space the quads are laid out in: subtract the
    // translation, then apply the inverse rotations (negated angles, applied
    // X before Y to reverse M's Y-then-X order).
    const Vec3 originObj = RotateY(RotateX(Vec3{ -mXPan, -mYPan, -mCameraDist }, -mXRotation), -mYRotation);
    const Vec3 dirObj = RotateY(RotateX(dirEye, -mXRotation), -mYRotation);

    // Every tile lies in the z=0 plane in object space, so there's a single
    // ray/plane hit point shared by all of them.
    if (std::abs(dirObj.z) < 1e-9)
    {
        return false;
    }
    const double t = -originObj.z / dirObj.z;
    if (t <= 0.0)
    {
        return false;
    }

    outHitX = originObj.x + t * dirObj.x;
    outHitY = originObj.y + t * dirObj.y;
    return true;
}

bool multipleBrowsers::pickBrowser(double mouseX, double mouseY, llCefBrowserHandle& outHandle, int& outLocalX, int& outLocalY) const
{
    double hitX, hitY;
    if (! computeGroundHit(mouseX, mouseY, hitX, hitY))
    {
        return false;
    }

    const size_t count = mCefBrowserManager->LiveBrowserCount();
    if (count == 0)
    {
        return false;
    }
    const GLfloat spacing = 0.1;
    const size_t num_x = std::ceil(sqrt(count));
    const size_t num_y = std::ceil(static_cast<double>(count) / static_cast<double>(num_x));

    bool found = false;
    size_t index = 0;
    mCefBrowserManager->ForEachBrowser([this, &index, num_x, num_y, spacing, hitX, hitY, &found, &outHandle, &outLocalX, &outLocalY](llCefBrowserHandle handle)
    {
        if (! found)
        {
            const TileLayout tile = computeTileLayout(index, num_x, num_y, spacing, handle);
            if (hitX >= tile.x - tile.width_offset && hitX <= tile.x + tile.width_offset &&
                    hitY >= tile.y - tile.height_offset && hitY <= tile.y + tile.height_offset)
            {
                const double u = (hitX - (tile.x - tile.width_offset)) / (2.0 * tile.width_offset);
                const double v = ((tile.y + tile.height_offset) - hitY) / (2.0 * tile.height_offset);

                const BrowserTileData* data = GetTileData(*mCefBrowserManager, handle);
                outHandle = handle;
                outLocalX = (int)(u * (data ? data->textureWidth : 0));
                outLocalY = (int)(v * (data ? data->textureHeight : 0));
                found = true;
            }
        }
        ++index;
    });

    return found;
}

bool multipleBrowsers::computeTileLayoutForHandle(llCefBrowserHandle handle, TileLayout& outTile) const
{
    const size_t count = mCefBrowserManager->LiveBrowserCount();
    if (count == 0)
    {
        return false;
    }
    const GLfloat spacing = 0.1;
    const size_t num_x = std::ceil(sqrt(count));
    const size_t num_y = std::ceil(static_cast<double>(count) / static_cast<double>(num_x));

    bool found = false;
    size_t index = 0;
    mCefBrowserManager->ForEachBrowser([this, handle, num_x, num_y, spacing, &index, &found, &outTile](llCefBrowserHandle candidate)
    {
        if (! found && candidate == handle)
        {
            outTile = computeTileLayout(index, num_x, num_y, spacing, candidate);
            found = true;
        }
        ++index;
    });

    return found;
}

bool multipleBrowsers::localCoordsForHandle(llCefBrowserHandle handle, double mouseX, double mouseY, int& outLocalX, int& outLocalY) const
{
    double hitX, hitY;
    if (! computeGroundHit(mouseX, mouseY, hitX, hitY))
    {
        return false;
    }

    TileLayout tile;
    if (! computeTileLayoutForHandle(handle, tile))
    {
        return false;
    }

    // Deliberately not clamped to [0, width/height] - out-of-bounds
    // coordinates are how a locked-focus browser finds out the cursor is
    // currently somewhere else on screen, same as OS mouse capture during a
    // drag outside a view.
    const double u = (hitX - (tile.x - tile.width_offset)) / (2.0 * tile.width_offset);
    const double v = ((tile.y + tile.height_offset) - hitY) / (2.0 * tile.height_offset);

    const BrowserTileData* data = GetTileData(*mCefBrowserManager, handle);
    outLocalX = (int)(u * (data ? data->textureWidth : 0));
    outLocalY = (int)(v * (data ? data->textureHeight : 0));
    return true;
}

void multipleBrowsers::update()
{
    llCefBrowserLib::DoMessageLoopWork();
    mCefBrowserManager->Tick();

    // Required every tick, per live browser -- CreateBrowser() always uses
    // external-begin-frame mode now (see SendExternalBeginFrame()'s own doc
    // comment), so without this CEF never composites a frame at all for any
    // of them: no error, just permanently black tiles.
    mCefBrowserManager->ForEachBrowser([this](llCefBrowserHandle handle)
    {
        mCefBrowserManager->SendExternalBeginFrame(handle);
    });
}

void multipleBrowsers::draw()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef((GLfloat)mXPan, (GLfloat)mYPan, (GLfloat)mCameraDist);
    glRotatef((GLfloat)mXRotation, 1.0f, 0.0f, 0.0f);
    glRotatef((GLfloat)mYRotation, 0.0f, 1.0f, 0.0f);
    glRotatef(0.0f, 0.0f, 0.0f, 1.0f);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0, 1.0, 1.0);

    const size_t count = mCefBrowserManager->LiveBrowserCount();
    const GLfloat spacing = 0.1;
    const size_t num_x = std::ceil(sqrt(count));
    const size_t num_y = std::ceil(static_cast<double>(count) / static_cast<double>(num_x));
    size_t index = 0;

    mCefBrowserManager->ForEachBrowser([this, &index, num_x, num_y, spacing](llCefBrowserHandle handle)
    {

        const TileLayout tile = computeTileLayout(index, num_x, num_y, spacing, handle);
        const GLfloat width_offset = tile.width_offset;
        const GLfloat height_offset = tile.height_offset;
        const GLfloat vertical_nudge = 0.0;
        const GLfloat x = tile.x;
        const GLfloat y = tile.y;
        const GLfloat z = tile.z;

        BrowserTileData* data = GetTileData(*mCefBrowserManager, handle);
        glBindTexture(GL_TEXTURE_2D, data ? data->textureId : 0);

        int w, h;
        std::vector<uint8_t> frame;
        if (mCefBrowserManager->CopyLatestFrame(handle, frame, w, h))
        {

            if (! data || w != data->textureWidth || h != data->textureHeight)
            {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, frame.data());
                if (data)
                {
                    data->textureWidth = w;
                    data->textureHeight = h;
                }
            }
            else
            {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, frame.data());
            }
        }

        // Reset state left over from the previous iteration's wireframe
        // outline pass below (texturing/color/polygon mode), otherwise only
        // the first browser drawn each frame gets a filled, textured quad.
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0, 1.0, 1.0);

        glBegin(GL_QUADS);
        glTexCoord2f(0.0, 1.0);
        glVertex3f(x - width_offset, y - height_offset - vertical_nudge, z);
        glTexCoord2f(1.0, 1.0);
        glVertex3f(x + width_offset, y - height_offset - vertical_nudge, z);
        glTexCoord2f(1.0, 0.0);
        glVertex3f(x + width_offset, y + height_offset - vertical_nudge, z);
        glTexCoord2f(0.0, 0.0);
        glVertex3f(x - width_offset, y + height_offset - vertical_nudge, z);
        glEnd();

        glPolygonMode(GL_FRONT, GL_LINE);
        glDisable(GL_TEXTURE_2D);
        if (handle == mSelectedHandle)
        {
            glColor3f(0.0f, 1.0f, 0.0f);
            glLineWidth(1.5);
            glBegin(GL_LINE_STRIP);
            glVertex3f(x - width_offset, y + height_offset - vertical_nudge, z);
            glVertex3f(x + width_offset, y + height_offset - vertical_nudge, z);
            glVertex3f(x + width_offset, y - height_offset - vertical_nudge, z);
            glVertex3f(x - width_offset, y - height_offset - vertical_nudge, z);
            glVertex3f(x - width_offset, y + height_offset - vertical_nudge, z);
            glEnd();
        }

        ++index;
    });
}

void multipleBrowsers::run()
{
    while (! glfwWindowShouldClose(mWindow))
    {
        update();

        draw();

        updateUI();

        glfwSwapBuffers(mWindow);

        glfwPollEvents();
    }
}

void multipleBrowsers::reset()
{
    resetUI();

    // Free every remaining browser's userData before destroying it - once
    // destroyed, the handle is no longer valid to query the manager with.
    mCefBrowserManager->ForEachBrowser([this](llCefBrowserHandle handle)
    {
        delete GetTileData(*mCefBrowserManager, handle);
    });

    // Initiate destruction of all browsers, which will trigger the async close
    mCefBrowserManager->DestroyAll();

    // Pump a few more turns so each async close handshake finishes
    // before the manager (and its CefRequestContext reference) is destroyed.
    for (int i = 0; i < 30; ++i)
    {
        llCefBrowserLib::DoMessageLoopWork();
    }

    mCefBrowserManager.reset();

    llCefBrowserLib::SetJavaScriptBridge(nullptr);
    llCefBrowserLib::Shutdown();

#if defined(WIN32)
    HWND hwnd = glfwGetWin32Window(mWindow);
    RemoveWindowSubclass(hwnd, keyEventSubClassProc, 0x01);
#endif

    glfwDestroyWindow(mWindow);

    glfwTerminate();
}

void multipleBrowsers::initUI()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();
    io.FontGlobalScale = 1.2f;
    ImGui_ImplGlfw_InitForOpenGL(mWindow, true);
    ImGui_ImplOpenGL2_Init();
}

void multipleBrowsers::updateUI()
{
    // main host for UI - URL and bookmarks drop-down
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Turn off window decoration - we don't want it
    ImGuiWindowFlags window_flags = 0;
    window_flags |= ImGuiWindowFlags_NoTitleBar;
    window_flags |= ImGuiWindowFlags_NoScrollbar;
    window_flags |= ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoResize;
    window_flags |= ImGuiWindowFlags_NoCollapse;
    window_flags |= ImGuiWindowFlags_NoBackground;

    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(0, main_viewport->WorkPos.y), 0);
    ImGui::SetNextWindowSize(ImVec2(main_viewport->Size.x, ImGui::GetFrameHeight() * 3), 0);

    // Write menu bar and associated actions - everything here acts on
    // mSelectedHandle (the tile with the green border), not "whichever
    // browser the mouse happens to be over", matching how mouse/keyboard
    // input is already routed in mouseButtonCallback()/keyCallback().
    ImGui::Begin("##ui", NULL, window_flags);
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Quit"))
            {
                glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Actions"))
        {
            if (ImGui::MenuItem("New Tab"))
            {
                createBrowserTab(mHomeUrl, mDefaultBrowserWidth, mDefaultBrowserHeight);
            }
            if (ImGui::MenuItem("Close Selected Tab", nullptr, false, mSelectedHandle.IsValid()))
            {
                destroyBrowserTab();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Go Home", nullptr, false, mSelectedHandle.IsValid()))
            {
                mCefBrowserManager->Navigate(mSelectedHandle, mHomeUrl);
            }
            if (ImGui::MenuItem("Back", nullptr, false, mSelectedHandle.IsValid() && mCefBrowserManager->CanGoBack(mSelectedHandle)))
            {
                mCefBrowserManager->GoBack(mSelectedHandle);
            }
            if (ImGui::MenuItem("Forward", nullptr, false, mSelectedHandle.IsValid() && mCefBrowserManager->CanGoForward(mSelectedHandle)))
            {
                mCefBrowserManager->GoForward(mSelectedHandle);
            }
            if (ImGui::MenuItem("Reload", nullptr, false, mSelectedHandle.IsValid()))
            {
                mCefBrowserManager->Reload(mSelectedHandle, false);
            }
            if (ImGui::MenuItem("Reload (Ignore Cache)", nullptr, false, mSelectedHandle.IsValid()))
            {
                mCefBrowserManager->Reload(mSelectedHandle, true);
            }
            if (ImGui::MenuItem("Stop", nullptr, false, mSelectedHandle.IsValid()))
            {
                mCefBrowserManager->StopLoad(mSelectedHandle);
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Show Dev Console", nullptr, false, mSelectedHandle.IsValid()))
            {
                mCefBrowserManager->ShowDevTools(mSelectedHandle);
            }
            if (ImGui::MenuItem("Execute Test JavaScript", nullptr, false, mSelectedHandle.IsValid()))
            {
                mCefBrowserManager->ExecuteJavaScript(mSelectedHandle, "document.body.style.backgroundColor = 'red';");
            }
            if (ImGui::BeginMenu("Resize", mSelectedHandle.IsValid()))
            {
                std::vector<std::pair<int, int>> new_sizes =
                {
                    {0, 0}, // special - indicates random size
                    {256, 480},
                    {640, 512},
                    {1024, 768},
                    {1024, 1024},
                    {1536, 900},
                    {2048, 2048},
                };
                std::vector<std::pair<int, int>>::iterator sizes_iter = new_sizes.begin();
                while (sizes_iter != new_sizes.end())
                {
                    int new_width = (*sizes_iter).first;
                    int new_height = (*sizes_iter).second;
                    std::string label = "Random size";
                    if (new_width > 0)
                    {
                        label = std::to_string(new_width) + " x " + std::to_string(new_height);
                    }
                    else
                    {
                        std::random_device rd;
                        std::mt19937 gen(rd());
                        std::uniform_int_distribution<std::size_t> distrib(512, 1536);
                        new_width = distrib(gen);
                        new_height = distrib(gen);
                    }

                    if (ImGui::MenuItem(label.c_str()))
                    {
                        mCefBrowserManager->ResizeBrowser(mSelectedHandle, new_width, new_height);
                    }
                    ++sizes_iter;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Zoom", mSelectedHandle.IsValid()))
            {
                std::vector<int> options = {25, 50, 100, 200, 400};
                std::vector<int>::iterator options_iter = options.begin();
                while (options_iter != options.end())
                {
                    std::string label = std::to_string(*options_iter) + "%";
                    if (ImGui::MenuItem(label.c_str()))
                    {
                        mCefBrowserManager->SetPageZoom(mSelectedHandle, static_cast<float>(*options_iter) / 100.0f);
                    }
                    ++options_iter;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit", mSelectedHandle.IsValid()))
            {
                if (ImGui::MenuItem("Undo"))
                {
                    mCefBrowserManager->Undo(mSelectedHandle);
                }
                if (ImGui::MenuItem("Redo"))
                {
                    mCefBrowserManager->Redo(mSelectedHandle);
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Cut"))
                {
                    mCefBrowserManager->Cut(mSelectedHandle);
                }
                if (ImGui::MenuItem("Copy"))
                {
                    mCefBrowserManager->Copy(mSelectedHandle);
                }
                if (ImGui::MenuItem("Paste"))
                {
                    mCefBrowserManager->Paste(mSelectedHandle);
                }
                if (ImGui::MenuItem("Delete"))
                {
                    mCefBrowserManager->Delete(mSelectedHandle);
                }
                if (ImGui::MenuItem("Select All"))
                {
                    mCefBrowserManager->SelectAll(mSelectedHandle);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Cookies"))
            {
                if (ImGui::MenuItem("Set Test Cookie"))
                {
                    mCefBrowserManager->SetCookie("https://secondlife.com", "testcookie", "testvalue",
                                                  "secondlife.com", "/", false, true, [this](bool success)
                    {
                        LLCB_OUT_APP_INFO("SetCookie: " << "success: " << success)
                    });
                }
                if (ImGui::MenuItem("Get Cookies (log)"))
                {
                    mCefBrowserManager->GetCookies([this](const std::vector<llCefCookie>& cookies)
                    {
                        LLCB_OUT_APP_INFO("GetCookies: " << cookies.size() << " cookie(s)")
                        for (const auto& cookie : cookies)
                        {
                            LLCB_OUT_APP_INFO("  " << cookie.domain << cookie.path << " " <<
                                              cookie.name << "=" << cookie.value << " " <<
                                              "secure: " << cookie.secure << " " <<
                                              "httpOnly: " << cookie.httpOnly
                                             )
                        }
                    });
                }
                if (ImGui::MenuItem("Delete All Cookies"))
                {
                    mCefBrowserManager->DeleteAllCookies([this](int numDeleted)
                    {
                        LLCB_OUT_APP_INFO("DeleteAllCookies: " << numDeleted << " deleted")
                    });
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About"))
            {
                mShowAbout = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (mShowAbout)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
        if (ImGui::Begin(mWindowTitle.c_str(), &mShowAbout, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
        {
            std::ostringstream ss;

            ss << "             Version: " << mAppVersionStr << std::endl;
            ss << std::endl;
            ss << "llCefBrowser version: " << llCefBrowserLib::GetVersion() << std::endl;
            ss << "         CEF version: " << llCefBrowserLib::GetCefVersion() << std::endl;
            ss << "    Chromium version: " << llCefBrowserLib::GetChromiumVersion() << std::endl;
            ss << std::endl;
            ss << "     Root cache path: " << llCefBrowserLib::GetRootCachePath() << std::endl;
            ss << "          Cache path: " << mCefBrowserManager->GetCachePath() << std::endl;
            ss << "       Log file path: " << llCefBrowserLib::GetLogFilePath() << std::endl;
            ss << std::endl;
            ss << "       IMGUI version: " << IMGUI_VERSION<< std::endl ;
            ss << std::endl;
            ss << "Ctrl+drag to orbit/pan the camera, Ctrl+scroll to zoom." << std::endl << std::endl;
            ss << "Click a tile to select it - the selected tile gets a" << std::endl;
            ss << "green border and receives all mouse and keyboard input." << std::endl << std::endl;
            ss << "ESC key to exit.";

            ImGui::Text(ss.str().c_str());
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // Write freeform URL entry bar - navigates the selected tab.
    ImGui::SetNextItemWidth(main_viewport->Size.x);
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(64, 64, 100, 255));
    static char url_buffer[4096];
    if (ImGui::InputTextWithHint("UrlInput", "Enter a URL",
                                 url_buffer,
                                 IM_ARRAYSIZE(url_buffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (mSelectedHandle.IsValid())
        {
            mCefBrowserManager->Navigate(mSelectedHandle, url_buffer);
        }
    }
    ImGui::PopStyleColor();

    // Write bookmarks bar - navigates the selected tab. A good place to add
    // useful or interesting bookmarks that are frequently accessed.
    const char* items[] =
    {
        "https://sl-viewer-media-system.s3.amazonaws.com/bookmarks/index.html",
        "chrome://version",
        "https://howbigismybrowser.com/",
        "https://secondlife.com",
        "https://viewer-splash.secondlife.com/",
        "https://viewer-splash-v2.secondlife.com/",
        "https://marketplace.secondlife.com/",
        "https://lecs-viewer-web-components.s3.amazonaws.com/v3.0/agni/guide.html",
        "https://lecs-viewer-web-components.s3.amazonaws.com/v3.0/agni/avatars.html"
    };
    static const char* current_item = "Select a bookmark";
    ImGui::SetNextItemWidth(main_viewport->Size.x);
    ImGui::SetCursorPos(ImVec2(0, ImGui::GetFrameHeight()));
    if (ImGui::BeginCombo("##Bookmarks", current_item))
    {
        for (int n = 0; n < IM_ARRAYSIZE(items); n++)
        {
            bool is_selected = (current_item == items[n]);
            if (ImGui::Selectable(items[n], is_selected))
            {
                current_item = items[n];
                if (mSelectedHandle.IsValid())
                {
                    mCefBrowserManager->Navigate(mSelectedHandle, current_item);
                }
            }
            if (is_selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
}

void multipleBrowsers::resetUI()
{
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

int main(int argc, char* argv[])
{
    int exitCode = llCefBrowserLib::ExecuteSubProcess(argc, argv);
    if (exitCode >= 0)
    {
        return exitCode;
    }

    LLCB_OUT_APP_INFO("Finished with CEF process management - starting app")

    multipleBrowsers* app = new multipleBrowsers();

    app->init();

    app->run();

    app->reset();
}
