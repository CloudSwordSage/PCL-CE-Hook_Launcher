#include "pclcehook/hooks.hpp"

#include <windows.h>

#include <MinHook.h>

#include <atomic>
#include <cstring>
#include <cwchar>
#include <mutex>

#include "pclcehook/log.hpp"

namespace {

/** 目标地区：新加坡。RegionPolicy 只接受 ISO 3166-1 alpha-2 或 ISO 数字码。 */
constexpr wchar_t kGeoName[] = L"SG";
/** 含结尾 NUL 的字符数，GetUserDefaultGeoName 的返回值语义。 */
constexpr int kGeoNameLength = 3;

/** 新加坡 GeoID，与 RegionPolicy 中的 MainlandChinaGeoId(45) 区分。 */
constexpr DWORD kSingaporeDeviceRegion = 215;  // 0xD7

/** 新加坡时区注册表键名，TimeZoneInfo.Local 以该键名回查 HKLM。 */
constexpr wchar_t kSingaporeTimeZone[] = L"Singapore Standard Time";

using GetUserDefaultGeoNameFn = int(WINAPI*)(LPWSTR, int);
using GetDynamicTimeZoneInformationFn = DWORD(WINAPI*)(DYNAMIC_TIME_ZONE_INFORMATION*);
using GetTimeZoneInformationFn = DWORD(WINAPI*)(LPTIME_ZONE_INFORMATION);
using RegQueryValueExWFn = LSTATUS(WINAPI*)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);

GetUserDefaultGeoNameFn g_originalGetUserDefaultGeoName = nullptr;
GetDynamicTimeZoneInformationFn g_originalGetDynamicTimeZoneInformation = nullptr;
GetTimeZoneInformationFn g_originalGetTimeZoneInformation = nullptr;
RegQueryValueExWFn g_originalRegQueryValueExW = nullptr;

std::atomic<int> g_installedCount{0};
std::once_flag g_installOnce;

/**
 * 将源字符串复制进 Win32 定长 WCHAR 数组并补 NUL。
 *
 * @tparam N 目标数组元素个数，调用方保证与结构体字段一致。
 */
template <size_t N>
void CopyFixedName(wchar_t (&destination)[N], const wchar_t* source) {
    const size_t length = std::wcslen(source);
    const size_t count = length < N - 1 ? length : N - 1;
    std::wmemcpy(destination, source, count);
    destination[count] = L'\0';
}

/**
 * 用新加坡时区数据覆盖 Win32 时区结构。
 *
 * 新加坡与系统原始值（中国标准时间）同为 UTC+8 且都不使用夏令时，
 * 因此只替换标识性名称与偏移字段，避免影响时间换算本身。
 */
void ApplySingaporeTimeZone(DYNAMIC_TIME_ZONE_INFORMATION& info) {
    CopyFixedName(info.StandardName, kSingaporeTimeZone);
    CopyFixedName(info.DaylightName, kSingaporeTimeZone);
    CopyFixedName(info.TimeZoneKeyName, kSingaporeTimeZone);
    info.Bias = -480;
    info.StandardBias = 0;
    info.DaylightBias = 0;
    info.StandardDate = {};
    info.DaylightDate = {};
    info.DynamicDaylightTimeDisabled = FALSE;
}

void ApplySingaporeTimeZone(TIME_ZONE_INFORMATION& info) {
    CopyFixedName(info.StandardName, kSingaporeTimeZone);
    CopyFixedName(info.DaylightName, kSingaporeTimeZone);
    info.Bias = -480;
    info.StandardBias = 0;
    info.DaylightBias = 0;
    info.StandardDate = {};
    info.DaylightDate = {};
}

int WINAPI DetourGetUserDefaultGeoName(LPWSTR geoName, int geoNameCount) {
    if (geoName == nullptr || geoNameCount < kGeoNameLength) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    std::wmemcpy(geoName, kGeoName, kGeoNameLength);
    return kGeoNameLength;
}

DWORD WINAPI DetourGetDynamicTimeZoneInformation(DYNAMIC_TIME_ZONE_INFORMATION* info) {
    const DWORD result = g_originalGetDynamicTimeZoneInformation(info);
    if (info != nullptr) ApplySingaporeTimeZone(*info);
    return result;
}

DWORD WINAPI DetourGetTimeZoneInformation(LPTIME_ZONE_INFORMATION info) {
    const DWORD result = g_originalGetTimeZoneInformation(info);
    if (info != nullptr) ApplySingaporeTimeZone(*info);
    return result;
}

/**
 * 构造 REG_DWORD 读取结果，遵循 RegQueryValueExW 对 lpData/lpcbData 的两次调用协议：
 * lpData 为空时只回报所需字节数，缓冲区不足时返回 ERROR_MORE_DATA。
 */
LSTATUS ReturnDwordValue(DWORD value, LPDWORD type, LPBYTE data, LPDWORD size) {
    if (type != nullptr) *type = REG_DWORD;
    if (size == nullptr) return ERROR_INVALID_PARAMETER;

    if (data == nullptr) {
        *size = static_cast<DWORD>(sizeof(DWORD));
        return ERROR_SUCCESS;
    }
    if (*size < sizeof(DWORD)) {
        *size = static_cast<DWORD>(sizeof(DWORD));
        return ERROR_MORE_DATA;
    }
    std::memcpy(data, &value, sizeof(DWORD));
    *size = static_cast<DWORD>(sizeof(DWORD));
    return ERROR_SUCCESS;
}

LSTATUS WINAPI DetourRegQueryValueExW(HKEY key, LPCWSTR valueName, LPDWORD reserved, LPDWORD type,
                                      LPBYTE data, LPDWORD size) {
    // 只按值名拦截：PCL-CE 读取设备区域时固定的值名是 DeviceRegion。
    if (valueName != nullptr && lstrcmpiW(valueName, L"DeviceRegion") == 0) {
        return ReturnDwordValue(kSingaporeDeviceRegion, type, data, size);
    }
    return g_originalRegQueryValueExW(key, valueName, reserved, type, data, size);
}

const wchar_t* ModuleCandidates[] = {L"kernelbase.dll", L"kernel32.dll", L"advapi32.dll"};

/**
 * 在真实实现所在的模块中解析符号。
 *
 * kernel32/advapi32 的导出在 Windows 10 之后多为到 kernelbase 的转发，
 * 因此优先 kernelbase，避免在短转发桩上布 hook。
 */
HMODULE ResolveSymbolModule(const char* symbol, const wchar_t* preferredModule, FARPROC* address,
                            const wchar_t** moduleName) {
    auto tryModule = [&](const wchar_t* name) -> HMODULE {
        HMODULE module = GetModuleHandleW(name);
        if (module == nullptr) module = LoadLibraryW(name);
        if (module == nullptr) return nullptr;
        const FARPROC proc = GetProcAddress(module, symbol);
        if (proc == nullptr) return nullptr;
        *address = proc;
        *moduleName = name;
        return module;
    };

    if (preferredModule != nullptr) {
        if (HMODULE module = tryModule(preferredModule)) return module;
    }
    for (const wchar_t* name : ModuleCandidates) {
        if (preferredModule != nullptr && lstrcmpiW(name, preferredModule) == 0) continue;
        if (HMODULE module = tryModule(name)) return module;
    }
    return nullptr;
}

const char* StatusName(MH_STATUS status) {
    switch (status) {
        case MH_OK: return "MH_OK";
        case MH_ERROR_ALREADY_INITIALIZED: return "MH_ERROR_ALREADY_INITIALIZED";
        case MH_ERROR_NOT_INITIALIZED: return "MH_ERROR_NOT_INITIALIZED";
        case MH_ERROR_ALREADY_CREATED: return "MH_ERROR_ALREADY_CREATED";
        case MH_ERROR_NOT_CREATED: return "MH_ERROR_NOT_CREATED";
        case MH_ERROR_ENABLED: return "MH_ERROR_ENABLED";
        case MH_ERROR_DISABLED: return "MH_ERROR_DISABLED";
        case MH_ERROR_NOT_EXECUTABLE: return "MH_ERROR_NOT_EXECUTABLE";
        case MH_ERROR_UNSUPPORTED_FUNCTION: return "MH_ERROR_UNSUPPORTED_FUNCTION";
        case MH_ERROR_MEMORY_ALLOC: return "MH_ERROR_MEMORY_ALLOC";
        case MH_ERROR_MEMORY_PROTECT: return "MH_ERROR_MEMORY_PROTECT";
        case MH_ERROR_MODULE_NOT_FOUND: return "MH_ERROR_MODULE_NOT_FOUND";
        case MH_ERROR_FUNCTION_NOT_FOUND: return "MH_ERROR_FUNCTION_NOT_FOUND";
        default: return "MH_ERROR_UNKNOWN";
    }
}

/** 创建单个 hook，返回是否成功；失败原因写入诊断日志。 */
bool CreateHook(const char* symbol, const wchar_t* preferredModule, LPVOID detour, LPVOID* original,
                const wchar_t** moduleName) {
    FARPROC target = nullptr;
    const wchar_t* ownerName = L"";
    HMODULE owner = ResolveSymbolModule(symbol, preferredModule, &target, &ownerName);
    if (owner == nullptr || target == nullptr) {
        pclcehook::log::WriteFormat(L"hook %S: symbol not found", symbol);
        return false;
    }

    const MH_STATUS status = MH_CreateHook(reinterpret_cast<LPVOID>(target), detour, original);
    if (status != MH_OK) {
        pclcehook::log::WriteFormat(L"hook %S: MH_CreateHook failed: %S", symbol, StatusName(status));
        return false;
    }

    *moduleName = ownerName;
    return true;
}

}  // namespace

int pclcehook::hooks::Install() noexcept {
    std::call_once(g_installOnce, [] {
        wchar_t processPath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, processPath, MAX_PATH);
        log::WriteFormat(L"install start pid=%lu target=%ls", GetCurrentProcessId(), processPath);

        const MH_STATUS initStatus = MH_Initialize();
        if (initStatus != MH_OK) {
            log::WriteFormat(L"MH_Initialize failed: %S", StatusName(initStatus));
            g_installedCount.store(-1);
            return;
        }

        struct HookSpec {
            const char* symbol;
            const wchar_t* preferredModule;
            LPVOID detour;
            LPVOID* original;
            bool created = false;
        };

        // GetUserDefaultGeoName: .NET 的 P/Invoke 直接绑定 kernel32 的自有实现，
        // 该实现不经过 kernelbase，因此必须挂到 kernel32。
        // 其余符号的调用链由 kernelbase 承载（advapi32/kernel32 最终进入 kernelbase）。
        HookSpec specs[] = {
            {"GetUserDefaultGeoName", L"kernel32.dll",
             reinterpret_cast<LPVOID>(&DetourGetUserDefaultGeoName),
             reinterpret_cast<LPVOID*>(&g_originalGetUserDefaultGeoName)},
            {"RegQueryValueExW", L"kernelbase.dll",
             reinterpret_cast<LPVOID>(&DetourRegQueryValueExW),
             reinterpret_cast<LPVOID*>(&g_originalRegQueryValueExW)},
            {"GetDynamicTimeZoneInformation", L"kernelbase.dll",
             reinterpret_cast<LPVOID>(&DetourGetDynamicTimeZoneInformation),
             reinterpret_cast<LPVOID*>(&g_originalGetDynamicTimeZoneInformation)},
            {"GetTimeZoneInformation", L"kernelbase.dll",
             reinterpret_cast<LPVOID>(&DetourGetTimeZoneInformation),
             reinterpret_cast<LPVOID*>(&g_originalGetTimeZoneInformation)},
        };

        int created = 0;
        for (HookSpec& spec : specs) {
            const wchar_t* moduleName = L"";
            spec.created = CreateHook(spec.symbol, spec.preferredModule, spec.detour, spec.original,
                                      &moduleName) &&
                           *spec.original != nullptr;
            if (spec.created) {
                ++created;
                log::WriteFormat(L"hook %S: created in %ls", spec.symbol, moduleName);
            }
        }

        if (created == 0) {
            log::Write(L"no hook could be created");
            g_installedCount.store(-2);
            return;
        }

        const MH_STATUS enableStatus = MH_EnableHook(MH_ALL_HOOKS);
        if (enableStatus != MH_OK) {
            log::WriteFormat(L"MH_EnableHook failed: %S", StatusName(enableStatus));
            g_installedCount.store(-2);
            return;
        }

        g_installedCount.store(created);
        log::WriteFormat(L"install done: %d/%d hooks enabled, geo=SG deviceRegion=%lu tz=%ls",
                         created, static_cast<int>(sizeof(specs) / sizeof(specs[0])),
                         kSingaporeDeviceRegion, kSingaporeTimeZone);
    });

    return g_installedCount.load();
}

int pclcehook::hooks::InstalledCount() noexcept {
    return g_installedCount.load();
}
