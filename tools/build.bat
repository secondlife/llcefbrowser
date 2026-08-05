@if not exist "tools/" (
    @echo Run this command from the project root directory
    @goto end
)

@rem Note: let's not make the build directory a variable with this line in play - just in case...
if exist build rmdir build /s /q

@rem Note: CEF packages canbe found at the Spotify Automated Builds site here: https://cef-builds.spotifycdn.com/index.html
@rem A batch file restriction means you must replace the % signs in the original url with %%
set CEF_MINIMAL_URL=https://cef-builds.spotifycdn.com/cef_binary_150.0.9%%2Bg81b0088%%2Bchromium-150.0.7871.46_windows64_minimal.tar.bz2

cmake -B build -G "Visual Studio 17 2022" -A x64  -DCEF_RUNTIME_LIBRARY_FLAG=/MD -DUSE_SANDBOX=OFF -DCEF_PACKAGE_URL=%CEF_MINIMAL_URL%
cmake --build build --config Release

:end
