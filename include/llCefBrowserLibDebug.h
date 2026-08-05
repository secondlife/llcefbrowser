
/**
 *
 * @file llCefBrowserLibDebug.h
 * @brief Debug/console output macros used throughout the library and its examples.
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

// Enable/disable debug output - can be a bit noisy but handy for
// development and debugging. Should be disabled for production builds.
#define ENABLE_LLCB_DEBUG_OUTPUT true

// Enable/disable debug output with color - useful for log files
// or environments that don't support ANSI color codes
#define ENABLE_LLCB_COLOR_DEBUG_OUTPUT true

// Now the big ball of macros for debug output - these will be
// no-ops if ENABLE_LLCB_DEBUG_OUTPUT is false
#if (ENABLE_LLCB_DEBUG_OUTPUT)
#include <iostream>
#if (ENABLE_LLCB_COLOR_DEBUG_OUTPUT)
#define LLCB_OUT(t,l,m) \
    std::cout << "\033[38;5;160m"<< t << "\033[0m:" \
              << "\033[38;5;196m"<< l << "\033[0m> " \
              << "\033[38;5;194m"<< m << "\033[0m " \
              << std::endl;
#define LLCB_OUT_CEF "\033[38;5;141mCEF\033[0m"
#define LLCB_OUT_APP "\033[38;5;228mAPP\033[0m"
#define LLCB_OUT_INFO "\033[38;5;120mINFO\033[0m"
#define LLCB_OUT_WARN "\033[38;5;208mWARN\033[0m"
#define LLCB_OUT_ERR "\033[38;5;160mERR\033[0m"
#define LLCB_OUT_TIME "\033[38;5;25mTIME\033[0m"
#define LLCB_OUT_CEF_INFO(m) LLCB_OUT(LLCB_OUT_CEF, LLCB_OUT_INFO, m)
#define LLCB_OUT_CEF_WARN(m) LLCB_OUT(LLCB_OUT_CEF, LLCB_OUT_WARN, m)
#define LLCB_OUT_CEF_ERR(m) LLCB_OUT(LLCB_OUT_CEF, LLCB_OUT_ERR, m)
#define LLCB_OUT_APP_INFO(m) LLCB_OUT(LLCB_OUT_APP, LLCB_OUT_INFO, m)
#define LLCB_OUT_APP_WARN(m) LLCB_OUT(LLCB_OUT_APP, LLCB_OUT_WARN, m)
#define LLCB_OUT_APP_ERR(m) LLCB_OUT(LLCB_OUT_APP, LLCB_OUT_ERR, m)
#define LLCB_INITTIME(v, desc) \
    LLCB_OUT(LLCB_OUT_CEF, LLCB_OUT_TIME, \
             "\033[38;5;105m"<< "INIT TIME: " << "\033[0m" \
             << "\033[38;5;220m"<< desc << "\033[0m") \
    const auto v = std::chrono::steady_clock::now();
#define LLCB_MARKTIME(v, desc) \
    LLCB_OUT(LLCB_OUT_CEF, LLCB_OUT_TIME, \
             "\033[38;5;105m"<< "MARK TIME: " << "\033[0m" \
             << "\033[38;5;220m"<< desc << "\033[0m -> " \
             << "\033[38;5;99m"<< duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - v) << "\033[0m")
#else
#define LLCB_OUT_NOCOL(t,l,m) \
    std::cout << t << ":" << l << "> " << m << std::endl;
#define LLCB_OUT_CEF_NOCOL "CEF"
#define LLCB_OUT_APP_NOCOL "APP"
#define LLCB_OUT_INFO_NOCOL "INFO"
#define LLCB_OUT_WARN_NOCOL "WARN"
#define LLCB_OUT_ERR_NOCOL "ERR"
#define LLCB_OUT_TIME_NOCOL "TIME"
#define LLCB_OUT_CEF_INFO(m) LLCB_OUT_NOCOL(LLCB_OUT_CEF_NOCOL, LLCB_OUT_INFO_NOCOL, m)
#define LLCB_OUT_CEF_WARN(m) LLCB_OUT_NOCOL(LLCB_OUT_CEF_NOCOL, LLCB_OUT_WARN_NOCOL, m)
#define LLCB_OUT_CEF_ERR(m) LLCB_OUT_NOCOL(LLCB_OUT_CEF_NOCOL, LLCB_OUT_ERR_NOCOL, m)
#define LLCB_OUT_APP_INFO(m) LLCB_OUT_NOCOL(LLCB_OUT_APP_NOCOL, LLCB_OUT_INFO_NOCOL, m)
#define LLCB_OUT_APP_WARN(m) LLCB_OUT_NOCOL(LLCB_OUT_APP_NOCOL, LLCB_OUT_WARN_NOCOL, m)
#define LLCB_OUT_APP_ERR(m) LLCB_OUT_NOCOL(LLCB_OUT_APP_NOCOL, LLCB_OUT_ERR_NOCOL, m)
#define LLCB_INITTIME(v, desc) \
    LLCB_OUT_NOCOL(LLCB_OUT_CEF_NOCOL, LLCB_OUT_TIME_NOCOL, \
                   "INIT TIME: " \
                   << desc) \
    const auto v = std::chrono::steady_clock::now();
#define LLCB_MARKTIME(v, desc) \
    LLCB_OUT_NOCOL(LLCB_OUT_CEF_NOCOL, LLCB_OUT_TIME_NOCOL, \
                   "MARK TIME: " << desc << " -> " \
                   << duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - v))
#endif  // ENABLE_LLCB_COLOR_DEBUG_OUTPUT
#else
#define LLCB_OUT_CEF_INFO(m)
#define LLCB_OUT_CEF_WARN(m)
#define LLCB_OUT_CEF_ERR(m)
#define LLCB_OUT_APP_INFO(m)
#define LLCB_OUT_APP_WARN(m)
#define LLCB_OUT_APP_ERR(m)
#define LLCB_INITTIME(v, desc)
#define LLCB_MARKTIME(v, desc)
#endif // ENABLE_LLCB_DEBUG_OUTPUT
