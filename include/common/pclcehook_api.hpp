#pragma once

#include <windows.h>

namespace pclcehook::api {

/** 注入器默认加载的 hook DLL 文件名，与启动器同目录。 */
inline constexpr wchar_t kDllFileName[] = L"PCLCEHook.dll";

/** DLL 导出的安装入口，签名见 PclCeHookInstallFn。 */
inline constexpr char kInstallExport[] = "PclCeHookInstall";

/** DLL 导出的查询入口，签名见 PclCeHookStatusFn。 */
inline constexpr char kStatusExport[] = "PclCeHookStatus";

/**
 * 安装全部 hook 并启用。
 *
 * @return 成功启用的 hook 数量；小于 0 表示失败（-1 MH_Initialize 失败，-2 无任何 hook 安装成功）。
 * @note 该函数可重复调用，重复调用返回首次结果。调用时不能持有目标进程的 loader lock
 *       （注入器通过远程线程调用，而非在 DllMain 内）。
 */
using PclCeHookInstallFn = int(__stdcall*)(void);

/** 返回已启用的 hook 数量，0 表示未安装。 */
using PclCeHookStatusFn = unsigned(__stdcall*)(void);

}  // namespace pclcehook::api
