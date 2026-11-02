// wizard.cpp — Asistente de bienvenida Win32 (2 páginas).
#include "wizard.h"

#include <windows.h>
#include <windowsx.h>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

const wchar_t* kLangs[][2] = {
    {L"Español", L"es"}, {L"English", L"en"}, {L"Deutsch", L"de"},
    {L"Français", L"fr"}, {L"Português", L"pt"}, {L"Italiano", L"it"},
};

struct WizardState {
    int page = 0; // 0 = bienvenida+idioma, 1 = todo listo
    int langIndex = 0;
    bool searchNow = true;
    bool done = false;
    bool cancelled = false;
    HWND hwnd = nullptr;
    HWND combo = nullptr, check = nullptr, text = nullptr, dots = nullptr;
    HWND back = nullptr, next = nullptr;
};

void PaintOrange(HDC hdc, RECT rc) {
    HBRUSH b = CreateSolidBrush(RGB(232, 90, 16)); // naranja WE
    RECT top = rc;
    top.bottom = 64;
    FillRect(hdc, &top, b);
    DeleteObject(b);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    HFONT f = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                          DEFAULT_PITCH, L"Segoe UI");
    HFONT old = (HFONT)SelectObject(hdc, f);
    DrawTextW(hdc, L"Te damos la bienvenida", -1, &RECT{20, 12, rc.right, 60},
              DT_LEFT | DT_SINGLELINE);
    SelectObject(hdc, old);
    DeleteObject(f);
}

void LayoutPage(WizardState* s) {
    const wchar_t* titles[2] = {
        L"Esta es una breve introducción para configurar algunas opciones "
        L"que te resultarán bastante importantes. Todo lo que ajustes aquí "
        L"se podrá cambiar en la configuración.",
        L"¡Ya está todo listo!\n\nAhora la aplicación estará disponible en "
        L"la bandeja del sistema. Haz doble clic para elegir un fondo o "
        L"clic derecho para acceder a la configuración."};
    SetWindowTextW(s->text, titles[s->page]);
    ShowWindow(s->combo, s->page == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(s->check, s->page == 1 ? SW_SHOW : SW_HIDE);
    SetWindowTextW(s->check, L"Buscar fondos ahora");
    SetWindowTextW(s->back, L"Atrás");
    SetWindowTextW(s->next, s->page == 0 ? L"Adelante  >" : L"Todo listo  ✓");
    EnableWindow(s->back, s->page == 1);
    wchar_t dots[16];
    wsprintfW(dots, s->page == 0 ? L"● ○" : L"○ ●");
    SetWindowTextW(s->dots, dots);
    InvalidateRect(s->hwnd, nullptr, TRUE);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    WizardState* s = (WizardState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)l;
        s = (WizardState*)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
        s->hwnd = h;
        s->text = CreateWindowExW(0, L"STATIC",
            L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 90, 440, 110, h, nullptr, nullptr, nullptr);
        s->combo = CreateWindowExW(0, L"COMBOBOX",
            nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            20, 215, 220, 160, h, nullptr, nullptr, nullptr);
        for (auto& lg : kLangs) {
            ComboBox_AddString(s->combo, lg[0]);
        }
        ComboBox_SetCurSel(s->combo, 0);
        s->check = CreateWindowExW(0, L"BUTTON", L"Buscar fondos ahora",
            WS_CHILD | BS_AUTOCHECKBOX, 20, 215, 220, 24,
            h, nullptr, nullptr, nullptr);
        Button_SetCheck(s->check, BST_CHECKED);
        s->dots = CreateWindowExW(0, L"STATIC", L"● ○",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 190, 300, 100, 24,
            h, nullptr, nullptr, nullptr);
        s->back = CreateWindowExW(0, L"BUTTON", L"Atrás",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 330, 110, 32,
            h, (HMENU)11, nullptr, nullptr);
        s->next = CreateWindowExW(0, L"BUTTON", L"Adelante  >",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
            350, 330, 110, 32, h, (HMENU)12, nullptr, nullptr);
        LayoutPage(s);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(h, &ps);
        RECT rc{};
        GetClientRect(h, &rc);
        PaintOrange(hdc, rc);
        // Fondo oscuro del cuerpo.
        HBRUSH b = CreateSolidBrush(RGB(18, 18, 22));
        RECT body = rc;
        body.top = 64;
        FillRect(hdc, &body, b);
        DeleteObject(b);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(230, 230, 230));
        static HBRUSH bg = CreateSolidBrush(RGB(18, 18, 22));
        return (LRESULT)bg;
    }
    case WM_COMMAND:
        if (LOWORD(w) == 12) { // Adelante / Todo listo
            if (s->page == 0) {
                s->langIndex = ComboBox_GetCurSel(s->combo);
                s->page = 1;
                LayoutPage(s);
            } else {
                s->searchNow = Button_GetCheck(s->check) == BST_CHECKED;
                s->done = true;
                DestroyWindow(h);
            }
        } else if (LOWORD(w) == 11 && s->page == 1) {
            s->page = 0;
            LayoutPage(s);
        }
        return 0;
    case WM_CLOSE:
        s->cancelled = true;
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

std::string ReadAll(const std::wstring& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

bool WelcomeDone(const std::wstring& resDir) {
    std::string j = ReadAll(resDir + L"\\config\\welcome.json");
    return j.find("welcomed") != std::string::npos &&
           j.find("true") != std::string::npos;
}

bool SaveWelcome(const std::wstring& resDir, const std::wstring& language,
                 bool searchNow) {
    CreateDirectoryW((resDir + L"\\config").c_str(), nullptr);
    std::ofstream f(resDir + L"\\config\\welcome.json");
    if (!f) {
        return false;
    }
    std::string lang;
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, language.c_str(), -1,
                                    nullptr, 0, nullptr, nullptr);
        lang.resize((size_t)(n > 0 ? n - 1 : 0));
        if (n > 0) {
            WideCharToMultiByte(CP_UTF8, 0, language.c_str(), -1,
                                lang.data(), n, nullptr, nullptr);
        }
    }
    f << "{\"welcomed\": true, \"language\": \"" << lang
      << "\", \"searchNow\": " << (searchNow ? "true" : "false") << "}";
    return true;
}

std::wstring RunWelcomeWizard(const std::wstring& resDir) {
    if (WelcomeDone(resDir)) {
        return L"";
    }
    WizardState s;
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WEWelcomeWizard";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"Bienvenida",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT, 480, 410,
                             nullptr, nullptr, wc.hInstance, &s);
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    MSG msg{};
    while (IsWindow(h) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // Navegación de teclado estándar (Tab / Enter en botón por defecto).
        if (IsDialogMessageW(h, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (s.cancelled || !s.done) {
        return L"";
    }
    std::wstring lang = kLangs[s.langIndex][1];
    SaveWelcome(resDir, lang, s.searchNow);
    if (s.searchNow) {
        // "Buscar fondos ahora": abre la carpeta local de fondos.
        std::wstring scenes = resDir + L"\\scenes";
        ShellExecuteW(nullptr, L"open", scenes.c_str(), nullptr, nullptr, SW_SHOW);
    }
    return lang;
}
