# SpineLoveEX

Spine 与 Live2D 桌面查看器。默认使用简体中文，可在设置中切换为 English。

## 功能

- 加载、播放 Spine 与 Cubism 3+ Live2D 模型。
- 动作队列、皮肤、部件、参数、表情和模型语音。
- 鼠标视线跟随与手动固定方向。
- 缩放、平移、背景、收藏和桌面宠物。
- PNG、JPG、图片序列和视频导出。

视频和 GIF 导出需要 FFmpeg。将 `ffmpeg.exe` 放入 `_internal/tools`，或加入系统 PATH。

## Windows 源码构建

需要 Visual Studio 2022 C++ 桌面开发工具、Windows SDK，以及 Qt 6.8.3 MSVC 2022 x64，包含 Qt Quick、Quick Controls 和 Shader Tools。

在 PowerShell 中执行：

```powershell
./scripts/build.ps1 -QtRoot 'C:/Qt/6.8.3/msvc2022_64'
./scripts/package.ps1 -QtRoot 'C:/Qt/6.8.3/msvc2022_64'
```

内部程序输出到 `build/main/qt/spinelove_qt.exe`，启动器输出到 `build/SpineLoveEX.exe`。可分发压缩包输出到 `dist/SpineLoveEX-windows-x64.zip`，用户运行根目录的 `SpineLoveEX.exe`。

源码采用 MIT 许可证。Spine、Live2D、字体及其他第三方组件遵循各自许可证，见 `LICENSES` 和对应组件的许可证文件。

[English](README_en.md)
