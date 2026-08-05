/**
 *
 * @file single-browser-example.cpp
 * @brief Implementation of the single-browser example: one CEF browser rendered offscreen into an OpenGL texture.
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

#include <chrono>
#include <filesystem>
#include <vector>

#include "llCefBrowserManager.h"
#include "llCefBrowserLib.h"
#include "llCefBrowserJavaScriptBridge.h"
#include "llCefBrowserLibDebug.h"

#include "single-browser-example.h"

singleBrowser::singleBrowser() :
    mWindow(nullptr),
    mTextureWidth(1024),
    mTextureHeight(1024),
    mTextureDepth(4),
    mTextureId(0),
    mCefBrowserManager(nullptr),
    mShowAbout(false)
{
}

singleBrowser::~singleBrowser() = default;

static void errorCallback(int error, const char* description)
{
    LLCB_OUT_APP_INFO(description << " - code: " << error)
}

void singleBrowser::keyCallback(int key, int scancode, int action, int mods)
{
    // GLFW can deliver this before initUI() has created the ImGui context
    // (e.g. Windows synchronously dispatching a queued key/mouse message
    // during window setup calls like glfwSetWindowPos, before init() even
    // reaches initUI()) - ImGui::GetIO() on a null context is undefined
    // behavior in a Release build, since IM_ASSERT compiles out with
    // NDEBUG, so this has to check GetCurrentContext() first.
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard)
    {
        return;
    }

    // Every other command that used to live here (navigation, reload,
    // dev tools, zoom, cookies, edit commands) now has a menu equivalent
    // in updateUI() instead - see the Actions menu.
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
    {
        glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
    }
}

#if defined(WIN32)
// Windows subclass procedure for handling keyboard events using native
// Windows messages and parameters which is what CEF requires.
LRESULT CALLBACK singleBrowser::keyEventSubClassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_CHAR || uMsg == WM_KEYDOWN || uMsg == WM_KEYUP ||
            uMsg == WM_SYSCHAR || uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP)
    {
        // This hook operates on raw Win32 messages, entirely outside GLFW's
        // (and therefore ImGui's) callback system, so it has no other way
        // to know a text field like the URL bar currently wants the
        // keystroke instead of the browser - without this check, typing
        // into any ImGui widget also gets sent to the page as a key event.
        // Guard against a null context too: this can still fire briefly
        // during reset(), after resetUI() has destroyed ImGui's context but
        // before RemoveWindowSubclass() has run.
        const bool imguiWantsKeyboard = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
        if (! imguiWantsKeyboard)
        {
            singleBrowser* parent = (singleBrowser*)dwRefData;
            parent->mCefBrowserManager->SendKeyEvent(parent->mCefBrowser, uMsg, (uint64_t)wParam, (int64_t)lParam);
        }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}
#endif

void singleBrowser::mouseButtonCallback(int button, int action, int mods)
{
    // See the comment in keyCallback() - GetCurrentContext() must be
    // checked before GetIO(), since this can fire before initUI() runs.
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    double mx, my;
    glfwGetCursorPos(mWindow, &mx, &my);

    // account for UI offset
    my = scaleMouseYforUI(my);

    llCefMouseButton cefButton;
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        cefButton = llCefMouseButton::Left;
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        cefButton = llCefMouseButton::Right;
    }
    else
    {
        return;
    }

    const bool mouseUp = (action == GLFW_RELEASE);
    mCefBrowserManager->SendMouseClickEvent(mCefBrowser, (int)mx, (int)my, cefButton, mouseUp);

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        mCefBrowserManager->SetFocus(mCefBrowser, true);
    }
}

void singleBrowser::mouseMoveCallback(double xpos, double ypos)
{
    // See the comment in keyCallback() - GetCurrentContext() must be
    // checked before GetIO(), since this can fire before initUI() runs.
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    mCefBrowserManager->SendMouseMoveEvent(mCefBrowser, (int)xpos, (int)scaleMouseYforUI(ypos));
}

void singleBrowser::mouseScrollCallback(double xoffset, double yoffset)
{
    // See the comment in keyCallback() - GetCurrentContext() must be
    // checked before GetIO(), since this can fire before initUI() runs.
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    double mx, my;
    glfwGetCursorPos(mWindow, &mx, &my);

    mCefBrowserManager->SendMouseWheelEvent(mCefBrowser, (int)mx, (int)my, (int)(yoffset * 30));
}

void singleBrowser::windowFocusCallback(int focused)
{
    mCefBrowserManager->SetFocus(mCefBrowser, focused == GLFW_TRUE);
}

void singleBrowser::resizeCallback(int width, int height)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0f, width, 0.0f, height, -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glViewport(0, 0, width, height);

    mTextureWidth = width;
    mTextureHeight = height;

    glDeleteTextures(1, &mTextureId);
    glGenTextures(1, &mTextureId);
    glBindTexture(GL_TEXTURE_2D, mTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mTextureWidth, mTextureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    mCefBrowserManager->ResizeBrowser(mCefBrowser, width, height);

    update();
}

void singleBrowser::initGLFWCallbacks()
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

void singleBrowser::initCEFCallbacks()
{
    mCefBrowserManager->SetOnAddressChangeCallback(mCefBrowser, [this](const std::string & url)
    {
        LLCB_OUT_APP_INFO("onAddressChangeCallback: " <<
                          "URL changed to " << url
                         )
    });

    mCefBrowserManager->SetOnConsoleMessageCallback(mCefBrowser, [this](const std::string & message, const std::string & source, int line)
    {
        LLCB_OUT_APP_INFO("onConsoleMessageCallback: " <<
                          message << " " <<
                          "in file " << source << " " <<
                          "at line " << line
                         )
    });

    mCefBrowserManager->SetOnCursorChangedCallback(mCefBrowser, [this](llCefCursorType type)
    {
        LLCB_OUT_APP_INFO("onCursorChangedCallback: " <<
                          "cursor changed to: " << static_cast<int>(type)
                         )
    });

    mCefBrowserManager->SetOnCustomSchemeURLCallback(mCefBrowser, [this](const std::string & url, bool user_gesture, bool is_redirect)
    {
        LLCB_OUT_APP_INFO("onCustomSchemeURLCallback: " <<
                          "URL: " << url << " " <<
                          "user_gesture: " << user_gesture << " " <<
                          "is_redirect: " << is_redirect
                         )
    });

    mCefBrowserManager->SetOnLoadEndCallback(mCefBrowser, [this](int status)
    {
        LLCB_OUT_APP_INFO("onLoadEndCallback: " <<
                          "status " << status
                         )
    });

    mCefBrowserManager->SetOnPageSourceRetrievedCallback(mCefBrowser, [this](const std::string & source)
    {
        LLCB_OUT_APP_INFO("onPageSourceRetrievedCallback: " <<
                          source.size() << " bytes: " <<
                          source
                         )
    }, 64);

    mCefBrowserManager->SetOnJSDialogCallback(mCefBrowser, [this](const std::string & origin_url, llCefJSDialogType dialog_type, const std::string & message_text, const std::string & default_prompt_text) -> bool
    {
        LLCB_OUT_APP_INFO("onJSDialogCallback: " <<
                          "origin_url: " << origin_url << " " <<
                          "dialog_type: " << static_cast<int>(dialog_type) << " " <<
                          "message_text: " << message_text << " " <<
                          "default_prompt_text: " << default_prompt_text
                         )

        return false;  // don't suppress - let CEF show its own default dialog
    });

    mCefBrowserManager->SetOnBeforeUnloadCallback(mCefBrowser, [this](const std::string & message_text, bool is_reload) -> bool
    {
        LLCB_OUT_APP_INFO("onBeforeUnloadCallback: " <<
                          "message_text: " << message_text << " " <<
                          "is_reload: " << is_reload
                         )

        return false;  // don't suppress - let CEF show its own default dialog
    });

    mCefBrowserManager->SetOnLoadErrorCallback(mCefBrowser, [this](int status, const std::string & error_text, const std::string & error_url)
    {
        LLCB_OUT_APP_ERR("onLoadErrorCallback: " <<
                         "Error URL: " << error_url << " " <<
                         "Error text: " << error_text << " " <<
                         "(error status: " << status << ")"
                        )
    });

    mCefBrowserManager->SetOnLoadStartCallback(mCefBrowser, [this]()
    {
        LLCB_OUT_APP_INFO("onLoadStartCallback")
    });

    mCefBrowserManager->SetOnPageChangedCallback(mCefBrowser, [this]()
    {
        // Signal only - no pixel data attached. update() picks this flag
        // up and pulls the frame via CopyLatestFrame() when convenient,
        // instead of polling it unconditionally every tick.
        mPageChanged = true;
    });

    mCefBrowserManager->SetOnQueryCallback(mCefBrowser, [this](const std::string & request)
    {
        LLCB_OUT_APP_INFO("onQueryCallback: " <<
                          "request " << request
                         )
    });

    mCefBrowserManager->SetOnStatusMessageCallback(mCefBrowser, [this](const std::string & message)
    {
        LLCB_OUT_APP_INFO("onStatusMessageCallback: " <<
                          "message " << message
                         )

    });

    mCefBrowserManager->SetOnTitleChangeCallback(mCefBrowser, [this](const std::string & title)
    {
        LLCB_OUT_APP_INFO("onTitleChangeCallback: " <<
                          "title " << title
                         )
    });

    mCefBrowserManager->SetOnTooltipCallback(mCefBrowser, [this](const std::string & text)
    {
        LLCB_OUT_APP_INFO("onTooltipCallback: " <<
                          "text " << text
                         )
    });

    mCefBrowserManager->SetOnFileDialogCallback(mCefBrowser, [this](int64_t dialogId, llCefFileDialogMode mode, const std::string & title, const std::string & defaultFile, const std::vector<std::string>& acceptFilters)
    {
        // Do not ask CEF to display a dialog - instead, we would use
        // our own and then respond with the selected file(s).
        LLCB_OUT_APP_INFO("onFileDialogCallback: " <<
                          "mode: " << static_cast<int>(mode) << " " <<
                          "title: " << title << " " <<
                          "default_file: " << defaultFile << " " <<
                          "accept_filters: " << acceptFilters.size()
                         )

        // fake a file selection, responding inline
        mCefBrowserManager->RespondToFileDialog(mCefBrowser, dialogId, { "C:\\foo\\bar\\flasm.txt" });
    });

    mCefBrowserManager->SetOnAuthRequestCallback(mCefBrowser, [this](const std::string & originUrl, const std::string & host, int port, const std::string & realm, const std::string & scheme, bool isProxy, std::string & username, std::string & password) -> bool
    {
        // Do not ask CEF to display a credentials dialog - instead, we
        // would show our own and answer with what the user entered. Called
        // on CEF's IO thread, so must answer synchronously, right here.
        LLCB_OUT_APP_INFO("onAuthRequestCallback: " <<
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

void singleBrowser::init()
{
    char exe_path[MAX_PATH + 1];
    // FIXME: windows only - make cross platform
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
    std::filesystem::path default_root_cache_path(exe_dir / "cef_profile");
    std::filesystem::path default_cache_path(exe_dir / "cef_profile" / "Default");
    std::filesystem::path default_log_file(exe_dir / "cef_log.txt");


    llCefBrowserLibInitOptions initOptions;
    initOptions.rootCachePath = default_root_cache_path.string();
    initOptions.logFile = default_log_file.string();
    initOptions.userAgentProduct = "llCefBrowser: SingleBrowser";
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

    // one cache for all browsers
    mCefBrowserManager = std::make_unique<llCefBrowserManager>(default_cache_path.string());

    mCefBrowser = mCefBrowserManager->CreateBrowser(mHomeUrl, mTextureWidth, mTextureHeight);

    if (! glfwInit())
    {
        exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    mWindow = glfwCreateWindow(mTextureWidth, mTextureHeight, mWindowTitle.c_str(), nullptr, nullptr);
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

    // Must run after initGLFWCallbacks(): ImGui_ImplGlfw_InitForOpenGL's
    // install_callbacks=true chain-calls whatever GLFW callback was already
    // registered on mWindow when it installs its own (see
    // imgui_impl_glfw.h) - so the app's own callbacks have to be in place
    // first, or there's nothing for ImGui to chain to and its callback
    // registration simply overwrites the app's instead.
    initGLFWCallbacks();
    initUI();

    initCEFCallbacks();
}

void singleBrowser::update()
{
    llCefBrowserLib::DoMessageLoopWork();
    mCefBrowserManager->Tick();

    if (mPageChanged)
    {
        mPageChanged = false;

        std::vector<uint8_t> frame;
        int w, h;
        if (mCefBrowserManager->CopyLatestFrame(mCefBrowser, frame, w, h))
        {
            glBindTexture(GL_TEXTURE_2D, mTextureId);
            if (w != mTexAllocWidth || h != mTexAllocHeight)
            {
                // The frame's dimensions changed
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, frame.data());
                mTexAllocWidth = w;
                mTexAllocHeight = h;
            }
            else
            {
                // The frame's dimensions did not change - just write in the data
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, frame.data());
            }
        }
    }
}

void singleBrowser::draw()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glLoadIdentity();

    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);

    glTexCoord2f(1.0f, 0.0f);
    glVertex2d(mTextureWidth, mTextureHeight - getUIHeight());

    glTexCoord2f(0.0f, 0.0f);
    glVertex2d(0, mTextureHeight - getUIHeight());

    glTexCoord2f(0.0f, 1.0f);
    glVertex2d(0, 0);

    glTexCoord2f(1.0f, 1.0f);
    glVertex2d(mTextureWidth, 0);

    glEnd();
}

void singleBrowser::run()
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

void singleBrowser::reset()
{
    resetUI();

    mCefBrowserManager->DestroyAll();
    // Pump a few more turns so each async close handshake finishes
    // before the manager (and its CefRequestContext reference) is destroyed.
    for (int i = 0; i < 30; ++i)
    {
        llCefBrowserLib::DoMessageLoopWork();
    }
    mCefBrowserManager.reset();
    // manager is now fully destroyed - safe to shut CEF down.
    llCefBrowserLib::SetJavaScriptBridge(nullptr);
    llCefBrowserLib::Shutdown();

    glDeleteTextures(1, &mTextureId);
    mTextureId = 0;

#if defined(WIN32)
    HWND hwnd = glfwGetWin32Window(mWindow);
    RemoveWindowSubclass(hwnd, keyEventSubClassProc, 0x01);
#endif

    glfwDestroyWindow(mWindow);

    glfwTerminate();
}

// Used in many places to move output down so as not to overlap UI. Callable
// before initUI() has created the ImGui context (e.g. from a mouse callback
// that fires during window setup, before init() reaches initUI()) -
// ImGui::GetFrameHeight() would be undefined behavior in that case, so
// this reports no UI height yet rather than touching a null context.
int singleBrowser::getUIHeight()
{
    if (! ImGui::GetCurrentContext())
    {
        return 0;
    }
    return (int)(ImGui::GetFrameHeight() * 3.6);
}

int singleBrowser::scaleMouseYforUI(double raw_y)
{
    return (raw_y - getUIHeight()) * (double)mTextureHeight / (double)(mTextureHeight - getUIHeight());
}

void singleBrowser::initUI()
{
    IMGUI_CHECKVERSION();
    ImGui:: CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();
    io.FontGlobalScale = 1.2f;
    ImGui_ImplGlfw_InitForOpenGL(mWindow, true);
    ImGui_ImplOpenGL2_Init();
}

void singleBrowser::updateUI()
{
    // main host for UI - URL and bookmarks drop-down
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Turn off window decoaration - we don't want
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

    // Write menu bar and associated actions
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
            if (ImGui::MenuItem("Go Home"))
            {
                mCefBrowserManager->Navigate(mCefBrowser, mHomeUrl);
            }
            if (ImGui::MenuItem("Back", nullptr, false, mCefBrowserManager->CanGoBack(mCefBrowser)))
            {
                mCefBrowserManager->GoBack(mCefBrowser);
            }
            if (ImGui::MenuItem("Forward", nullptr, false, mCefBrowserManager->CanGoForward(mCefBrowser)))
            {
                mCefBrowserManager->GoForward(mCefBrowser);
            }
            if (ImGui::MenuItem("Reload"))
            {
                mCefBrowserManager->Reload(mCefBrowser, false);
            }
            if (ImGui::MenuItem("Reload (Ignore Cache)"))
            {
                mCefBrowserManager->Reload(mCefBrowser, true);
            }
            if (ImGui::MenuItem("Stop"))
            {
                mCefBrowserManager->StopLoad(mCefBrowser);
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Show Dev Console"))
            {
                mCefBrowserManager->ShowDevTools(mCefBrowser);
            }
            if (ImGui::MenuItem("Execute Test JavaScript"))
            {
                mCefBrowserManager->ExecuteJavaScript(mCefBrowser, "document.body.style.backgroundColor = 'red';");
            }

            // Resize test is hard to implement here because
            // for simplicity, we use an OpenGL ortho mode where
            // the window size and the browser texture size are the same,
            // so resizing the browser also requires resizing the window
            // and associated OpenGL viewport and texture.
            // It's doable for I want to keep this example simple.
            //if (ImGui::BeginMenu("Resize"))
            //{
            // std::vector<std::pair<int, int>> options;
            // options.push_back(std::make_pair(512, 512));
            // options.push_back(std::make_pair(800, 800));
            // options.push_back(std::make_pair(1024, 1024));
            // options.push_back(std::make_pair(1536, 1536));
            // options.push_back(std::make_pair(2048, 2048));
            // std::vector<std::pair<int, int>>::iterator options_iter = options.begin();
            // while (options_iter != options.end())
            // {
            // std::string label = std::to_string(options_iter->first) + " x " + std::to_string(options_iter->second);
            // if (ImGui::MenuItem(label.c_str()))
            // {
            // resizeBrowser(options_iter->first, options_iter->second);
            // }
            // ++options_iter;
            // }
            // ImGui::EndMenu();
            //}
            if (ImGui::BeginMenu("Zoom"))
            {
                std::vector<int> options = {25, 50, 100, 200, 400};
                std::vector<int>::iterator options_iter = options.begin();
                while (options_iter != options.end())
                {
                    std::string label = std::to_string(*options_iter) + "%";
                    if (ImGui::MenuItem(label.c_str()))
                    {
                        mCefBrowserManager->SetPageZoom(mCefBrowser, static_cast<float>(*options_iter) / 100.0f);
                    }
                    ++options_iter;
                }
                ImGui::EndMenu();
            }

            // Bound to Alt+<letter> rather than the usual Ctrl+Z/X/C/V/A
            // back when these were keyboard shortcuts, since those raw key
            // combinations already reach CEF via SendKeyEvent and are
            // likely handled as its own built-in accelerators - now that
            // they're menu items instead, that reasoning no longer applies,
            // but the same commands are kept together here as an Edit menu.
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo"))
                {
                    mCefBrowserManager->Undo(mCefBrowser);
                }
                if (ImGui::MenuItem("Redo"))
                {
                    mCefBrowserManager->Redo(mCefBrowser);
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Cut"))
                {
                    mCefBrowserManager->Cut(mCefBrowser);
                }
                if (ImGui::MenuItem("Copy"))
                {
                    mCefBrowserManager->Copy(mCefBrowser);
                }
                if (ImGui::MenuItem("Paste"))
                {
                    mCefBrowserManager->Paste(mCefBrowser);
                }
                if (ImGui::MenuItem("Delete"))
                {
                    mCefBrowserManager->Delete(mCefBrowser);
                }
                if (ImGui::MenuItem("Select All"))
                {
                    mCefBrowserManager->SelectAll(mCefBrowser);
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
            ss << "       IMGUI version: " << IMGUI_VERSION << std::endl;
            ss << std::endl;
            ss << "Interact with the page using the mouse, left mouse button and scroll wheel." << std::endl << std::endl;
            ss << "ESC key to exit." << std::endl;

            ImGui::Text(ss.str().c_str());
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // Write freeform URL entry bar
    ImGui::SetNextItemWidth(main_viewport->Size.x);
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(64, 64, 100, 255));
    static char url_buffer[4096];
    if (ImGui::InputTextWithHint("UrlInout", "Enter a URL",
                                 url_buffer,
                                 IM_ARRAYSIZE(url_buffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
    {
        mCefBrowserManager->Navigate(mCefBrowser, url_buffer);
    }
    ImGui::PopStyleColor();

    // Write bookmarks bar - a good place to add useful or interesting bookmarks
    const char* items[] =
    {
        "chrome://version",
        "https://sl-viewer-media-system.s3.amazonaws.com/bookmarks/index.html",
        "https://viewer-login.agni.lindenlab.com/",
        "https://secondlife.com"
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
                mCefBrowserManager->Navigate(mCefBrowser, current_item);
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

void singleBrowser::resetUI()
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

    singleBrowser* app = new singleBrowser();

    app->init();

    app->run();

    app->reset();
}
