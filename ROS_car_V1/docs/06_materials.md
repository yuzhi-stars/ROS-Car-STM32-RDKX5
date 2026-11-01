# 本次资料整理说明

整理日期：2026-11-01。目标是让复刻者能配置自己的电脑，不依赖作者已有缓存和用户名路径。

## 文件分工

| 位置 | 内容 | 对外源码包 |
|---|---|---|
| `Core`、`Drivers` | 业务、HAL/CMSIS 和必要修复 | 包含 |
| `cmake`、根构建/启动/链接文件 | GCC 构建入口 | 包含 |
| `.ioc` | CubeMX 配置，再生成要对比手工修改 | 包含 |
| `MDK-ARM` | Keil 工程与启动/RTE 源文件 | 仅源工程 |
| `.vscode` | 公共任务、扩展、F8 样例 | 不含个人生成配置 |
| `tools` | 构建/烧录、模板、打包 | 不含 local.settings.json |
| `docs`、`docs/history` | 当前教程与历史过程 | 包含，明确区分现状与历史 |
| `tests` | 模拟测试 | 包含，原上位机外部依赖需另提供 |
| `build` | 当前构建和测试生成物 | 不包含 |
| `archive` | 原配置、旧生成物和临时试验 | 不包含 |
| `dist` | 导出的 ZIP | 不递归包含自身 |
| `.settings`、`.mxproject` | 原 STM32 IDE 元数据，保留原处 | 不包含，教程不依赖它们 |

## 整理内容

四份过程文档移到 `docs/history/`。根目录 15 个 `.su`、两个生成器试验目录、Keil 输出/列表/个人界面文件移到 `archive/2026-11-01/`，没有删除业务源码。

原 `.vscode`、CMake 预设、工具链、clangd 和忽略配置保存在归档的 `original_config/`；移动前后路径记录于 `archive/2026-11-01/manifest.json`。

CMake 预设保留 Debug/Release 和 MinGW Makefiles，删除个人 make/windres 路径；Arm 工具链支持本机配置、环境变量和 PATH。VS Code 取消冲突的 cube-cmake/Ninja 专用覆盖，统一使用 Debug 编译数据库。

工具位置集中到 `tools/local.settings.json`；公共模板只含示例路径。保留 `Flash DAPLink OpenOCD` 任务名，原 F8 绑定继续可用；配置/编译失败会阻止烧录。F5 和补全路径由 Setup 生成，没有修改用户全局快捷键。

本次整理不改变轮序、PID、控制策略或串口布局。上一次无需主机零速握手的接管行为已写入当前教程，历史说明不覆盖它。

## 验证边界

验证入口见 [环境教程](01_vscode_daplink.md) 和 [维护说明](05_maintenance.md)：工具版本、Setup 输出、Debug/Release 构建、Flash/Reset DryRun、控制模拟测试、文档链接、源码包内容与解压后构建。

完整互通测试需要当前缺少的原上位机协议文件，不计为本次运行。没有接硬件烧录或实车测试，上板按 [首次联调](04_bringup.md) 验收。
