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
// Confirmed empirically (not just assumed): this executable links against
// llcefbrowser, which itself links directly against the CEF framework (see
// CMakeLists.txt) -- that gives it a hard, build-time
// @executable_path/../Frameworks/Chromium Embedded Framework.framework
// dependency, resolved automatically by dyld before main() ever runs
// (verified: running this binary before that framework is in place fails at
// the dyld loader, never reaching main() at all). CefScopedLibraryLoader
// (CEF's own dlopen-based alternative for a helper that does NOT hard-link
// the framework) would be redundant here and is deliberately not used.
#include "llCefBrowserLib.h"

int main(int argc, char* argv[])
{
    return llCefBrowserLib::ExecuteSubProcess(argc, argv);
}
