// file_verify.cpp — Verificación de integridad de la distribución.
#include "file_verify.h"

#include <windows.h>
#include <bcrypt.h>
#include <cstdio>

#pragma comment(lib, "bcrypt.lib")

namespace {

std::string ToHex(const BYTE* data, DWORD len) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (DWORD i = 0; i < len; ++i) {
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 0xF]);
    }
    return out;
}

void Log(const wchar_t* fmt, const wchar_t* a) {
    wchar_t buf[1024];
    swprintf_s(buf, fmt, a);
    OutputDebugStringW(buf);
}

} // namespace

std::string ComputeFileSHA256(const std::wstring& path) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return result;
    }
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        DWORD hashLen = 0, cb = 0;
        if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (BYTE*)&hashLen,
                              sizeof(hashLen), &cb, 0) == 0 &&
            BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
            BYTE chunk[65536];
            DWORD read = 0;
            BOOL ok = TRUE;
            while (ReadFile(h, chunk, sizeof(chunk), &read, nullptr) && read > 0) {
                if (BCryptHashData(hash, chunk, read, 0) != 0) {
                    ok = FALSE;
                    break;
                }
            }
            if (ok) {
                std::vector<BYTE> digest(hashLen);
                if (BCryptFinishHash(hash, digest.data(), hashLen, 0) == 0) {
                    result = ToHex(digest.data(), hashLen);
                }
            }
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    CloseHandle(h);
    return result;
}

bool VerifyFileHashes(const std::wstring& baseDir,
                      const std::vector<ExpectedFileHash>& table,
                      std::wstring& errorOut) {
    for (const auto& e : table) {
        std::wstring full = baseDir + L"\\" + e.relativePath;
        DWORD attrs = GetFileAttributesW(full.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            wchar_t msg[512];
            swprintf_s(msg, L"VerifyFileHashes dst file not found: %s\n",
                       full.c_str());
            OutputDebugStringW(msg);
            errorOut = msg;
            return false;
        }
        std::string actual = ComputeFileSHA256(full);
        if (actual.empty()) {
            wchar_t msg[512];
            swprintf_s(msg, L"VerifyFileHashes can't open dst file: %s\n",
                       full.c_str());
            OutputDebugStringW(msg);
            errorOut = msg;
            return false;
        }
        if (actual != e.sha256Hex) {
            wchar_t msg[1024];
            wchar_t aHex[65] = {}, eHex[65] = {};
            for (size_t i = 0; i < 64 && i < actual.size(); ++i) {
                aHex[i] = (wchar_t)actual[i];
            }
            for (size_t i = 0; i < 64 && i < e.sha256Hex.size(); ++i) {
                eHex[i] = (wchar_t)e.sha256Hex[i];
            }
            swprintf_s(msg, L"VerifyFileHashes hash failed for %s (%s != %s)\n",
                       full.c_str(), aHex, eHex);
            OutputDebugStringW(msg);
            errorOut = msg;
            return false;
        }
    }
    return true;
}
