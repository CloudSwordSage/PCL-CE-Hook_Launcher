# PCL-CE Region Probe (v2.15.1)

这是供 Hook/Agent 调试使用的最小 Windows .NET 10 CLI。它**复用** PCL-CE v2.15.1 的 `RegionPolicy.cs`，执行与 `RestrictedFeaturePolicy.Assess()` 相同的地区判定，**不启动 PCL、不联网、不安装游戏**。

## 前置条件

- Windows 10/11
- .NET 10 SDK（编译）
- .NET 10 Runtime（运行；framework-dependent，可用 `dotnet --list-runtimes` 检查）
- 不含第三方 NuGet 依赖，不需要完整 PCL-CE 项目。

## 构建

```powershell
pwsh -NoProfile -File .\build.ps1
```

第一次构建会通过 `prepare.ps1` 从 GitHub 的 **v2.15.1** 标签下载原始的 `PCL.Core/App/Region/RegionPolicy.cs`，约 11 KB。之后离线编译使用已下载的文件。不下载可执行文件，也不从网络执行脚本。如果手上已有 PCL-CE 源码，可改为：

```powershell
pwsh -NoProfile -File .\prepare.ps1 -LocalPath 'D:\PCL-CE\PCL.Core\App\Region\RegionPolicy.cs' -Force
pwsh -NoProfile -File .\build.ps1
```

编译产物位于 `publish\win-x64\PclCeRegionProbe.exe`。如需 ARM64：`pwsh -NoProfile -File .\build.ps1 -Runtime win-arm64`。

## 标准输出协议

| stdout | 退出码 | 意义 |
| --- | --- | --- |
| `Ban` | 1 | 被地区策略限制（Mainland 或 Unknown） |
| `NotBanned` | 0 | 判定为 Other，允许安装 |
| `Ban` | 2 | 参数错误、非 Windows 或运行异常（不能用于证明地区限制命中） |

正常情况 stdout 严格只有 `Ban` 或 `NotBanned` 一行。诊断信息走 stderr。

## 运行示例

```powershell
.\publish\win-x64\PclCeRegionProbe.exe
$LASTEXITCODE

# 输出依然只有 Ban / NotBanned，详细地区信号走 stderr
.\publish\win-x64\PclCeRegionProbe.exe --debug

# 延迟 3 秒供注入器注入，延迟后读取系统信息
.\publish\win-x64\PclCeRegionProbe.exe --delay-ms 3000 --debug
```

## 重要限制

- **`NotBanned` 仅表示本进程地区判定通过，不等于证明 Hook 已成功安装。** 建议在已知会输出 `Ban` 的同机环境进行 Hook 前/后的对照测试。
- 只验证 **v2.15.1 地区策略**，不验证下载服务器、UI 或真实 PCL-CE 是否能下载。
- 要让 Hook 影响此程序，必须在**本程序的进程内**生效；Hook 另一个 PCL-CE 进程不会自动影响本程序。
- `--delay-ms` 便于进程创建后注入；若在 CLR 启动时已缓存某些值（例如时区），应在进程启动前注入或悬挂启动。
- 若微软 API / .NET 版本、执行位数、Hook 库实现不同，观测仍可能不同。切勿在系统全局替换返回值，建议只针对测试进程。
- 本项目不包含 PCL 原版 OAuth / 其他模块。源码来自 PCL-CE 仓库，原始源代码版权与许可证归原项目所有。

上游源码：<https://github.com/PCL-Community/PCL-CE/blob/v2.15.1/PCL.Core/App/Region/RegionPolicy.cs>
