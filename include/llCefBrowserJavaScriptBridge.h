/**
 *
 * @file llCefBrowserJavaScriptBridge.h
 * @brief Public interface an application implements to receive window.cefQuery(...) calls from web content.
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
#include "llCefBrowserHandle.h"
#include <cstdint>
#include <string>

// Public, CEF-free interface. Implement this to receive JS bridge queries
// (window.cefQuery(...) calls from web content) from any browser created
// through llCefBrowserManager. Register your implementation once via
// llCefBrowserLib::SetJavaScriptBridge(), before creating any browsers -
// it applies process-wide, not per-manager, since the underlying CEF
// message router is itself a process-wide construct.
class llCefBrowserJavaScriptBridge {
    public:
        virtual ~llCefBrowserJavaScriptBridge() = default;

        // Called on the CEF UI thread when JS content calls
        // window.cefQuery({request: ..., onSuccess: ..., onFailure: ...}).
        //
        // Return true if you will respond to this request - either
        // synchronously (call llCefBrowserLib::RespondToQuery() before
        // returning) or asynchronously later (store queryId and call it once
        // your response is ready). Return false if you don't want to handle
        // this particular request; CEF reports failure to the page
        // automatically in that case.
        virtual bool OnQuery(llCefBrowserHandle handle, int64_t queryId,
                             const std::string& request, bool persistent) = 0;

        // Called if the page cancels a pending query (e.g. by navigating away)
        // before you've responded to it. Stop holding onto queryId - calling
        // llCefBrowserLib::RespondToQuery() with it afterward is a no-op.
        virtual void OnQueryCanceled(llCefBrowserHandle handle, int64_t queryId) {}
};
