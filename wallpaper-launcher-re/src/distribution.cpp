// distribution.cpp — Instalación/actualización de la distribución local.
#include "distribution.h"

#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;

namespace {

void Log(const wchar_t* fmt, int code, const wchar_t* path) {
    wchar_t buf[1024];
    if (path) {
        swprintf_s(buf, fmt, code, path);
    } else {
        swprintf_s(buf, fmt, code);
    }
    OutputDebugStringW(buf);
}

bool EqualsI(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

} // namespace

bool CloseProcessesForUpdate(const wchar_t* const* exeNames, size_t count) {
    OutputDebugStringW(L"core_updater_status_closing_processes\n");
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        Log(L"InstallDistribution: CreateToolhelp32Snapshot failed with %i (%s)\n", (int)e, nullptr);
        return false;
    }
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (!Process32FirstW(snap, &pe)) {
        DWORD e = GetLastError();
        Log(L"InstallDistribution: Process32First failed with %i (%s)\n", (int)e, nullptr);
        CloseHandle(snap);
        return false;
    }
    bool allOk = true;
    do {
        for (size_t i = 0; i < count; ++i) {
            if (!EqualsI(pe.szExeFile, exeNames[i])) {
                continue;
            }
            HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
            if (!h) {
                DWORD e = GetLastError();
                Log(L"InstallDistribution: OpenProcess failed with error code %i (%s)\n",
                    (int)e, pe.szExeFile);
                allOk = false;
                continue;
            }
            OutputDebugStringW(L"core_updater_status_closing_processes_wait_quit\n");
            if (!TerminateProcess(h, 0)) {
                DWORD e = GetLastError();
                Log(L"InstallDistribution: TerminateProcess failed with %i (%s)\n",
                    (int)e, pe.szExeFile);
                allOk = false;
                CloseHandle(h);
                continue;
            }
            DWORD w = WaitForSingleObject(h, 15000);
            if (w != WAIT_OBJECT_0) {
                OutputDebugStringW(
                    L"core_updater_status_closing_processes_wait_quit_terminate_error\n");
                allOk = false;
            } else {
                OutputDebugStringW(
                    L"core_updater_status_closing_processes_wait_quit_terminate\n");
            }
            CloseHandle(h);
        }
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    if (!allOk) {
        OutputDebugStringW(
            L"InstallDistribution: Not all processes could be terminated for the update.\n");
    }
    return allOk;
}

bool CopyDistribution(const std::wstring& distroDir, const std::wstring& installDir) {
    if (!fs::exists(distroDir)) {
        OutputDebugStringW(L"InstallDistribution: Missing distribution directory.\n");
        return false;
    }
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(distroDir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            wchar_t msg[512];
            swprintf_s(msg, L"InstallDistribution: fsInternal::recursive_directory_iterator error %hs (%s)\n",
                       ec.message().c_str(), it->path().c_str());
            OutputDebugStringW(msg);
            OutputDebugStringW(L"core_updater_status_distro_prepare_error\n");
            return false;
        }
        const fs::path& src = it->path();
        fs::path rel = fs::relative(src, distroDir, ec);
        if (ec) {
            OutputDebugStringW(L"core_updater_status_distro_file_path_error\n");
            return false;
        }
        fs::path dst = fs::path(installDir) / rel;
        if (it->is_directory(ec)) {
            fs::create_directories(dst, ec);
            if (ec) {
                wchar_t msg[512];
                swprintf_s(msg, L"InstallDistribution: %s fs::create_directories error %hs\n",
                           dst.c_str(), ec.message().c_str());
                OutputDebugStringW(msg);
                OutputDebugStringW(L"core_updater_status_distro_file_directory_error\n");
                return false;
            }
        } else if (it->is_regular_file(ec)) {
            fs::create_directories(dst.parent_path(), ec);
            ec.clear();
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                wchar_t msg[1024];
                swprintf_s(msg, L"InstallDistribution: %s -> %s fs::copy error %hs\n",
                           src.c_str(), dst.c_str(), ec.message().c_str());
                OutputDebugStringW(msg);
                OutputDebugStringW(L"core_updater_status_distro_file_copy_error\n");
                return false;
            }
        }
    }
    return true;
}

bool InstallDistribution(const std::wstring& distroDir, const std::wstring& installDir) {
    static const wchar_t* kManaged[] = {
        L"DNXWallpaper.Motor.exe", L"DNXWallpaper.Motor.exe",
        L"DNXWallpaper.UI.exe", L"webDNXWallpaper.Motor.exe", L"edgeDNXWallpaper.Motor.exe",
    };
    CloseProcessesForUpdate(kManaged, _countof(kManaged));
    if (!CopyDistribution(distroDir, installDir)) {
        OutputDebugStringW(L"InstallDistribution: failed handling distribution copy\n");
        return false;
    }
    return true;
}
