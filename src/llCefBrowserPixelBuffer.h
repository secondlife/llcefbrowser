/**
 *
 * @file llCefBrowserPixelBuffer.h
 * @brief Double-buffered BGRA32 pixel storage for one browser's latest painted frame.
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
#include <cstring>
#include <mutex>
#include <vector>

// Double-buffered BGRA32 pixel storage. Write() is called from CEF's
// OnPaint (CEF UI thread, single-threaded message loop mode). CopyLatest()
// is called from your render/GL thread once per frame. A mutex separates
// the two since they are never guaranteed to run on the same thread.
class llCefBrowserPixelBuffer {
    public:
        void Resize(int w, int h) {
            std::lock_guard<std::mutex> lock(mMutex);
            mWidth = w;
            mHeight = h;
            const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
            mFront.assign(bytes, 0);
            mBack.assign(bytes, 0);
            mDirty = false;
        }

        // Like Resize(), but also marks the (blank) buffer dirty so the very
        // next CopyLatest() call picks up a correctly-sized, empty frame right
        // away. Call this when a resize is requested mid-life (as opposed to
        // Resize(), which is for initial construction) so stale, old-size
        // content can't linger in the GL texture during the async gap before
        // CEF delivers its first real repaint at the new dimensions.
        void ResetForResize(int w, int h) {
            std::lock_guard<std::mutex> lock(mMutex);
            mWidth = w;
            mHeight = h;
            const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
            mFront.assign(bytes, 0);
            mBack.assign(bytes, 0);
            mDirty = true;
        }

        // buffer is BGRA32, width*height*4 bytes, as handed to OnPaint by CEF.
        void Write(const void* buffer, int w, int h) {
            std::lock_guard<std::mutex> lock(mMutex);
            if (w != mWidth || h != mHeight) {
                mWidth = w;
                mHeight = h;
                const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
                mBack.assign(bytes, 0);
                mFront.assign(bytes, 0);
            }
            std::memcpy(mBack.data(), buffer, mBack.size());
            std::swap(mFront, mBack);
            mDirty = true;
        }

        // Returns true and fills dst/w/h if a new frame has arrived since the
        // last successful call. Returns false (dst untouched) otherwise, so
        // callers can skip the GL upload when nothing changed.
        bool CopyLatest(std::vector<uint8_t>& dst, int& w, int& h) {
            std::lock_guard<std::mutex> lock(mMutex);
            if (! mDirty) {
                return false;
            }
            dst = mFront;
            w = mWidth;
            h = mHeight;
            mDirty = false;
            return true;
        }

        int Width() const {
            return mWidth;
        }
        int Height() const {
            return mHeight;
        }

    private:
        mutable std::mutex mMutex;
        std::vector<uint8_t> mFront;
        std::vector<uint8_t> mBack;
        int mWidth = 0;
        int mHeight = 0;
        bool mDirty = false;
};
