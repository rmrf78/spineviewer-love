# SpineLoveEX

A desktop viewer for Spine and Cubism 3+ Live2D models. The interface defaults to Simplified Chinese and also supports English.

Features include animation queues, skins, parts, parameters, expressions, model audio, mouse-follow and fixed-direction controls, backgrounds, favorites, desktop pets, and image/video export.

Video and GIF export require FFmpeg. Place `ffmpeg.exe` in `_internal/tools` or add it to PATH.

Build on Windows with Visual Studio 2022 C++ tools, Windows SDK and Qt 6.8.3 MSVC 2022 x64 with Quick, Quick Controls and Shader Tools:

```powershell
./scripts/build.ps1 -QtRoot 'C:/Qt/6.8.3/msvc2022_64'
./scripts/package.ps1 -QtRoot 'C:/Qt/6.8.3/msvc2022_64'
```

The internal application is written to `build/main/qt/spinelove_qt.exe`, and the launcher to `build/SpineLoveEX.exe`. The portable archive is written to `dist/SpineLoveEX-windows-x64.zip`; users launch `SpineLoveEX.exe` from its root.

Application code is MIT licensed. Third-party components retain their respective licenses; see `LICENSES` and the bundled component license files.
