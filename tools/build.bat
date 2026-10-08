@if not exist "tools/" (
    @echo Run this command from the project root directory
    @goto end
)

@rem Note: let's not make the build directory a variable with this line in play - just in case...
if exist build rmdir build /s /q

@rem Same internally-built, media-codec-enabled CEF package autobuild.xml's
@rem own cef-bin installable points at (see that file) -- built by hand
@rem (too slow/resource-heavy for GitHub CI) and uploaded once to this URL.
@rem A plain Spotify Automated Builds package
@rem (https://cef-builds.spotifycdn.com/) works here too if you just need a
@rem quick local build and don't care about codec support, but then this
@rem local dev build no longer matches what actually ships.
set CEF_MINIMAL_URL=https://automated-builds-secondlife-com.s3.us-east-1.amazonaws.com/gh/secondlife/cef/cef_bin-154.0.34_g14c5a08_chromium-154.0.8037.98-windows64-262801230.tar.zst

@rem Must match the Viewer's own LL_BUILD_RELEASE flags exactly (see that
@rem repo's build-vc170-64/CMakeCache.txt, LL_BUILD_RELEASE_ENV) -- in
@rem particular /D_SECURE_STL=0 and /D_HAS_ITERATOR_DEBUGGING=0, which
@rem change MSVC's STL ABI (std::string/std::vector/std::function layout).
@rem Without these matching, this .lib is binary-incompatible with
@rem SLCefProducer.exe/secondlife-bin.exe even though it compiles and links
@rem fine -- confirmed the hard way 2026-10-08: audio capture silently
@rem never fired with a mismatched local build, while everything else
@rem (video, cursor, JS bridge) looked fine, since those touch far less
@rem STL surface at the library boundary.
set LL_BUILD_RELEASE=/MD /O2 /Ob2 /std:c++20 /permissive- /Zc:wchar_t /Zi /GR /DEBUG /DLL_RELEASE=1 /DLL_RELEASE_FOR_DOWNLOAD=1 /DNDEBUG /D_SECURE_STL=0 /D_HAS_ITERATOR_DEBUGGING=0 /DWIN32 /D_WINDOWS /DLL_WINDOWS=1 /DUNICODE /D_UNICODE /DWINVER=0x0602 /D_WIN32_WINNT=0x0602

cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_CXX_FLAGS="%LL_BUILD_RELEASE%" -DCEF_RUNTIME_LIBRARY_FLAG=/MD -DUSE_SANDBOX=OFF -DCEF_PACKAGE_URL=%CEF_MINIMAL_URL%
cmake --build build --config Release

:end
