# PCL-CE 地区信号 Hook

在 **PCL-CE v2.15.1** 进程内伪造 Windows 地区信号，使 `RestrictedFeaturePolicy.Assess()`
（内部调用 `PCL.Core.App.Region.RegionPolicy.Evaluate`）判定为 `Other`，从而解除
"Minecraft 下载、补全与整合包安装功能在你所在的国家或地区不可用"的限制。

方案由两部分组成：

- `PCLCEHook.dll`：基于 [MinHook](https://github.com/TsudaKageyu/minhook) 的进程内 API Hook。
- `launcher.exe`：代理启动器，负责拉起目标进程、在最早期注入 DLL、再恢复运行。

## 伪造的目标值

目标地区为**新加坡**，伪造值刻意保持用户区域与代码页不变：

| RegionPolicy 读取的信号 | 系统调用 | 伪造结果 |
| --- | --- | --- |
| `GeoName` | `GetUserDefaultGeoName` | `SG` |
| `DeviceRegionGeoId` | `RegQueryValueExW("DeviceRegion")` | `215`（0xD7，新加坡 GeoID） |
| `TimeZoneId` | `GetDynamicTimeZoneInformation` / `GetTimeZoneInformation` | `Singapore Standard Time` |
| `UserLocaleName` | `GetUserDefaultLocaleName` | **不拦截**，保持 `zh-CN` |
| `PreferredUiLanguages` | `GetUserPreferredUILanguages` | **不拦截**，保持原值 |

新加坡与中国标准时间同为 UTC+8 且都不使用夏令时，因此时间换算结果不变，只替换标识与时区键名。
`zh-CN` 与 UTF-8/系统代码页保持原样，不引入语言或编码副作用。

上游判定逻辑（v2.15.1）：
[`RegionPolicy.cs`](https://github.com/PCL-Community/PCL-CE/blob/v2.15.1/PCL.Core/App/Region/RegionPolicy.cs)、
[`RestrictedFeaturePolicy.cs`](https://github.com/PCL-Community/PCL-CE/blob/v2.15.1/Plain%20Craft%20Launcher%202/Modules/Minecraft/RestrictedFeaturePolicy.cs)。

## 为什么必须"挂起后注入"

`TimeZoneInfo.Local` 是 .NET 的静态惰性缓存：第一次访问时通过
`GetDynamicTimeZoneInformation` 取得时区键名，随后一直持有该实例。PCL 启动过程中一旦
读取过它，再注入就无法改变 `TimeZoneInfo.Local.Id`。

因此启动器一律使用 `CreateProcessW(..., CREATE_SUSPENDED)`：

1. 进程创建后主线程挂起，CLR 尚未开始执行托管代码；
2. 远程线程完成 `LoadLibraryW` + `PclCeHookInstall`；
3. `ResumeThread` 放行，此时 4 个 hook 已经生效。

`--debug-mode` 保留给需要调试器附加的场景：注入仍在挂起阶段完成，随后 `DebugActiveProcess`
附加并转发调试事件。**不使用 `DEBUG_ONLY_THIS_PROCESS` 启动**，原因是调试事件期间系统会挂起
被调试进程的全部线程，远程注入线程无法运行，注入会超时失败。

## 构建

要求：Windows 10/11 x64、CMake ≥ 3.20、Ninja、MSVC（推荐）或 MSYS2 UCRT64 GCC。

MinHook（v1.3.4，BSD-2-Clause）的源码不纳入版本库，首次构建前先取回：

```powershell
git clone --depth 1 --branch v1.3.4 https://github.com/TsudaKageyu/minhook.git third_party/minhook
```

仓库只保留其 `LICENSE.txt` 以满足许可证要求；缺少源码时 CMake 会在配置阶段直接报错。
上游 `CMakeLists.txt` 声明 `cmake_minimum_required(3.0...3.5)`，CMake 4.x 会拒绝该策略范围，
所以本工程只编译其源文件，不使用 `add_subdirectory`。

MSVC（在 VS 开发者环境内执行）：

```powershell
cmake --preset msvc-x64
cmake --build --preset msvc-x64
```

产物：

- `build/msvc-x64/bin/launcher.exe`
- `build/msvc-x64/bin/PCLCEHook.dll`

GCC 备选：`cmake --preset gcc-x64`。

## 使用

把 `launcher.exe` 与 `PCLCEHook.dll` 放在一起（推荐直接放进 PCL-CE 所在目录）：

```powershell
# 自动查找同级目录中的 PCL*CE*.exe（不递归子目录）
.\launcher.exe

# 指定目标，并透传参数
.\launcher.exe --target "D:\PCL-CE\PCL2_CE_Release_x64.exe"

# 等待目标退出并转发退出码（便于命令行验证）
.\launcher.exe --target ".\PclCeRegionProbe.exe" --wait -- --debug

# 只解析目标与 DLL，不启动
.\launcher.exe --dry-run
```

| 选项 | 说明 |
| --- | --- |
| `--target <exe>` | 指定目标可执行文件；省略时只在启动器所在目录查找 `PCL*CE*.exe` |
| `--dll <dll>` | 指定 hook DLL，默认同目录 `PCLCEHook.dll` |
| `--debug-mode` | 注入后附加调试器并转发事件 |
| `--wait` | 等待目标退出并转发退出码 |
| `--dry-run` | 仅解析并打印目标路径 |
| `--` | 其后的参数原样传给目标 |

诊断日志：`%TEMP%\PCLCEHook.log`（启动器每次启动前清空）。

## 验证结果

同一台机器（Windows 11 26100，.NET 10）上的实测：

Hook 前：

```
GeoName = "CN", DeviceRegionGeoId = 45, TimeZoneId = "China Standard Time"
Classification = Mainland, Reason = geo-cn        -> Ban
```

Hook 后（`launcher --target ...\PclCeRegionProbe.exe --wait -- --debug`）：

```
GeoName = "SG", DeviceRegionGeoId = 215, TimeZoneId = "Singapore Standard Time"
UserLocaleName = "zh-CN", PreferredUiLanguages = ["zh-CN", "en-US"]
Classification = Other, Reason = geo-other        -> NotBanned (exit 0)
```

PCL-CE 端到端：`launcher.exe` 启动 `PCL2_CE_Release_x64.exe` 后，进程模块列表中存在
`PCLCEHook.dll`，日志记录 `4/4 hooks enabled`。打开"下载"页触发 Forge 推荐版本刷新后，
PCL 日志中不再出现 Hook 前必有的
`[RegionPolicy] 已阻止功能 minecraft-install，判定原因：geo-cn` 与
"Minecraft 下载、补全与整合包安装功能在你所在的国家或地区不可用"提示。

## 已知限制

- Hook 仅在**被注入的进程内**生效，不修改注册表、系统区域设置或任何全局状态。
- 只覆盖 `RegionPolicy.ReadWindowsSignals()` 使用的信号；不改变网络位置、账号地区或下载源选择。
- 需要同位数目标（x64）。32 位目标无法加载 x64 DLL。
- 若目标以管理员权限运行而启动器不是，注入会因权限不足失败；反之亦然。
- 若目标自身启用了反注入保护，远程线程注入会被拒绝，启动器会终止目标并报错，不会留下"看似成功"的假象。
- PCL-CE 的 UI 文案、下载源、正版登录等其它行为不在本工程范围内。

## 免责声明

本项目仅用于**个人学习、安全研究与本地兼容性测试**，目的是验证 Windows 地区信号 API 的可观测性
与进程内 Hook 技术，不构成对任何服务的规避方案，也不提供任何形式的担保。

- **仅供研究**：不得用于商业用途、批量分发或规避付费、授权、账号风控与地区合规要求。
  PCL-CE 的地区限制由其上游项目自行决定，本项目不主张该限制无效或应当被绕过。
- **无关联、无背书**：本项目与 PCL-CE（PCL-Community）、Plain Craft Launcher、Mojang Studios、
  Microsoft 均无隶属或合作关系，未获其授权、认可或支持。相关名称与商标归各自所有者。
- **仅改本机进程内存**：Hook 只影响被注入进程自身的 API 返回值，不修改系统区域设置、注册表、
  时区、网络出口或服务端数据，也不会伪造账号归属地或登录状态。
- **风险自担**：使用本项目的全部风险由使用者承担。可能触发的后果包括但不限于违反 PCL-CE 或
  相关服务的使用条款、下载源可用性变化、目标程序更新后 Hook 失效、进程崩溃或数据损坏。
  作者与贡献者不对任何直接或间接损失负责。
- **合法性自负**：部分国家或地区对绕过软件地区限制有明确法律约束。请在**使用前自行确认**
  所在地法律法规与相关服务条款；如不确定，请勿使用。
- **不提供破解**：本项目不包含、不修改任何授权校验、付费内容或闭源组件，也不分发 PCL-CE 本体。
- **许可证**：本项目以 Apache-2.0 发布（见 `LICENSE`）；MinHook 采用 BSD-2-Clause，其许可证见
  `third_party/minhook/LICENSE.txt`。分发二进制或衍生作品时必须保留上述版权与许可证声明。

如果你不接受以上任何一条，请立即停止使用并删除本项目及其产物。
