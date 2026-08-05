# llCefBrowser

A C++20 library that wraps [CEF](https://bitbucket.org/chromiumembedded/cef) (Chromium Embedded Framework) for **offscreen/windowless browser rendering** - embed one or many web pages as raw BGRA32 pixel buffers your own app uploads to a texture, without ever including a CEF header or touching `CefBrowser`/`CefClient`/`CefApp` directly.

Everything is addressed through an opaque `llCefBrowserHandle` (a slot index + generation counter), so a single `llCefBrowserManager` can own any number of concurrent browser instances, each independently created, resized, navigated, and destroyed.

## What it gives you

- **Offscreen rendering** - each browser paints into its own double-buffered pixel storage; `CopyLatestFrame()` hands you the latest BGRA32 frame (and only copies when something actually changed).
- **Input forwarding** - mouse move/click/wheel, keyboard (Win32 message passthrough), and focus, all addressed per-handle.
- **Per-browser callbacks** - navigation/load state, title and address changes, cursor shape, JS console messages, JS dialogs, file dialogs, HTTP auth, tooltips, and more (see `include/llCefBrowserManager.h`).
- **A JavaScript bridge** - page JS can call `window.cefQuery(...)` and get a response back from your native app.
- **Cookie management and DevTools** - inspect a running browser in its own native DevTools window; get/set/delete cookies against the shared cookie store.

## Building

Requires CMake 3.30+ and a CEF binary distribution (currently Windows only). Point the build at one via `CEF_PACKAGE_URL` (a downloadable tarball) or `CEF_PACKAGE_DIR` (a local extracted copy):

```
tools\build.bat
```

or manually:

```
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCEF_PACKAGE_URL=<cef binary distribution URL>
cmake --build build --config Release
```

## Examples

Two example applications live under `examples/`, both built alongside the library:

- **[single-browser-example](examples/single-browser)** - the simplest starting point: one CEF browser rendered offscreen into an OpenGL texture in a GLFW window.
- **[multiple-browsers-example](examples/multiple-browsers)** - several browsers at once, laid out as tiles in an orbitable 3D scene, with click-to-select and mouse/keyboard input routed to whichever one is selected.

Built executables land in `build/Release/`.

## Layout

- `include/` - the public API (`llCefBrowserManager`, `llCefBrowserLib`, handle/options/bridge types).
- `src/` - the implementation, wrapping CEF internally.
- `examples/` - the two example applications above, plus their vendored third-party dependencies (GLFW, glad, nlohmann/json).
- `tools/` - build scripts.
