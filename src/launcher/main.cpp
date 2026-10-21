#include <windows.h>

#include <string>
#include <vector>

#include "common/pclcehook_api.hpp"
#include "pclcehook/inject.hpp"
#include "pclcehook/log.hpp"
#include "pclcehook/target.hpp"

namespace {

struct Options {
    std::wstring target;
    std::wstring dll;
    std::vector<std::wstring> targetArguments;
    bool debugMode = false;
    bool wait = false;
    bool dryRun = false;
    bool showHelp = false;
};

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), needed,
                        nullptr, nullptr);
    return result;
}

/** 输出一行文本；控制台走 WriteConsoleW，重定向时退化为 UTF-8。 */
void PrintLine(const std::wstring& text, bool toError = false) {
    HANDLE handle = GetStdHandle(toError ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return;

    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode) != FALSE) {
        DWORD written = 0;
        WriteConsoleW(handle, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
        WriteConsoleW(handle, L"\r\n", 2, &written, nullptr);
        return;
    }

    const std::string utf8 = ToUtf8(text + L"\r\n");
    DWORD written = 0;
    WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

void PrintUsage() {
    PrintLine(L"用法：launcher [选项] [-- <目标参数>...]");
    PrintLine(L"");
    PrintLine(L"选项：");
    PrintLine(L"  --target <exe>   指定要启动并注入的可执行文件");
    PrintLine(L"  --dll <dll>      指定 hook DLL，默认使用启动器同目录的 PCLCEHook.dll");
    PrintLine(L"  --debug-mode     注入后附加调试器并转发事件（注入仍在挂起阶段完成）");
    PrintLine(L"  --wait           等待目标进程退出并转发其退出码");
    PrintLine(L"  --dry-run        只解析并打印目标与 DLL，不启动进程");
    PrintLine(L"  --help           显示本帮助");
    PrintLine(L"");
    PrintLine(L"未提供 --target 时，只在启动器所在目录（不递归子目录）查找 PCL*CE*.exe。");
}

std::wstring ExecutableDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return L".";
        if (length < buffer.size()) return pclcehook::DirectoryOf(std::wstring(buffer.data(), length));
        if (buffer.size() >= 32768) return L".";
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring MakeAbsolute(const std::wstring& path) {
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) return path;
    std::wstring buffer(needed, L'\0');
    const DWORD written = GetFullPathNameW(path.c_str(), needed, buffer.data(), nullptr);
    if (written == 0 || written >= needed) return path;
    buffer.resize(written);
    return buffer;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/** 按 Windows 命令行解析规则为单个参数加引号并转义反斜杠/引号。 */
std::wstring QuoteArgument(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }

    std::wstring quoted;
    quoted.push_back(L'"');
    size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

bool ParseOptions(int argc, wchar_t** argv, Options& options, std::wstring& error) {
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];

        if (argument == L"--") {
            for (++index; index < argc; ++index) options.targetArguments.push_back(argv[index]);
            break;
        }
        if (argument == L"--target" || argument == L"-t") {
            if (index + 1 >= argc) {
                error = L"--target 缺少参数值";
                return false;
            }
            options.target = argv[++index];
            continue;
        }
        if (argument == L"--dll") {
            if (index + 1 >= argc) {
                error = L"--dll 缺少参数值";
                return false;
            }
            options.dll = argv[++index];
            continue;
        }
        if (argument == L"--debug-mode") {
            options.debugMode = true;
            continue;
        }
        if (argument == L"--wait") {
            options.wait = true;
            continue;
        }
        if (argument == L"--dry-run") {
            options.dryRun = true;
            continue;
        }
        if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
            options.showHelp = true;
            continue;
        }

        error = L"未知参数：" + argument;
        return false;
    }
    return true;
}

std::wstring BuildCommandLine(const std::wstring& target,
                              const std::vector<std::wstring>& arguments) {
    std::wstring commandLine = QuoteArgument(target);
    for (const std::wstring& argument : arguments) {
        commandLine.push_back(L' ');
        commandLine.append(QuoteArgument(argument));
    }
    return commandLine;
}

/**
 * 转发已附加调试器的事件，直到目标进程退出。
 *
 * 只用于让 --debug-mode 在注入后继续以调试器身份运行；注入本身始终在
 * CREATE_SUSPENDED 阶段完成，因此不会因为调试事件冻结线程而无法注入。
 */
int RunDebugLoop() {
    DebugSetProcessKillOnExit(FALSE);

    bool exitReceived = false;
    int exitCode = 0;
    DEBUG_EVENT event{};

    while (WaitForDebugEvent(&event, INFINITE) != FALSE) {
        DWORD continueStatus = DBG_CONTINUE;

        switch (event.dwDebugEventCode) {
            case LOAD_DLL_DEBUG_EVENT:
                if (event.u.LoadDll.hFile != nullptr) CloseHandle(event.u.LoadDll.hFile);
                break;

            case EXCEPTION_DEBUG_EVENT:
                if (event.u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT) {
                    continueStatus = DBG_EXCEPTION_NOT_HANDLED;
                }
                break;

            case EXIT_PROCESS_DEBUG_EVENT:
                exitCode = static_cast<int>(event.u.ExitProcess.dwExitCode);
                exitReceived = true;
                break;

            default:
                break;
        }

        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continueStatus);
        if (exitReceived) break;
    }

    return exitCode;
}

int RunSuspended(const Options& options, const std::wstring& target, const std::wstring& dllPath,
                 const std::wstring& commandLine, const std::wstring& workingDirectory,
                 bool attachDebugger) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    // CreateProcessW 会就地修改命令行缓冲区，必须传入可写副本。
    std::wstring mutableCommandLine = commandLine;
    if (CreateProcessW(target.c_str(), mutableCommandLine.data(), nullptr, nullptr, FALSE,
                       CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, nullptr,
                       workingDirectory.c_str(), &startup, &process) == FALSE) {
        PrintLine(L"CreateProcessW 失败，错误码 " + std::to_wstring(GetLastError()), true);
        return 1;
    }

    const pclcehook::InjectResult result = pclcehook::InjectHookDll(process.hProcess, dllPath);
    if (!result.ok) {
        pclcehook::log::WriteFormat(L"launcher: inject failed: %ls", result.message.c_str());
        PrintLine(L"注入失败：" + result.message, true);
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }
    PrintLine(L"注入成功：" + result.message);

    const DWORD processId = GetProcessId(process.hProcess);
    bool attached = false;
    if (attachDebugger) {
        if (DebugActiveProcess(processId) == FALSE) {
            PrintLine(L"DebugActiveProcess 失败，错误码 " + std::to_wstring(GetLastError()) +
                          L"，继续以普通方式运行",
                      true);
        } else {
            attached = true;
        }
    }

    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        PrintLine(L"ResumeThread 失败，错误码 " + std::to_wstring(GetLastError()), true);
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }
    CloseHandle(process.hThread);

    int exitCode = 0;
    if (attached) {
        exitCode = RunDebugLoop();
        PrintLine(L"目标进程已退出，退出码 " + std::to_wstring(exitCode));
    } else if (options.wait) {
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code = 0;
        if (GetExitCodeProcess(process.hProcess, &code) != FALSE) {
            exitCode = static_cast<int>(code);
            PrintLine(L"目标进程已退出，退出码 " + std::to_wstring(exitCode));
        }
    }
    CloseHandle(process.hProcess);
    return exitCode;
}

int Run(const Options& options) {
    const std::wstring launcherDirectory = ExecutableDirectory();

    std::wstring target;
    if (!options.target.empty()) {
        target = MakeAbsolute(options.target);
        if (!FileExists(target)) {
            PrintLine(L"目标可执行文件不存在：" + target, true);
            return 1;
        }
    } else {
        const std::vector<std::wstring> candidates =
            pclcehook::FindPclCeExecutables(launcherDirectory);
        if (candidates.empty()) {
            PrintLine(L"在启动器目录中未找到 PCL*CE*.exe：" + launcherDirectory, true);
            PrintLine(L"请使用 --target 指定可执行文件路径。", true);
            return 1;
        }
        if (candidates.size() > 1) {
            PrintLine(L"启动器目录中存在多个 PCL*CE*.exe，请使用 --target 指定：", true);
            for (const std::wstring& candidate : candidates) PrintLine(L"  " + candidate, true);
            return 1;
        }
        target = candidates.front();
    }

    std::wstring dllPath = options.dll.empty() ? launcherDirectory + L"\\" + pclcehook::api::kDllFileName
                                               : MakeAbsolute(options.dll);
    if (!FileExists(dllPath)) {
        PrintLine(L"hook DLL 不存在：" + dllPath, true);
        return 1;
    }

    const std::wstring commandLine = BuildCommandLine(target, options.targetArguments);
    const std::wstring workingDirectory = pclcehook::DirectoryOf(target);

    PrintLine(L"目标：" + target);
    PrintLine(L"Hook DLL：" + dllPath);
    PrintLine(L"日志：" + pclcehook::log::LogFilePath());

    if (options.dryRun) {
        PrintLine(L"--dry-run：不启动进程。");
        return 0;
    }

    pclcehook::log::ResetLog();
    pclcehook::log::WriteFormat(L"launcher: target=%ls dll=%ls debugMode=%d",
                                target.c_str(), dllPath.c_str(), options.debugMode ? 1 : 0);

    if (options.debugMode) {
        return RunSuspended(options, target, dllPath, commandLine, workingDirectory, true);
    }
    return RunSuspended(options, target, dllPath, commandLine, workingDirectory, false);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    std::wstring error;

    if (!ParseOptions(argc, argv, options, error)) {
        PrintLine(error, true);
        PrintUsage();
        return 2;
    }
    if (options.showHelp) {
        PrintUsage();
        return 0;
    }
    return Run(options);
}
