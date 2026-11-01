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

    # ★★ 2026-11-01（账本 `TODO-06` v4.6/v4.7）：**让崩溃可符号化，但别把它吓跑**。
    #   病灶：本目标此前**不带任何调试信息**——产物既无 PDB，COFF 符号表也是空的
    #   （`PointerToSymbolTable = 0`）。于是 `.scratch/_dbg_lite.py` 抓到的
    #   `harness_gui.exe+0x2410` 只能用 `.pdata` 落到「函数区间」。
    #   `/MAP` 让链接器吐 `harness_gui.map`（Publics + **Static symbols**）——
    #   实测足以把 RVA 反查到函数名（认出 `RunAnswerProbe` / `SnapshotRows` /
    #   `std::string::_Reallocate_grow_by`），且**不改代码生成、不碰运行期**。
    #
    #   ★★ 反面教训（同日实测，别重犯）：曾加 `/Zi` + `/DEBUG` 想再拿源行，
    #     结果 **崩溃直接消失**——同一份源码，带调试信息构建 6/6 通过，
    #     不带 4~6 次里崩 4~6 次（详见账本 v4.7 的 A/B 表）。
    #     ⇒ 这类布局/时序敏感缺陷会被调试构建本身掩盖（经典 heisenbug）。
    #     故**只保留 /MAP**：它给符号、不改布局，尽可能保住缺陷的可复现性。
    #     真要抓根因，工具应是 **`/fsanitize=address` 独立构建**（不依赖布局），
    #     而不是给发布构建灌调试信息。
    #   ★ 另一个实测坑：`/MAPINFO:LINES` 在本机 link 19.51 上 `LNK1117`（选项语法错），
    #     加了就链不过 ⇒ 行号只能走 PDB，而 PDB 又会掩盖缺陷 ⇒ 这条线暂时放弃。
    target_compile_options(harness_gui PRIVATE /utf-8)
    target_link_options(harness_gui PRIVATE /MAP)
endif()

# ★★ 2026-11-01（账本 `TODO-06` v4.7 ⑥）：**可选的 ASAN 构建开关，默认关**。
#   为什么需要它：`TODO-06` 的崩溃会随构建布局"消失"（同日 A/B：同一份源码，
#   带调试信息 6/6 通过、不带 4~6 次崩 4~6 次）。凡是靠布局才能复现的悬垂指针，
#   都不能用「跑几轮看看」当判据 —— ASAN 的判据**不依赖布局**：它把释放过的块
#   留在隔离区并标记，任何时候再访问都当场报 UAF，并给出**分配点与使用点两处栈**。
#   为什么默认关：ASAN 改变分配器行为与内存布局，paints/frames/时延一类读数
#   与发布构建**不可比**（本仓纪律 3：跨轮不可比的东西不能当门）⇒ 只用于查缺陷，
#   不进门、不出基线读数。
#   ★ 用法：`cmake -S gui -B gui/build-asan -DHARNESS_ASAN=ON`（独立 build 目录，
#     别污染 `gui/build`——那棵树产的是门用的二进制）。
option(HARNESS_ASAN "用 /fsanitize=address 构建（查悬垂指针专用，读数不可当基线）" OFF)
if(MSVC AND HARNESS_ASAN)
    target_compile_options(harness_gui PRIVATE /fsanitize=address /Zi)
    # ★ 必需：`ECDI.lib` 是非 ASAN 构建，其 STL 注解标记为 0；本目标在
    #   `/fsanitize=address` 下会把注解标记置 1，链接器直接以
    #   `LNK2038: annotate_vector/string/optional 值 "0" 不匹配 "1"` 拒链（实测 132 条）。
    #   关掉注解后两侧都是 0 ⇒ 能链。
    #   ★ 代价要说清：丢的是「容器未用容量内的毒化」（能抓越界写的更细一层），
    #     **堆红区与隔离区不受影响** —— 悬垂读一个已释放的缓冲区照样当场报。
    #   这个取舍值得：不关就根本进不了 ASAN，等于什么都没有。
    target_compile_definitions(harness_gui PRIVATE _DISABLE_STL_ANNOTATION)
    # /DEBUG 只为让 ASAN 报告能落到 函数名+行号（它会警告 LNK4302 让你加）。
    target_link_options(harness_gui PRIVATE /fsanitize=address /DEBUG)
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
