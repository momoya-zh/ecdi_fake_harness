cmake_minimum_required(VERSION 3.20)
project(harness_gui LANGUAGES CXX)

# 独立工程，外部消费 ECDI（规格 §3 / 基线 B2/B6）。
# ECDI 采用 ExactVersion 兼容性，find_package 不带版本号，用 HINTS 指定前缀。
set(ECDI_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/ecdi" CACHE PATH
    "ECDI install prefix (cmake --install 的输出目录)")
find_package(ECDI CONFIG REQUIRED HINTS "${ECDI_PREFIX}")

# 新增源文件无需改本文件（GLOB 自动入库，规格 §3 沿用 ECDI 条 66 的结论）
file(GLOB_RECURSE HARNESS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(harness_gui WIN32 ${HARNESS_SOURCES})
target_include_directories(harness_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(harness_gui PRIVATE ECDI::ECDI shell32 ole32)

target_compile_definitions(harness_gui PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(harness_gui PRIVATE /utf-8)
endif()
if(MINGW)
    target_compile_options(harness_gui PRIVATE -municode)
    target_link_options(harness_gui PRIVATE -municode -static)
endif()
