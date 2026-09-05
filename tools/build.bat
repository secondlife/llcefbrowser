@if not exist "tools/" (
    @echo Run this command from the project root directory
    @goto end
)

@rem Note: let's not make the build directory a variable with this line in play - just in case...
if exist build rmdir build /s /q

@rem Same internally-built, media-codec-enabled CEF package autobuild.xml's
@rem own cef-bin installable points at (see that file) -- built by hand
@rem (too slow/resource-heavy for GitHub CI) and uploaded once to this URL,
@rem which both this project and secondlife/dullahan consume from. A plain
@rem Spotify Automated Builds package (https://cef-builds.spotifycdn.com/)
@rem works here too if you just need a quick local build and don't care
@rem about codec support, but then this local dev build no longer matches
@rem what actually ships.
set CEF_MINIMAL_URL=https://automated-builds-secondlife-com.s3.us-east-1.amazonaws.com/gh/secondlife/cef/cef_bin-151.3.24_g2384915_chromium-151.0.7922.174-windows64-262470121.tar.zst

cmake -B build -G "Visual Studio 17 2022" -A x64  -DCEF_RUNTIME_LIBRARY_FLAG=/MD -DUSE_SANDBOX=OFF -DCEF_PACKAGE_URL=%CEF_MINIMAL_URL%
cmake --build build --config Release

:end
