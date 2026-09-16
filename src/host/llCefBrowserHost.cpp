/**
 *
 * @file llCefBrowserHost.cpp
 * @brief macOS helper-process entry point (Renderer/GPU/Plugin/Alerts) for llcefbrowser.
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

// CEF's multi-process architecture needs a real, separately-signed .app
// bundle per sub-process type on macOS (Renderer/GPU/Plugin/Alerts, see
// LLCEFBROWSER_HELPER_APP_SUFFIXES in CMakeLists.txt) -- unlike Windows/
// Linux, where llmediaproducer.cpp re-execs itself for this purpose via
// llCefBrowserLib::ExecuteSubProcess(), macOS sandboxing/Info.plist
// requirements make that single-executable trick unavailable. Each of the
// 5 tiny helper .app bundles built from this same file just hands off to
// the exact same ExecuteSubProcess() the main executable already calls on
// every other platform; no app-specific logic belongs here at all.
//
// CefScopedLibraryLoader IS still required here, unconditionally, even
// though this executable also hard-links the framework at build time (see
// target_link_libraries in CMakeLists.txt) -- an earlier version of this
// file dropped it on the theory that the hard link made it redundant. That
// was wrong: per CEF's own cef_library_loader.h, "loading at runtime
// instead of linking directly is a requirement of the macOS sandbox
// implementation" -- the hard link only satisfies dyld's own load-time
// requirement; CefExecuteProcess() itself calls through an internal
// function-pointer table that only this loader populates, and crashes
// (SIGSEGV, null function pointer) without it. Confirmed by reproducing
// the exact same crash in the example apps (see single-browser-example.cpp)
// and fixing it the same way.
#include "include/wrapper/cef_library_loader.h"
#include "llCefBrowserLib.h"

int main(int argc, char* argv[])
{
    CefScopedLibraryLoader library_loader;
    if (! library_loader.LoadInHelper())
    {
        return 1;
    }

    return llCefBrowserLib::ExecuteSubProcess(argc, argv);
}
