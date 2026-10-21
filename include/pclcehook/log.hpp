#pragma once

#include <string>

namespace pclcehook::log {

/** 返回共享诊断日志的完整路径（%TEMP%\PCLCEHook.log）。 */
std::wstring LogFilePath();

/**
 * 清空日志文件。启动器在拉起目标进程前调用，避免上一轮记录掩盖本次结果。
 *
 * @return 是否成功清空（文件不存在也算成功）。
 */
bool ResetLog();

/**
 * 追加一行带时间戳与 PID 的日志。
 *
 * @param text 日志正文，不允许包含换行符。
 * @note 内部使用 CreateFileW/WriteFile，可在 DllMain 之外的任意线程调用；
 *       失败时静默忽略，不影响被注入进程。
 */
void Write(const std::wstring& text);

/**
 * 按 printf 风格格式化后写日志。
 *
 * @param format 格式串，支持 %s（wchar_t*）、%d、%u、%ls 等标准 C 宽度格式。
 */
void WriteFormat(const wchar_t* format, ...);

}  // namespace pclcehook::log
