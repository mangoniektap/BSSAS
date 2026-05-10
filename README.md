# BSSAS

肠鸣音信号分析系统医学辅助诊断工具。项目采用 Qt 6 + QML 构建桌面端界面，配合 C++ 后端完成数据采集、信号处理、结果分析、报告生成和更新管理。

## 项目简介

BSSAS 是一个面向肠鸣音信号分析场景的医学辅助诊断工具，核心目标包括：

- 采集与管理 DAQ 数据。
- 对肠鸣音信号进行预处理、降采样、DFT 分析和联合检测。
- 支持时频监测、辅助诊断和结果展示。
- 提供数据库管理、报告生成和软件更新能力。
- 通过 QML 实现现代化桌面 UI。

## 主要模块

- `App/`：程序入口、窗口管理、更新器入口。
- `BackendCore/`：信号处理、数据管理、数据库、设备管理、报告与更新等后端能力。
- `BSSAS/`：QML 主题与常量模块。
- `BSSASContent/`：主界面内容、页面和可复用组件。
- `PythonModules/`：项目使用的 Python 脚本和嵌入式运行时。
- `Generate/`：报告模板和生成所需的数据块文件。
- `3D_Resources/`：三维资源文件。

## 环境要求

当前项目主要面向 Windows 平台，建议环境如下：

- Windows 10 / 11。
- CMake 3.21.1 或更高版本。
- Ninja。
- Visual Studio 2022 MSVC 工具链。
- Qt 6.10.1 `msvc2022_64`。
- KFR Windows x86_64 依赖已放置在仓库内。
- `PythonModules/Runtime` 下需要存在嵌入式 Python 运行时。
- 如需生成安装包，还需要 Inno Setup。

## 构建前检查

请先确认以下资源存在，否则配置或打包可能失败：

- `PythonModules/Scripts/` 下的脚本文件。
- `PythonModules/Runtime/Python-*-embed-*/python.exe`。
- `Generate/DataChunk/` 下的数据块文件。
- `Generate/report/` 下的报告模板文件。
- `3D_Resources/3D model of abdomen.glb`。

## 构建方法

项目已配置 CMake Presets，可以直接使用 Debug / Release 构建。

### Debug

```bash
cmake --preset debug
cmake --build --preset debug
```

### Release

```bash
cmake --preset release
cmake --build --preset release
```

构建目录分别为 `build/debug` 和 `build/release`。

## 运行程序

主程序目标名为 `BSSASApp`，构建完成后可在对应构建目录中找到可执行文件。

```text
build/debug/BSSASApp.exe
build/release/BSSASApp.exe
```

此外仓库还包含更新器目标 `Updater`。

## 打包说明

打包相关配置位于 `Setup.iss` 和顶层 `CMakeLists.txt` 中。

- 构建时会自动生成 `Setup.iss`。
- `Usb_Daq4203.dll` 会在构建后自动复制到可执行文件目录。
- 嵌入式 Python 运行时默认随发布包一起使用，项目不会回退到系统 Python。

## 目录结构

```text
.
├── App/                 # 程序入口与启动相关代码
├── BackendCore/         # 核心算法、设备、数据库与更新逻辑
├── BSSAS/               # QML 常量和主题
├── BSSASContent/        # 主界面页面与组件
├── Generate/            # 报告模板与生成数据
├── PythonModules/       # Python 脚本与嵌入式运行时
├── 3D_Resources/        # 三维资源
├── cmake/               # CMake 辅助脚本
├── CMakeLists.txt       # 顶层构建入口
└── CMakePresets.json    # CMake 预设配置
```

## 开发说明

- UI 规范参考 `bowel_sound_ui_qml_spec.md`。
- 应用启动后会加载 QML 主界面，并在启动时检查更新。
- 更新服务地址和部分构建配置定义在顶层 `CMakeLists.txt` 中。

## 许可证

当前仓库未显式提供统一许可证文件，如需对外发布，请先确认项目许可策略。
