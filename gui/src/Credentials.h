#pragma once
// Windows Credential Manager 薄封装（P3.5，规格 §8.5 D4）。
//
// 定位：**读写都在 GUI 侧**——core 不接触凭据 API（保住零第三方、不绑 Windows），
// key 经 CreateProcessW 显式 env block 注入（D5）。本文件是 GUI 侧唯一的
// 凭据出入口，错误信息只含目标名与 Win32 错误码，**绝不含密钥本身**。
//
// 诚实边界（positioning §4）：Credential Manager 防的是静态文件读取；
// 同一 Windows 用户上下文的程序仍可枚举凭据——这是 at-rest 卫生，不是安全边界。

#include <string>

namespace creds {

// 写入一个通用凭据（CRED_PERSIST_LOCAL_MACHINE，绑定本机当前用户）。
// 已存在同名目标时**覆盖**（轮换语义：下次启动生效）。
bool Write(const std::wstring& target, const std::string& secret, std::string& error);

// 读取；不存在 → false + error 说明（区别于其它失败）。
bool Read(const std::wstring& target, std::string& secret, std::string& error);

// 目标是否存在（只查不读值；设置界面用它决定「留空 = 保持不变」的提示）。
bool Exists(const std::wstring& target);

// 删除（探针清理用；不存在不算错误）。
bool Delete(const std::wstring& target, std::string& error);

}   // namespace creds
