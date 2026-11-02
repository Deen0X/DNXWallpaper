// launcher.cpp — Lanzador de DNXWallpaper v3.
//
// Arranca el motor (DNXWallpaper.Motor.exe) y le pasa el idioma del sistema.
// La verificación de integridad usa el manifiesto externo res/hashes.json.
//
// Uso:
//   DNXWallpaper.exe [-run <exe>] [-final] [-forceupdate] [-start]
//                    [-showbrowse] [-nobrowse] [-language XX]
//                    [-clearWindowPositions]
#include <windows.h>
#include <shlwapi.h>
#include <string>
#include <vector>

#include "file_verify.h"
#include "manifest.h"
#include "distribution.h"

#pragma comment(lib, "shlwapi.lib")

namespace {

// Manifiesto EXTERNO (res/hashes.json). Sin manifiesto o vacío -> desarrollo.

bool HasFlag(const std::wstring& cmd, const wchar_t* flag) {
    return cmd.find(flag) != std::wstring::npos;
}

std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return path;
}

bool FileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Lanza un proceso hijo y retorna su código de salida (o -1 en error).
// Equivale al helper original fcn.14000faa0 (CreateProcessW + espera).
int LaunchAndWait(const std::wstring& exe, const std::wstring& args,
                  const std::wstring& workDir) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // Flags originales observados: 0x228 (CREATE_NO_WINDOW parcial + ...)
    constexpr DWORD kFlags = 0x228;
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                        kFlags, nullptr, workDir.c_str(), &si, &pi)) {
        DWORD e = GetLastError();
        wchar_t msg[1024];
        swprintf_s(msg, L"Launcher cannot run the executable: %s (%s, code: %i)!\n",
                   exe.c_str(), cmd.c_str(), (int)e);
        MessageBoxW(nullptr, msg, L"DNXWallpaper", MB_OK | MB_ICONERROR);
        return -1;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR lpCmdLine, int) {
    std::wstring cmd = lpCmdLine ? lpCmdLine : L"";
    std::wstring dir = ExeDir();

    // 1. Verificación de integridad (manifiesto EXTERNO res/hashes.json).
    std::vector<ExpectedFileHash> table = LoadHashManifest(dir);
    if (!table.empty()) {
        std::wstring err;
        if (!VerifyFileHashes(dir, table, err)) {
            MessageBoxW(nullptr, err.c_str(), L"DNXWallpaper", MB_OK | MB_ICONERROR);
            return 1;
        }
    }

    // 2. "-clearWindowPositions": limpia posiciones de ventana (registro LOCAL).
    if (HasFlag(cmd, L"-clearWindowPositions")) {
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\DNXWallpaper\\Interface\\");
    }

    // 3. "-run <exe>": lanzamiento directo (modo mantenimiento/desarrollo).
    if (HasFlag(cmd, L"-run")) {
        // Formato original: -run <nombre.exe> (regex: -run\s+[\w.]+)
        size_t pos = cmd.find(L"-run");
        size_t start = cmd.find_first_not_of(L" \t", pos + 4);
        size_t end = cmd.find_first_of(L" \t", start);
        std::wstring target = cmd.substr(start, end - start);
        std::wstring full = dir + L"\\" + target;
        return LaunchAndWait(full, L"", dir);
    }

    // 4. Localizar el ejecutable principal.
    std::wstring mainExe = dir + L"\\DNXWallpaper.Motor.exe";
    if (!FileExists(mainExe)) {
        MessageBoxW(nullptr,
            L"No se encuentra DNXWallpaper.Motor.exe.\n\n"
            L"Puede que el antivirus haya eliminado archivos de la aplicación. "
            L"Añade la carpeta a las exclusiones y reinstala.",
            L"DNXWallpaper", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 5. Construir argumentos del hijo (sin servicios externos).
    //    Se reenvían los flags locales conocidos; el resto se ignora.
    std::wstring childArgs;
    for (const wchar_t* f : {L"-final", L"-forceupdate", L"-start",
                             L"-showbrowse", L"-nobrowse",
                             L"-clearWindowPositions"}) {
        if (HasFlag(cmd, f)) {
            childArgs += L" ";
            childArgs += f;
        }
    }
    // Idioma: -language XX explícito o idioma del SO (antes: servicios externos).
    if (HasFlag(cmd, L"-language")) {
        size_t pos = cmd.find(L"-language");
        size_t start = cmd.find_first_not_of(L" \t", pos + 9);
        size_t end = cmd.find_first_of(L" \t", start);
        childArgs += L" -language " + cmd.substr(start, end - start);
    } else {
        wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0) {
            childArgs += L" -language ";
            childArgs += std::wstring(locale).substr(0, 2);
        }
    }

    // 6. Lanzar y esperar (el original además dormía 500 ms con -showbrowse).
    if (HasFlag(cmd, L"-showbrowse")) {
        Sleep(500);
    }
    return LaunchAndWait(mainExe, childArgs, dir);
}
