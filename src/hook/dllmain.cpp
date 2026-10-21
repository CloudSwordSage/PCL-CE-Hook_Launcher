// PCL-CE 地区信号伪造 DLL。
//
// 安装入口故意放在导出函数中而不是 DllMain：MinHook 创建/启用 hook 需要分配可执行内存并
// 短暂挂起其它线程，在 loader lock 内执行会带来死锁风险。注入器通过远程线程直接调用
// PclCeHookInstall，保证调用发生在 LoadLibraryW 返回、loader lock 已释放之后。

#include <windows.h>

#include "common/pclcehook_api.hpp"
#include "pclcehook/hooks.hpp"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) int __stdcall PclCeHookInstall(void) {
    return pclcehook::hooks::Install();
}

extern "C" __declspec(dllexport) unsigned __stdcall PclCeHookStatus(void) {
    const int count = pclcehook::hooks::InstalledCount();
    return count > 0 ? static_cast<unsigned>(count) : 0u;
}
