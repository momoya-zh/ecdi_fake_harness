#include "Credentials.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincred.h>

#include <cstring>

namespace creds {

namespace {

// 统一的错误拼装：目标名 + Win32 错误码。★ 密钥值**从不**进任何错误文本——
// 按「别在打日志处记得遮」的教训，这里根本拿不到值就不可能漏。
std::string Fail(const char* what, const std::wstring& target, DWORD code) {
    return std::string(what) + " failed (" + std::to_string(code) +
           ") for target " + std::string(target.begin(), target.end());
}

}   // namespace

bool Write(const std::wstring& target, const std::string& secret, std::string& error) {
    if (target.empty() || secret.empty()) {
        error = "credential write needs a non-empty target and secret";
        return false;
    }
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(secret.size());
    // ★ CredWriteW 只要求 blob 为字节串，不做任何转换——按原始字节存，
    //   Read 侧拿回的就是同一串（避免宽窄转换再引入一份编码真相）。
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&cred, 0)) {
        error = Fail("CredWriteW", target, GetLastError());
        return false;
    }
    return true;
}

bool Read(const std::wstring& target, std::string& secret, std::string& error) {
    PCREDENTIALW p = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &p)) {
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND) {
            error = "credential not found for target " +
                    std::string(target.begin(), target.end());
        } else {
            error = Fail("CredReadW", target, code);
        }
        return false;
    }
    secret.assign(reinterpret_cast<const char*>(p->CredentialBlob),
                  p->CredentialBlobSize);
    CredFree(p);
    return true;
}

bool Exists(const std::wstring& target) {
    PCREDENTIALW p = nullptr;
    const BOOL ok = CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &p);
    if (ok && p) CredFree(p);
    return ok == TRUE;
}

bool Delete(const std::wstring& target, std::string& error) {
    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND) {
            return true;   // 不存在 = 已是目标状态，不算错误（探针清理要幂等）
        }
        error = Fail("CredDeleteW", target, code);
        return false;
    }
    return true;
}

}   // namespace creds
