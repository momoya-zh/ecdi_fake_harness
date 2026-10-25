// ============================================================
// 路径 UTF-8 ⇄ 宽字符（2026-10-25 从 App.cpp 匿名命名空间搬出）
// ------------------------------------------------------------
// 为什么搬：这三个函数原本是 App.cpp 的内部链接 helper，只有那个 TU 能用。
//   拆分后探针（ProbeFlow/ProbeChrome/ProbeSidebar）也要转路径——
//   复制一份会让「失败返回空串」这条约定出现多个实现。
// ★ 函数体与搬家前**逐字节相同**（门 2 的函数体指纹可比）。
// ============================================================

#include "AppInternal.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace harness {
namespace internal {

std::string Utf8FromWidePath(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), out.data(), size,
                            nullptr, nullptr) != size) return {};
    return out;
}

std::wstring WideFromUtf8Path(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), out.data(), size) != size) return {};
    return out;
}

std::string PathToUtf8(const std::filesystem::path& value) {
    return Utf8FromWidePath(value.native());
}

}  // namespace internal
}  // namespace harness
