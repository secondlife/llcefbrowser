@if not exist "tools/" (
    @echo Run this command from the project root directory
    @goto end
)

if exist build rmdir build /s /q
mkdir build
pushd build

cmake^
 -G "Visual Studio 17 2022" -A x64 ..^
 -DCEF_RUNTIME_LIBRARY_FLAG=/MD^
 -DUSE_SANDBOX=Off^
 -DCEF_PACKAGE_URL=https://cef-builds.spotifycdn.com/cef_binary_150.0.9%%2Bg81b0088%%2Bchromium-150.0.7871.46_windows64_minimal.tar.bz2

cmake --build . --config Release

:end
popd
