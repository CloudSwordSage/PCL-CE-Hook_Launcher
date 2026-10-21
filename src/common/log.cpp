#include "pclcehook/log.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>

namespace {

constexpr wchar_t kLogFileName[] = L"PCLCEHook.log";

std::wstring TempDirectory() {
    const DWORD needed = GetTempPathW(0, nullptr);
    if (needed == 0) return L".";
    std::wstring buffer(needed, L'\0');
    const DWORD written = GetTempPathW(needed, buffer.data());
    if (written == 0 || written > needed) return L".";
    buffer.resize(written);
    return buffer;
}

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

}  // namespace

std::wstring pclcehook::log::LogFilePath() {
    return TempDirectory() + kLogFileName;
}

bool pclcehook::log::ResetLog() {
    const std::wstring path = LogFilePath();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return true;
}

void pclcehook::log::Write(const std::wstring& text) {
    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t stamp[80];
    const int stampLength = std::swprintf(stamp, sizeof(stamp) / sizeof(stamp[0]),
                                          L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] ",
                                          now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                                          now.wSecond, now.wMilliseconds, GetCurrentProcessId());
    std::wstring line;
    if (stampLength > 0) {
        line.append(stamp, static_cast<size_t>(stampLength));
    }
    line.append(text);
    line.append(L"\r\n");

    const std::string utf8 = ToUtf8(line);
    if (utf8.empty()) return;

    const std::wstring path = LogFilePath();
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}

void pclcehook::log::WriteFormat(const wchar_t* format, ...) {
    wchar_t buffer[1024];
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vswprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), format, arguments);
    va_end(arguments);

    if (written <= 0) {
        Write(L"<log formatting failed>");
        return;
    }
    const size_t length = static_cast<size_t>(written);
    Write(std::wstring(buffer, length < sizeof(buffer) / sizeof(buffer[0]) ? length
                                                                          : sizeof(buffer) / sizeof(buffer[0]) - 1));
}
