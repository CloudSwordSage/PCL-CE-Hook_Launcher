#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace pclcehook {

/** 返回路径所在的目录；路径不含分隔符时返回 "."。 */
std::wstring DirectoryOf(const std::wstring& path);

/**
 * 在指定目录中查找与 `PCL*CE*.exe` 匹配的可执行文件。
 *
 * @param directory 只搜索该目录一层，不进入子目录。
 * @return 按路径排序的候选列表；目录不可读时返回空列表。
 */
std::vector<std::wstring> FindPclCeExecutables(const std::wstring& directory);

}  // namespace pclcehook
