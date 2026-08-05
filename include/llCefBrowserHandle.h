/**
 *
 * @file llCefBrowserHandle.h
 * @brief Opaque browser handle type and CEF-free mirror types (mouse button, cursor, file dialog, JS dialog, cookie) shared across the public API.
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

// Opaque handle into llCefBrowserManager's slot map. Combines a slot index
// with a generation counter so that a stale handle (referring to a browser
// that has since been destroyed and whose slot was recycled) is safely
// detected instead of aliasing a new browser.
struct llCefBrowserHandle
{
    uint32_t index = 0xFFFFFFFFu;
    uint32_t generation = 0;

    bool IsValid() const {
        return index != 0xFFFFFFFFu;
    }
    bool operator==(const llCefBrowserHandle& o) const {
        return index == o.index && generation == o.generation;
    }
    bool operator!=(const llCefBrowserHandle& o) const {
        return !(*this == o);
    }

    static llCefBrowserHandle Invalid() {
        return llCefBrowserHandle{};
    }
};

// CEF-free stand-in for cef_mouse_button_type_t, used by
// llCefBrowserManager::SendMouseClickEvent so callers never need a CEF
// header just to report which button was pressed.
enum class llCefMouseButton
{
    Left,
    Middle,
    Right
};

// CEF-free mirror of cef_cursor_type_t, used by
// llCefBrowserManager::SetOnCursorChangedCallback. Covers CEF's stable core
// cursor set; anything not explicitly listed here (including any custom
// cursor image) reports as Custom rather than failing to compile against a
// newer/older CEF version's exact enum.
enum class llCefCursorType
{
    Pointer,
    Cross,
    Hand,
    IBeam,
    Wait,
    Help,
    EastResize,
    NorthResize,
    NorthEastResize,
    NorthWestResize,
    SouthResize,
    SouthEastResize,
    SouthWestResize,
    WestResize,
    NorthSouthResize,
    EastWestResize,
    NorthEastSouthWestResize,
    NorthWestSouthEastResize,
    ColumnResize,
    RowResize,
    MiddlePanning,
    EastPanning,
    NorthPanning,
    NorthEastPanning,
    NorthWestPanning,
    SouthPanning,
    SouthEastPanning,
    SouthWestPanning,
    WestPanning,
    Move,
    VerticalText,
    Cell,
    ContextMenu,
    Alias,
    Progress,
    NoDrop,
    Copy,
    None,
    NotAllowed,
    ZoomIn,
    ZoomOut,
    Grab,
    Grabbing,
    Custom
};

// CEF-free mirror of cef_file_dialog_mode_t, used by
// llCefBrowserManager::SetOnFileDialogCallback.
enum class llCefFileDialogMode
{
    Open,
    OpenMultiple,
    OpenFolder,
    Save
};

// CEF-free mirror of cef_jsdialog_type_t, used by
// llCefBrowserManager::SetOnJSDialogCallback.
enum class llCefJSDialogType
{
    Alert,
    Confirm,
    Prompt
};

// CEF-free mirror of the fields on cef_cookie_t, used by
// llCefBrowserManager::GetCookies.
struct llCefCookie
{
    std::string name;
    std::string value;
    std::string domain;
    std::string path;
    bool secure = false;
    bool httpOnly = false;
};
