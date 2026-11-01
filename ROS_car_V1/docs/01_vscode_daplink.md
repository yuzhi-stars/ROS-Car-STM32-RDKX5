# Windows 下用 VS Code 和 DAPLink 编译、烧录

本教程对应当前 STM32F405RGT6 ROS 小车控制器工程。目标是从拿到源码开始，完成环境检查、生成工程、编译、通过 SWD 烧录，并区分“只烧录”和“断点调试”。全部命令在 **Windows PowerShell** 中执行。

## 1. 先理解工具之间的关系

| 工具 | 在本工程中做什么 |
|---|---|
| VS Code | 编辑器、任务入口和调试界面 |
| C/C++ 扩展 | 代码跳转、补全、编译错误定位 |
| CMake | 读取源文件清单、工具链和 Debug/Release 配置，生成构建规则 |
| `mingw32-make.exe` | 按构建规则调度编译；它本身不决定芯片型号 |
| `arm-none-eabi-gcc.exe` | 将 C/汇编编译成 Cortex-M4 固件 |
| `arm-none-eabi-gdb.exe` | F5 调试时控制目标、读变量和断点 |
| OpenOCD | 在电脑端通过 CMSIS-DAP 协议访问 DAPLink |
| DAPLink | 通过 SWDIO/SWCLK 读写 STM32 |

本工程采用 `MinGW Makefiles` 生成器，使用 `mingw32-make`，不是 Ninja，也不是 MSYS Makefiles。在 PowerShell 中运行本项目脚本即可；CMake 对该生成器的 shell 要求见 [官方说明](https://cmake.org/cmake/help/latest/generator/MinGW%20Makefiles.html)。

## 2. 准备硬件

需要 Windows 电脑、支持 CMSIS-DAP/SWD 的 DAPLink、数据 USB 线，以及正常供电的 STM32F405RGT6 控制板。先完成最小系统供电和 SWD，再接电机。

| DAPLink 信号 | STM32F405 侧 |
|---|---|
| SWDIO | PA13 |
| SWCLK | PA14 |
| GND | 控制板 GND |
| nRESET / nRST（建议接） | NRST |
| VTref / Vref（探针若有此脚） | 目标板 3.3 V 电压参考 |

**按探针和控制板的信号名称接线，不按连接器针序猜测。** VTref 通常是电压参考；探针上标为 3V3/VCC 的脚是否能供电，需要看该探针说明。目标板必须有稳定供电并共地，不向 MCU 的 3.3 V 信号脚送 5 V。电机供电按实际电路设计处理，不能由调试探针代替。

DAPLink 在这里用于 SWD 编程，不是给 RDK 的串口。即使探针有虚拟串口或拖放盘符，本工程的烧录任务也使用 OpenOCD＋CMSIS-DAP。CMSIS-DAP 的作用见 [Arm 官方介绍](https://arm-software.github.io/CMSIS-DAP/latest/index.html)。

## 3. 安装电脑端软件

### 3.1 安装 VS Code 和扩展

安装 [VS Code](https://code.visualstudio.com/Download)，在扩展页面安装：

- `ms-vscode.cpptools`：C/C++。
- `ms-vscode.cmake-tools`：CMake Tools。
- `marus25.cortex-debug`：Cortex-Debug，只有需要 F5 调试时才用到它。

本工程 `.vscode/extensions.json` 已提供推荐列表。用“文件 → 打开文件夹”打开 `ROS_car_V1` 根目录，资源管理器中应能直接看到 `Core`、`CMakeLists.txt` 和 `tools`。不要只打开 `Core` 或 `build`。

如果同时装有其他 STM32 工具扩展，可以保留，但本教程使用项目自带任务；不要让旧的 `cube-cmake`、Ninja 生成器或另外一套 clangd 配置覆盖这里的设置。

### 3.2 安装 CMake

从 [CMake 官方下载页](https://cmake.org/download/) 安装 Windows x64 版本。工程要求 **CMake ≥3.22**。可以加入 PATH，也可以直接在后面的配置文件中填写 `cmake.exe` 路径。

### 3.3 安装 Arm GNU 工具链

使用包含下列工具的 **Arm bare-metal** 工具链：

```text
arm-none-eabi-gcc.exe
arm-none-eabi-g++.exe
arm-none-eabi-gdb.exe
arm-none-eabi-objcopy.exe
arm-none-eabi-size.exe
```

可以使用 STM32Cube 已安装的 GNU Tools for STM32，或从 [Arm GNU 工具链入口](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) 查找 Windows 主机、`arm-none-eabi` 目标的版本。不要选择面向 Linux 应用的 `aarch64-linux-gnu` 或 `arm-linux-gnueabihf` 工具链。

本机验证使用 GNU Tools for STM32 **14.3.1**，其 `bin` 目录可直接用于本项目。其他版本需重新编译验证，不必照抄作者电脑的用户名路径。

### 3.4 安装 MinGW-w64

从 [MinGW-w64 的发行版列表](https://www.mingw-w64.org/downloads/) 选择 Windows 发行包，确认 `bin` 目录内有 `mingw32-make.exe`。如要运行电脑端模拟测试，还需要同目录中的 `gcc.exe` 和 `g++.exe`。

注意两套编译器的用途：`arm-none-eabi-gcc` 编译 STM32 固件；MinGW 的 `gcc/g++` 只用于 Windows 上的测试程序。本机 make 版本为 GNU Make **4.2.1**。

### 3.5 安装 OpenOCD

可按 [xPack OpenOCD 安装说明](https://xpack-dev-tools.github.io/openocd-xpack/docs/install/) 下载并解压 Windows 包。本机使用 xPack `0.12.0-7` 安装包，实际版本输出含 `0.12.0+dev`。

解压后至少能找到：

```text
bin/openocd.exe
openocd/scripts/interface/cmsis-dap.cfg
openocd/scripts/target/stm32f4x.cfg
```

有些发行包把脚本放在 `share/openocd/scripts`，以真实目录为准。不要只拷贝一个 `openocd.exe`，其附属 DLL 和脚本目录也需要保留。

CMSIS-DAP v1 常用 USB HID，v2 常用 USB bulk；OpenOCD 支持相应后端自动选择，见 [OpenOCD 的 CMSIS-DAP 配置](https://openocd.org/doc/html/Debug-Adapter-Configuration.html)。如果设备不能枚举，按探针厂商说明处理对应调试接口的驱动，不把串口、U 盘盘符当成 SWD 调试接口。

## 4. 只在一处填写本机路径

源码包不包含作者电脑的路径配置。第一次在根目录执行：

```powershell
Copy-Item -LiteralPath tools/local.settings.example.json -Destination tools/local.settings.json
```

如果当前文件夹已带有你自己的 `local.settings.json`，直接检查和编辑，不需要覆盖。将内容改成自己电脑上真实存在的路径，例如：

```json
{
    "armGccBin": "C:/Tools/ArmGNU/bin",
    "makeBin": "C:/Tools/mingw64/bin",
    "cmakeExe": "C:/Program Files/CMake/bin/cmake.exe",
    "openocdExe": "C:/Tools/xpack-openocd/bin/openocd.exe",
    "openocdScripts": "C:/Tools/xpack-openocd/openocd/scripts",
    "swdSpeedKhz": 100
}
```

| 字段 | 填什么 |
|---|---|
| `armGccBin` | 含 `arm-none-eabi-gcc.exe` 的目录，不是 gcc 文件本身 |
| `makeBin` | 含 `mingw32-make.exe` 的目录 |
| `cmakeExe` | `cmake.exe` 的完整文件路径 |
| `openocdExe` | `openocd.exe` 的完整文件路径 |
| `openocdScripts` | 能在其下面找到 `interface/` 和 `target/` 的脚本目录 |
| `swdSpeedKhz` | SWD 时钟，默认 100 kHz，延用原工程已使用的保守值 |

JSON 路径用 `/` 最方便；如果用 `\`，JSON 中应写成 `\\`。示例中的目录名只是示范，不会自动安装软件。字段留空时，脚本会尝试 PATH；Arm 工具目录也支持环境变量 `ARM_GCC_BIN`。

工具路径变化后重新执行下一节的 `Check`、`Setup`；若更换了编译器或构建生成器，使用一个新的空构建目录，避免继承旧缓存。

## 5. 检查环境、生成 VS Code 配置

打开 VS Code 集成终端，先确认当前路径是工程根目录，再执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/project.ps1 -Action Check
powershell -NoProfile -ExecutionPolicy Bypass -File tools/project.ps1 -Action Setup
```

`Check` 打印 CMake、Arm GCC、Make、OpenOCD、GDB 版本，检查 OpenOCD 脚本文件，**不会连接开发板**。全部工具找到且进程退出码为 0 才继续。

`Setup` 根据本机配置生成 `.vscode/launch.json` 和 `.vscode/c_cpp_properties.json`。前者给 Cortex-Debug 使用，后者给代码补全使用；两者包含本机绝对路径，不进入对外源码包。修改路径后重新生成即可。

命令中的 `-ExecutionPolicy Bypass` 只作用于这一次 PowerShell 进程，不需要全局修改电脑的脚本策略。

## 6. 编译固件

Debug：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/project.ps1 -Action Build -Configuration Debug
```

Release：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/project.ps1 -Action Build -Configuration Release
```

构建入口先运行 CMake 配置，再构建，第一次拿到源码也不需要预先创建 `build`。默认 4 个并行任务，可通过 `-Jobs 2` 调低。

| 配置 | 编译选项 | 固件和关联文件 |
|---|---|---|
| Debug | `-O0 -g3` | `build/Debug/ROS_car_V1.elf`、`.map`、`compile_commands.json` |
| Release | `-Os -g0` | `build/Release/ROS_car_V1.elf`、`.map`、`compile_commands.json` |

成功时应看到 `Built target ROS_car_V1`，新构建/链接时还会打印 RAM/FLASH 用量。`.elf` 自带烧录地址和符号，可以直接交给 OpenOCD；本项目默认不生成 `.bin/.hex`，不需要先转换才能烧录。修改源码后必须确认构建成功，不只看目录里是否有旧 ELF。

VS Code 中按 `Ctrl+Shift+B` 执行默认 `Build Debug`；或 `Ctrl+Shift+P` → `Tasks: Run Task`，选择 `Build Debug`/`Build Release`。任务实际调用同一个 PowerShell 入口，具体任务机制见 [VS Code 文档](https://code.visualstudio.com/docs/debugtest/tasks)。

只想生成构建规则而暂不编译，可运行 `-Action Configure -Configuration Debug`。对底层命令感兴趣时，用 `cmake --build --preset Debug --verbose` 查看已经配置好的工程；首次配置仍推荐项目脚本。

## 7. 只烧录，不进入调试

接好 SWD、共地和目标板供电，结束其他占用探针的调试会话，执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/project.ps1 -Action Flash -Configuration Debug
```

该动作依次完成：配置 → 编译 → OpenOCD 编程 → 校验 → 复位运行 → 退出 OpenOCD。任一步失败都停止。烧录时的小车应处于不接收运动指令的状态，因为复位后上位机模式能直接接受新的非零速度帧。

VS Code 任务入口：`Tasks: Run Task` → **Flash DAPLink OpenOCD**。烧录 Release 则使用相同命令并指定 `-Configuration Release`；F8 任务固定烧录 Debug，避免配置和目标文件不一致。

想先查看命令、不编译也不碰硬件：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/project.ps1 -Action Flash -Configuration Debug -DryRun
```

`-DryRun` 不会检查探针是否存在，也不证明 SWD 接线正确。真实烧录应检查 OpenOCD 是否完成编程与校验、无错误退出，以及板上程序是否运行。

## 8. 配置 F8 一键烧录

**F8 不是这个扩展自动提供的固定烧录键，需要手动绑定一次。** 工程任务可以随源码分发，用户快捷键属于 VS Code 用户/配置文件设置，不能只放一个 `.vscode/keybindings.json` 就指望自动生效。

按 `Ctrl+Shift+P`，打开 `Preferences: Open Keyboard Shortcuts (JSON)`，将下列对象追加到已有数组中，保留原来其他快捷键：

```json
{
    "key": "f8",
    "command": "workbench.action.tasks.runTask",
    "args": "Flash DAPLink OpenOCD",
    "when": "!inDebugMode"
}
```

也可以参考 [.vscode/keybindings.example.json](../.vscode/keybindings.example.json)。保存后按 F8，终端应出现本工程的配置、编译和 OpenOCD 输出。用户快捷键的作用与位置见 [VS Code 官方说明](https://code.visualstudio.com/docs/configure/keybindings)。

如果 F8 仍跳到下一处错误，检查当前 VS Code 配置文件是否是你修改的那个、是否有 F8 冲突，以及任务名称是否完全相同。作者原来已配置同名任务，整理后保留任务名称，因此原 F8 绑定可以继续使用。

## 9. F5 断点调试和停止调试

先执行 `Setup` 和 `Build Debug`，在运行和调试页面选 `DAPLink OpenOCD: Debug ROS_car_V1`，按 F5。当前配置会下载 Debug ELF，并停在 `main()`；这是预期行为。

停在 `main()` 时，外设和应用初始化可能尚未执行，OLED/串口当然可能没有输出。按 F5 继续执行。调试变量、单步或断点时，主循环和软件超时逻辑都会受到暂停影响，电机试验时不要把断点暂停当作可靠停车方式。

结束调试时，配置会执行 `Reset Run DAPLink OpenOCD`，让目标复位运行。如果探针释放失败，可结束其他 OpenOCD/GDB 会话后单独运行该任务。此任务只复位，不重新烧录。

调试路径由 `Setup` 自动生成，无需手填作者用户名；涉及的 `serverpath`、`gdbPath`、`searchDir` 等字段见 [Cortex-Debug 配置参考](https://github.com/Marus/cortex-debug/blob/master/debug_attributes.md)。

## 10. 常见环境和烧录错误

| 现象 | 核查方法 |
|---|---|
| 找不到 `arm-none-eabi-gcc.exe` | `armGccBin` 是否指向真正的 bin；不要填 MinGW gcc 的目录 |
| 找不到 `mingw32-make.exe` | 检查 `makeBin`；该工程不是 Ninja 生成器 |
| 找不到 `cube-cmake` | 仍有旧扩展/设置覆盖；使用本工程任务，检查工作区和用户设置 |
| CMake 提示 generator 不一致 | 旧 `build/Debug` 由其他生成器产生；关闭占用进程，将该构建目录改名备份，再重新构建 |
| CMake 提示源码目录与缓存不一致 | 不要复制别人的 build 缓存；源码包本来就不包含它，重新配置自己的构建目录 |
| CMake Tools 在点击状态栏配置时找不到工具 | 当前本机路径只由项目脚本加载；先用自带任务，若想直接使用 CMake Tools，再把工具 bin 加入 VS Code 启动环境的 PATH |
| `Can't find interface/cmsis-dap.cfg` | `openocdScripts` 填错或只拷了 exe；该路径下面应有 interface 和 target 两个子目录 |
| `unable to find a matching CMSIS-DAP device` | USB 数据线、设备枚举、正确的 CMSIS-DAP 调试接口及其驱动、探针是否被占用 |
| `target not examined` / SWD 连接失败 | 目标供电、GND、PA13/PA14 是否接反、线长、NRST、芯片选择和速度 |
| GDB 已启动但没有继续运行 | F5 默认停在 main，继续执行；只烧录使用 F8 或 Flash 任务 |
| 烧录成功但界面全黑 | OLED 默认上电熄屏，等待约 3 秒初始化，按旋钮开屏；再看 [联调教程](04_bringup.md) |
| PowerShell 禁止执行脚本 | 使用本教程完整的 `powershell -NoProfile -ExecutionPolicy Bypass -File ...` 命令 |

遇到旧构建目录问题时，只处理 `build` 下对应生成目录，保留源码、工具链文件和归档。复制项目给别人应使用 [源码导出流程](05_maintenance.md)，不要直接把整份电脑缓存压缩过去。

## 11. 完成后的验收

确认 Check 能找到五类工具，Debug/Release 均编译成功，F8 执行的是 Flash 任务且 OpenOCD 编程校验成功，复位后无需按 F5 继续。然后按 [首次联调](04_bringup.md) 检查轮速/IMU 反馈和控制权。

本教程的工具版本输出、配置生成、固件构建和烧录参数 DryRun 已在本机验证；本次整理没有连接 DAPLink 执行实际烧录，不能用 DryRun 代替硬件验收。
