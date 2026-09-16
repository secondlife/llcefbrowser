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

#if defined(_WIN32)
#include <windows.h> // GetModuleFileNameA
#elif defined(__APPLE__)
#include <climits>       // PATH_MAX
#include <mach-o/dyld.h> // _NSGetExecutablePath
#include "include/wrapper/cef_library_loader.h"
#else // Linux
#include <climits>  // PATH_MAX
#include <unistd.h> // readlink
#endif

#include "llCefBrowserManager.h"
#include "llCefBrowserLib.h"
#include "llCefBrowserJavaScriptBridge.h"
#include "llCefBrowserLibDebug.h"

#include "single-browser-example.h"

namespace {
    // Was "FIXME: windows only - make cross platform" -- same portable
    // get_exe_path() approach as the Viewer's own llmediaproducer.cpp.
#if defined(_WIN32)
    std::filesystem::path get_exe_path()
    {
        char buf[MAX_PATH + 1];
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        return std::filesystem::path(buf);
    }
#elif defined(__APPLE__)
    std::filesystem::path get_exe_path()
    {
        char buf[PATH_MAX];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) != 0) return {};
        std::error_code ec;
        const auto resolved = std::filesystem::canonical(buf, ec);
        return ec ? std::filesystem::path(buf) : resolved;
    }
#else // Linux
    std::filesystem::path get_exe_path()
    {
        char buf[PATH_MAX];
        const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0) return {};
        buf[n] = '\0';
        return std::filesystem::path(buf);
    }
#endif
}

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

namespace {

    // GLFW's own key/mods encoding is already OS-neutral -- this single
    // translator now covers every platform this example builds on,
    // replacing the old Windows-only native-window message hook this file
    // used before llcefbrowser's own SendKeyEvent became platform-neutral
    // (see its own comment): that hook only ever existed because Dullahan's
    // old SendKeyEvent needed a raw Win32 message triple straight from a
    // WndProc, which GLFW's cross-platform callbacks can't supply on their
    // own. Values checked directly against the vendored glfw3.h, not
    // assumed.
    int glfwKeyToWindowsKeyCode(int key)
    {
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return key; // matches VK_A..VK_Z
        if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return key; // matches VK_0..VK_9
        switch (key)
        {
            case GLFW_KEY_SPACE:         return 0x20; // VK_SPACE
            case GLFW_KEY_ENTER:
            case GLFW_KEY_KP_ENTER:      return 0x0D; // VK_RETURN
            case GLFW_KEY_TAB:           return 0x09; // VK_TAB
            case GLFW_KEY_BACKSPACE:     return 0x08; // VK_BACK
            case GLFW_KEY_DELETE:        return 0x2E; // VK_DELETE
            case GLFW_KEY_ESCAPE:        return 0x1B; // VK_ESCAPE
            case GLFW_KEY_HOME:          return 0x24; // VK_HOME
            case GLFW_KEY_END:           return 0x23; // VK_END
            case GLFW_KEY_PAGE_UP:       return 0x21; // VK_PRIOR
            case GLFW_KEY_PAGE_DOWN:     return 0x22; // VK_NEXT
            case GLFW_KEY_LEFT:          return 0x25; // VK_LEFT
            case GLFW_KEY_UP:            return 0x26; // VK_UP
            case GLFW_KEY_RIGHT:         return 0x27; // VK_RIGHT
            case GLFW_KEY_DOWN:          return 0x28; // VK_DOWN
            case GLFW_KEY_INSERT:        return 0x2D; // VK_INSERT
            case GLFW_KEY_CAPS_LOCK:     return 0x14; // VK_CAPITAL
            case GLFW_KEY_LEFT_SHIFT:
            case GLFW_KEY_RIGHT_SHIFT:   return 0x10; // VK_SHIFT
            case GLFW_KEY_LEFT_CONTROL:
            case GLFW_KEY_RIGHT_CONTROL: return 0x11; // VK_CONTROL
            case GLFW_KEY_LEFT_ALT:
            case GLFW_KEY_RIGHT_ALT:     return 0x12; // VK_MENU
            case GLFW_KEY_LEFT_SUPER:    return 0x5B; // VK_LWIN
            case GLFW_KEY_RIGHT_SUPER:   return 0x5C; // VK_RWIN
            case GLFW_KEY_F1:  return 0x70;
            case GLFW_KEY_F2:  return 0x71;
            case GLFW_KEY_F3:  return 0x72;
            case GLFW_KEY_F4:  return 0x73;
            case GLFW_KEY_F5:  return 0x74;
            case GLFW_KEY_F6:  return 0x75;
            case GLFW_KEY_F7:  return 0x76;
            case GLFW_KEY_F8:  return 0x77;
            case GLFW_KEY_F9:  return 0x78;
            case GLFW_KEY_F10: return 0x79;
            case GLFW_KEY_F11: return 0x7A;
            case GLFW_KEY_F12: return 0x7B;
            case GLFW_KEY_KP_0: return 0x60;
            case GLFW_KEY_KP_1: return 0x61;
            case GLFW_KEY_KP_2: return 0x62;
            case GLFW_KEY_KP_3: return 0x63;
            case GLFW_KEY_KP_4: return 0x64;
            case GLFW_KEY_KP_5: return 0x65;
            case GLFW_KEY_KP_6: return 0x66;
            case GLFW_KEY_KP_7: return 0x67;
            case GLFW_KEY_KP_8: return 0x68;
            case GLFW_KEY_KP_9: return 0x69;
            case GLFW_KEY_KP_MULTIPLY: return 0x6A;
            case GLFW_KEY_KP_ADD:      return 0x6B;
            case GLFW_KEY_KP_SUBTRACT: return 0x6D;
            case GLFW_KEY_KP_DECIMAL:  return 0x6E;
            case GLFW_KEY_KP_DIVIDE:   return 0x6F;
            case GLFW_KEY_COMMA:         return 0xBC; // VK_OEM_COMMA
            case GLFW_KEY_PERIOD:        return 0xBE; // VK_OEM_PERIOD
            case GLFW_KEY_SLASH:         return 0xBF; // VK_OEM_2
            case GLFW_KEY_SEMICOLON:     return 0xBA; // VK_OEM_1
            case GLFW_KEY_EQUAL:         return 0xBB; // VK_OEM_PLUS
            case GLFW_KEY_MINUS:         return 0xBD; // VK_OEM_MINUS
            case GLFW_KEY_LEFT_BRACKET:  return 0xDB; // VK_OEM_4
            case GLFW_KEY_RIGHT_BRACKET: return 0xDD; // VK_OEM_6
            case GLFW_KEY_BACKSLASH:     return 0xDC; // VK_OEM_5
            case GLFW_KEY_APOSTROPHE:    return 0xDE; // VK_OEM_7
            case GLFW_KEY_GRAVE_ACCENT:  return 0xC0; // VK_OEM_3
            default: return 0;
        }
    }

    uint32_t glfwModsToCefModifiers(int mods, int key)
    {
        uint32_t modifiers = 0;
        if (mods & GLFW_MOD_SHIFT)     modifiers |= llCefKeyModShift;
        if (mods & GLFW_MOD_CONTROL)   modifiers |= llCefKeyModControl;
        if (mods & GLFW_MOD_ALT)       modifiers |= llCefKeyModAlt;
        if (mods & GLFW_MOD_SUPER)     modifiers |= llCefKeyModCommand;
        if (mods & GLFW_MOD_CAPS_LOCK) modifiers |= llCefKeyModCapsLock;
        if (mods & GLFW_MOD_NUM_LOCK)  modifiers |= llCefKeyModNumLock;

        switch (key)
        {
            case GLFW_KEY_LEFT_SHIFT: case GLFW_KEY_LEFT_CONTROL:
            case GLFW_KEY_LEFT_ALT:   case GLFW_KEY_LEFT_SUPER:
                modifiers |= llCefKeyModIsLeft;
                break;
            case GLFW_KEY_RIGHT_SHIFT: case GLFW_KEY_RIGHT_CONTROL:
            case GLFW_KEY_RIGHT_ALT:   case GLFW_KEY_RIGHT_SUPER:
                modifiers |= llCefKeyModIsRight;
                break;
        }
        if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL)
        {
            modifiers |= llCefKeyModIsKeyPad;
        }

        return modifiers;
    }

}  // namespace

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

    if (action == GLFW_REPEAT)
    {
        // A held key auto-repeating -- still a "key down" from CEF's own
        // perspective (matching how a real WM_KEYDOWN also just keeps
        // firing while a key is held on Windows).
        action = GLFW_PRESS;
    }
    if (action != GLFW_PRESS && action != GLFW_RELEASE)
    {
        return;
    }

    const llCefKeyEventType type = (action == GLFW_PRESS) ? llCefKeyEventType::RawKeyDown : llCefKeyEventType::KeyUp;
    mCefBrowserManager->SendKeyEvent(mCefBrowser, type, glfwModsToCefModifiers(mods, key),
        glfwKeyToWindowsKeyCode(key), scancode, 0, 0, /*is_system_key=*/false);
}

void singleBrowser::charCallback(unsigned int codepoint)
{
    // Composed/localized text input (shift, caps lock, dead keys, IME, etc.
    // already resolved) -- GLFW's own equivalent of a Windows WM_CHAR,
    // fired separately from keyCallback()'s raw key down/up. Same ImGui
    // focus-stealing guard as keyCallback() -- typing into a UI text field
    // like the URL bar must not also reach the page.
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard)
    {
        return;
    }

    mCefBrowserManager->SendKeyEvent(mCefBrowser, llCefKeyEventType::Char, 0,
        (int)codepoint, 0, codepoint, codepoint, /*is_system_key=*/false);
}

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
    glfwSetCharCallback(mWindow, charCallbackStatic);
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
    std::filesystem::path exe_dir = get_exe_path().parent_path();
    std::filesystem::path default_root_cache_path(exe_dir / "cef_profile");
    std::filesystem::path default_cache_path(exe_dir / "cef_profile" / "Default");
    std::filesystem::path default_log_file(exe_dir / "cef_log.txt");


    llCefBrowserLibInitOptions initOptions;
    initOptions.rootCachePath = default_root_cache_path.string();
    initOptions.logFile = default_log_file.string();
    initOptions.userAgentProduct = "llCefBrowser: SingleBrowser";
#if defined(__APPLE__)
    // Not a real .app bundle -- point CEF straight at the framework it was
    // built against (see LLCEFBROWSER_EXAMPLE_CEF_FRAMEWORK_DIR's own
    // comment in CMakeLists.txt) instead of its bundle-relative default.
    // resources_dir_path defaults to "<framework>/Resources" when
    // framework_dir_path is set and resources_dir_path itself is left
    // empty... but CEF's own docs describe that fallback only for the
    // bundle case, so set it explicitly rather than rely on it.
    initOptions.frameworkDirPath = LLCEFBROWSER_EXAMPLE_CEF_FRAMEWORK_DIR;
    initOptions.resourcesDirPath = std::string(LLCEFBROWSER_EXAMPLE_CEF_FRAMEWORK_DIR) + "/Resources";
    initOptions.mainBundlePath = exe_dir.string();
    // Empty means "look for a bundled Helper.app" on macOS specifically
    // (unlike Windows/Linux, where it already means "re-exec myself") --
    // this isn't bundled, so every CEF sub-process (GPU/renderer/network)
    // fails to launch at all without this being set explicitly.
    initOptions.browserSubprocessPath = get_exe_path().string();
#endif
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

    // one cache for all browsers -- this example doesn't care about the UI-
    // vs-prim cookie isolation, so use the same path for both contexts
    mCefBrowserManager = std::make_unique<llCefBrowserManager>(default_cache_path.string(), default_cache_path.string());

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

    // Required every tick -- CreateBrowser() always uses external-begin-frame
    // mode now (see SendExternalBeginFrame()'s own doc comment), so without
    // this CEF never composites a frame at all: no error, no OnPageChanged
    // callback either, just a permanently black window.
    mCefBrowserManager->SendExternalBeginFrame(mCefBrowser);

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
#if defined(__APPLE__)
    // Required on macOS, unconditionally, before any other CEF call -- per
    // CEF's own cef_library_loader.h: "Loading at runtime instead of
    // linking directly is a requirement of the macOS sandbox
    // implementation." Confirmed the hard way: omitting this crashes with
    // SIGSEGV inside CefExecuteProcess (a null internal function-pointer
    // table CEF only populates via this loader) even though the framework
    // is *also* linked at build time -- that link only satisfies dyld's
    // own load-time requirement, it's a separate mechanism from this.
    CefScopedLibraryLoader library_loader;
    if (! library_loader.LoadInMain())
    {
        return 1;
    }
#endif

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
