#include "pclcehook/target.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace {

std::wstring ToLower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return text;
}

/**
 * 匹配 `PCL*CE*.exe`（不区分大小写）。
 *
 * 用户要求只认同级目录中的 PCL 可执行文件，因此这里按文件名通配，不做路径推断。
 */
bool MatchesPclCePattern(const std::wstring& fileName) {
    constexpr std::wstring_view prefix = L"pcl";
    constexpr std::wstring_view infix = L"ce";
    constexpr std::wstring_view suffix = L".exe";

    const std::wstring lower = ToLower(fileName);
    if (lower.size() <= prefix.size() + infix.size() + suffix.size()) return false;
    if (lower.compare(0, prefix.size(), prefix) != 0) return false;
    if (lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) != 0) return false;

    const size_t infixPosition = lower.find(infix, prefix.size());
    return infixPosition != std::wstring::npos &&
           infixPosition + infix.size() <= lower.size() - suffix.size();
}

}  // namespace

std::wstring pclcehook::DirectoryOf(const std::wstring& path) {
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return L".";
    return path.substr(0, separator);
}

std::vector<std::wstring> pclcehook::FindPclCeExecutables(const std::wstring& directory) {
    std::vector<std::wstring> matches;

    std::error_code error;
    std::filesystem::directory_iterator iterator(directory,
                                                 std::filesystem::directory_options::skip_permission_denied,
                                                 error);
    if (error) return matches;

    for (const std::filesystem::directory_entry& entry : iterator) {
        std::error_code entryError;
        if (!entry.is_regular_file(entryError) || entryError) continue;

        const std::wstring fileName = entry.path().filename().wstring();
        if (MatchesPclCePattern(fileName)) {
            matches.push_back(entry.path().wstring());
        }
    }

    std::sort(matches.begin(), matches.end());
    return matches;
}
