cmake_minimum_required(VERSION 3.20)
project(harness_gui LANGUAGES CXX)

# 独立工程，外部消费 ECDI（规格 §3 / 基线 B2/B6）。
# ECDI 采用 ExactVersion 兼容性，find_package 不带版本号，用 HINTS 指定前缀。
set(ECDI_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/ecdi" CACHE PATH
    "ECDI install prefix (cmake --install 的输出目录)")
find_package(ECDI CONFIG REQUIRED HINTS "${ECDI_PREFIX}")

# ★★ 2026-10-21：**这份前缀只有 Release 配置**（静态库 + MSVC）。
#
#   为什么要在配置期就拦：`ECDITargets-release.cmake` 只声明
#   `IMPORTED_CONFIGURATIONS RELEASE`。CMake 找不到与当前配置同名的导入配置时
#   **会静默回退**到 Release —— 于是 `--config Debug` 会一路编到最后，
#   再吐几十条 `LNK2038: RuntimeLibrary 值 "MD_DynamicRelease" 不匹配 "MDd_DynamicDebug"`
#   （外加同样数量的 `_ITERATOR_DEBUG_LEVEL 0 vs 2`）。
#   ★ 实测确认过：不是推测（2026-10-21 用 `--config Debug` 复现）。
#   ⇒ 这里在配置期给一条**说得清原因**的错误，而不是让人在链接阶段猜。
#   ★ 要 Debug 版：用 ECDI 仓的 Debug 配置再 `cmake --install` 一次，
#     并把 `ECDI_PREFIX` 指过去（同前缀不能同时装两种 CRT）。
#
#   ★★ 两种生成器要分别拦（这是本仓踩过的老坑：多配置生成器下
#     `CMAKE_BUILD_TYPE` **恒为空**，只写单配置那条等于没写）：
#     · 单配置（Ninja / Makefile）：配置期就知道，直接 FATAL_ERROR；
#     · 多配置（Visual Studio / Xcode）：配置期无法预知 `--config`，
#       故把判据挂到**生成期**（`$<CONFIG:Debug>` + `$<TARGET_EXISTS>` 不合适，
#       用 `add_custom_command` 不成立）——改用一个**构建期**守卫：
#       给目标挂 `/D` 不可行，故用 CMake 的 `$<CONFIG:...>` 生成表达式
#       在**链接行**上放一个必然失败的伪对象路径。见下方 LINK_OPTIONS。
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR
        "This ECDI prefix ships Release only (static lib, /MD). "
        "You asked for CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'. "
        "Build with -DCMAKE_BUILD_TYPE=Release, or install a Debug prefix and point ECDI_PREFIX at it.")
endif()

# 新增源文件无需改本文件（GLOB 自动入库，规格 §3 沿用 ECDI 条 66 的结论）
file(GLOB_RECURSE HARNESS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(harness_gui WIN32 ${HARNESS_SOURCES})
target_include_directories(harness_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# shell32/ole32：文件夹选择对话框（SHBrowseForFolderW / CoTaskMemFree）；
# comdlg32：保存文件对话框（GetSaveFileNameW）——2026-10-20 中栏 `↗` 导出转录用。
# advapi32：Windows Credential Manager（CredWriteW/CredReadW）——P3.5 凭据存储。
target_link_libraries(harness_gui PRIVATE ECDI::ECDI shell32 ole32 comdlg32 advapi32)

target_compile_definitions(harness_gui PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(harness_gui PRIVATE /utf-8)
endif()

# ★★ 多配置生成器下的 Debug 守卫（见文件顶部注释）。
#   ★ 做法：Debug 配置往链接行塞一个**故意不存在**的输入文件。
#     为什么用这种笨办法而不是 FATAL_ERROR：`--config` 在配置期不可知，
#     而 CMake 没有"按配置在生成期报错"的干净机制。这个伪输入会让链接器
#     明确报 `cannot open input file`，并且我们把原因写进了文件名本身——
#     错误信息自己解释自己，不需要谁去翻 CMakeLists。
#   ★ Release 配置下该表达式展开为空。**已验证的是行为**：Release 构建成功，
#     且 `--grow-probe` 七相与全量夹具基线逐位不变。
#     ⚠️ **未验证**的是"Release 产物逐字节不变"——那是另一句话，这里不写。
if(MSVC)
    target_link_options(harness_gui PRIVATE
        "$<$<CONFIG:Debug>:"
        "ECDI_prefix_is_Release_only__build_with_--config_Release_or_install_a_Debug_prefix.lib>")
endif()
if(MINGW)
    target_compile_options(harness_gui PRIVATE -municode)
    target_link_options(harness_gui PRIVATE -municode -static)
endif()
