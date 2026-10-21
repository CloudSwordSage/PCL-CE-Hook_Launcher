#include "pclcehook/inject.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cwchar>
#include <string>

#include "common/pclcehook_api.hpp"
#include "pclcehook/log.hpp"

namespace {

constexpr DWORD kRemoteThreadTimeoutMs = 30000;

using LoadLibraryWFn = HMODULE(WINAPI*)(LPCWSTR);

/** 在目标进程分配并写入以 NUL 结尾的宽字符串。 */
bool WriteRemoteString(HANDLE process, const std::wstring& text, LPVOID* address, std::wstring& error) {
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote == nullptr) {
        error = L"VirtualAllocEx 失败，错误码 " + std::to_wstring(GetLastError());
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, text.c_str(), bytes, &written) || written != bytes) {
        error = L"WriteProcessMemory 失败，错误码 " + std::to_wstring(GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    *address = remote;
    return true;
}

/** 创建远程线程并等待其结束，返回线程退出码。 */
bool RunRemoteThread(HANDLE process, LPVOID startAddress, LPVOID parameter, DWORD* exitCode,
                     std::wstring& error) {
    HANDLE thread = CreateRemoteThread(process, nullptr, 0,
                                       reinterpret_cast<LPTHREAD_START_ROUTINE>(startAddress),
                                       parameter, 0, nullptr);
    if (thread == nullptr) {
        error = L"CreateRemoteThread 失败，错误码 " + std::to_wstring(GetLastError());
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(thread, kRemoteThreadTimeoutMs);
    if (waitResult != WAIT_OBJECT_0) {
        error = L"远程线程超时未结束";
        CloseHandle(thread);
        return false;
    }

    const bool gotExitCode = GetExitCodeThread(thread, exitCode) != FALSE;
    CloseHandle(thread);
    if (!gotExitCode) {
        error = L"GetExitCodeThread 失败，错误码 " + std::to_wstring(GetLastError());
        return false;
    }
    return true;
}

/**
 * 用 Toolhelp 枚举目标进程模块，按完整路径定位刚加载的 DLL。
 *
 * x64 下 GetExitCodeThread 只能返回 32 位，模块基址会被截断，因此不能依赖远程
 * LoadLibraryW 的退出码作为基址。
 */
HMODULE FindRemoteModule(DWORD processId, const std::wstring& dllPath) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);
    if (snapshot == INVALID_HANDLE_VALUE) return nullptr;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    HMODULE found = nullptr;
    if (Module32FirstW(snapshot, &entry) != FALSE) {
        do {
            if (_wcsicmp(entry.szExePath, dllPath.c_str()) == 0) {
                found = reinterpret_cast<HMODULE>(entry.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &entry) != FALSE);
    }

    CloseHandle(snapshot);
    return found;
}

}  // namespace

pclcehook::InjectResult pclcehook::InjectHookDll(HANDLE process, const std::wstring& dllPath) {
    InjectResult result;

    HMODULE localModule = LoadLibraryExW(dllPath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (localModule == nullptr) {
        // 依赖解析被拒时退回完整加载，本地 DllMain 只调用 DisableThreadLibraryCalls。
        localModule = LoadLibraryW(dllPath.c_str());
    }
    if (localModule == nullptr) {
        result.message = L"无法在本地加载 hook DLL 以定位导出，错误码 " +
                         std::to_wstring(GetLastError());
        return result;
    }

    const FARPROC localInstall = GetProcAddress(localModule, api::kInstallExport);
    if (localInstall == nullptr) {
        result.message = L"hook DLL 缺少导出 PclCeHookInstall";
        FreeLibrary(localModule);
        return result;
    }
    const uintptr_t exportRva =
        reinterpret_cast<uintptr_t>(localInstall) - reinterpret_cast<uintptr_t>(localModule);
    FreeLibrary(localModule);

    LPVOID remoteDllPath = nullptr;
    std::wstring error;
    if (!WriteRemoteString(process, dllPath, &remoteDllPath, error)) {
        result.message = std::move(error);
        return result;
    }

    const LoadLibraryWFn loadLibrary = reinterpret_cast<LoadLibraryWFn>(&::LoadLibraryW);

    DWORD loadExitCode = 0;
    const bool loaded = RunRemoteThread(process, reinterpret_cast<LPVOID>(loadLibrary),
                                        remoteDllPath, &loadExitCode, error);
    VirtualFreeEx(process, remoteDllPath, 0, MEM_RELEASE);

    if (!loaded) {
        result.message = L"远程 LoadLibraryW 失败：" + error;
        return result;
    }
    if (loadExitCode == 0) {
        result.message = L"远程 LoadLibraryW 返回空模块句柄，DLL 可能在目标进程中初始化失败";
        return result;
    }

    const DWORD processId = GetProcessId(process);
    const HMODULE remoteModule = FindRemoteModule(processId, dllPath);
    if (remoteModule == nullptr) {
        result.message = L"已加载 DLL，但在目标进程模块列表中未找到它";
        return result;
    }

    const LPVOID remoteInstall =
        reinterpret_cast<LPVOID>(reinterpret_cast<uintptr_t>(remoteModule) + exportRva);

    DWORD hookCount = 0;
    if (!RunRemoteThread(process, remoteInstall, nullptr, &hookCount, error)) {
        result.message = L"远程调用 PclCeHookInstall 失败：" + error;
        return result;
    }

    const int signedCount = static_cast<int>(hookCount);
    result.hookCount = signedCount;
    result.ok = signedCount > 0;
    if (!result.ok) {
        result.message = signedCount == -1
                             ? L"PclCeHookInstall 返回 -1：MH_Initialize 失败"
                             : L"PclCeHookInstall 返回 -2：没有任何 hook 安装成功，详见日志";
        return result;
    }

    result.message = L"已启用 " + std::to_wstring(signedCount) + L" 个 hook";
    log::WriteFormat(L"inject: pid=%lu module=0x%llx hooks=%d", processId,
                     static_cast<unsigned long long>(
                         reinterpret_cast<uintptr_t>(remoteModule)),
                     signedCount);
    return result;
}
