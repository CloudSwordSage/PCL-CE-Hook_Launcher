#pragma once

#include <windows.h>

#include <string>

namespace pclcehook {

/** 注入结果。message 用于向用户解释失败原因。 */
struct InjectResult {
    bool ok = false;
    int hookCount = 0;
    std::wstring message;
};

/**
 * 在目标进程中加载 hook DLL 并调用其安装入口。
 *
 * 流程：VirtualAllocEx + WriteProcessMemory 写入 DLL 路径 → 远程 LoadLibraryW →
 * 用本地 DLL 的导出 RVA 在远程模块基址上定位 PclCeHookInstall → 远程调用并读取返回值。
 *
 * @param process 目标进程句柄，需要 PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
 *                PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION 权限。
 * @param dllPath hook DLL 的绝对路径。
 * @return 注入结果；失败时 message 包含具体阶段。
 * @note 调用方应在目标主线程挂起或调试器暂停时调用，以确保 hook 早于托管代码执行。
 */
InjectResult InjectHookDll(HANDLE process, const std::wstring& dllPath);

}  // namespace pclcehook
