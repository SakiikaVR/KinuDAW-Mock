# KinuUI compatibility profile

KinuUI is a native C++ fork of RmlUi. The public C++ namespace, headers, library names, and CMake targets remain `Rml` / `RmlUi` for source and integration compatibility. KinuUI is the repository and product name.

## CSS animation additions

- Standard timing aliases use their CSS cubic-bezier definitions.
- Arbitrary `cubic-bezier(x1, y1, x2, y2)` timing functions.
- Millisecond (`ms`) durations and delays.
- Standard direction, fill-mode, and play-state values.
- All eight `animation-*` longhands with comma-separated list behavior.
- Simple `calc()` multiplication and division used by Animate.css speed, delay, and repeat helpers.
- Per-keyframe `animation-timing-function`.
- CSS `solid` border tokens are accepted as the supported solid-border spelling.

## Animate.css 4.1.1

Run the conversion utility against an official Animate.css 4.1.1 download:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Utilities\Prepare-AnimateCss.ps1 `
  -InputPath .\animate.css `
  -OutputPath .\animate.kinu.rcss
```

The converter removes browser vendor duplicates and browser-only print/reduced-motion rules while preserving the upstream copyright and license header. An audit of the generated stylesheet verifies all 97 named keyframes. Its runtime declarations are limited to `animation-*`, custom properties, opacity, transform, transform-origin, and visibility; all transform functions used by Animate.css 4.1.1 are accepted by KinuUI.

Animate.css itself is not redistributed by this repository.

## Rendering quality

Renderer defaults use 4x multisample anti-aliasing where the backend exposes multisampling: DirectX 11, DirectX 12, OpenGL 3, Vulkan, GLFW GL2, and SDL GL2. Applications can still override backend sample-count macros or attributes before initialization.

Text quality continues to depend on the selected font engine, font asset, DPI ratio, and backend. The sample uses FreeType and the Win32 DirectX 11 backend for visual verification.

## Runtime performance

- mimalloc 3.5.1 provides the complete global C++ `new` and `delete` implementation linked through KinuUI Core.
- Tracy 0.14.1 instrumentation is compiled into regular configurations and remains dormant until a profiler connects.
- Tracy's separate allocation override is disabled so allocation ownership remains unambiguous.
- Both libraries are pinned Git submodules and mandatory in a KinuUI source build.

## Demo

Configure with samples and a font engine enabled, build `rmlui_sample_kinu_css_animations`, and run it from the repository root. The demo includes standard animation shorthand, Animate.css-style longhands, `var()`, `calc()`, pause/resume, replay, responsive layouts, and 4x MSAA validation.

The composition is visually inspired by the MIT-licensed [yui540/css-animations](https://github.com/yui540/css-animations). No image or video assets are redistributed.

## Scope

KinuUI improves compatibility without embedding a browser. Browser JavaScript frameworks, Tailwind CSS, Bootstrap, Windows UI, browser accessibility media features, and unsupported CSS layout engines are not bundled or claimed as compatible.
