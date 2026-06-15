#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include "app_format.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>

#include <cstdint>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kWindowClassName[] = L"DingooAppToolGuiWindow";
constexpr UINT kProgressMessage = WM_APP + 1;
constexpr UINT kDoneMessage = WM_APP + 2;

enum ControlId {
    IdUnpackInput = 1001,
    IdBrowseUnpackInput,
    IdUnpackOutput,
    IdBrowseUnpackOutput,
    IdRunUnpack,
    IdPackManifest,
    IdBrowsePackManifest,
    IdPackOutput,
    IdBrowsePackOutput,
    IdRunPack,
};

enum class Operation {
    Unpack,
    Pack,
};

struct ProgressPayload {
    std::uint32_t current = 0;
    std::uint32_t total = 0;
    std::wstring message;
};

struct DonePayload {
    bool success = false;
    std::wstring message;
};

struct AppState {
    HWND unpackInput = nullptr;
    HWND browseUnpackInput = nullptr;
    HWND unpackOutput = nullptr;
    HWND browseUnpackOutput = nullptr;
    HWND runUnpack = nullptr;
    HWND packManifest = nullptr;
    HWND browsePackManifest = nullptr;
    HWND packOutput = nullptr;
    HWND browsePackOutput = nullptr;
    HWND runPack = nullptr;
    HWND progress = nullptr;
    HWND status = nullptr;
    HWND log = nullptr;
    HFONT font = nullptr;
    bool busy = false;
};

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return L"<message conversion failed>";
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), required);
    return wide;
}

std::wstring getText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }

    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1u, L'\0');
    GetWindowTextW(control, buffer.data(), static_cast<int>(buffer.size()));
    return buffer.data();
}

void setText(HWND control, const std::wstring& text) {
    SetWindowTextW(control, text.c_str());
}

bool blank(const std::wstring& text) {
    return text.find_first_not_of(L" \t\r\n") == std::wstring::npos;
}

void appendLog(HWND log, const std::wstring& text) {
    const int length = GetWindowTextLengthW(log);
    SendMessageW(log, EM_SETSEL, static_cast<WPARAM>(length), static_cast<LPARAM>(length));
    const std::wstring line = text + L"\r\n";
    SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

HWND makeControl(
    HWND parent,
    AppState& state,
    const wchar_t* className,
    const wchar_t* text,
    DWORD style,
    DWORD exStyle,
    int x,
    int y,
    int width,
    int height,
    int id) {
    HWND control = CreateWindowExW(
        exStyle,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
    if (control && state.font) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
    }
    return control;
}

void createControls(HWND window, AppState& state) {
    state.font = CreateFontW(
        -12,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    makeControl(window, state, L"BUTTON", L"Unpack", BS_GROUPBOX, 0, 12, 12, 736, 136, 0);
    makeControl(window, state, L"STATIC", L"Input .app", 0, 0, 24, 40, 92, 20, 0);
    state.unpackInput = makeControl(window, state, L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 120, 36, 520, 24, IdUnpackInput);
    state.browseUnpackInput = makeControl(window, state, L"BUTTON", L"Browse", 0, 0, 652, 35, 80, 26, IdBrowseUnpackInput);
    makeControl(window, state, L"STATIC", L"Output folder", 0, 0, 24, 76, 92, 20, 0);
    state.unpackOutput = makeControl(window, state, L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 120, 72, 520, 24, IdUnpackOutput);
    state.browseUnpackOutput = makeControl(window, state, L"BUTTON", L"Browse", 0, 0, 652, 71, 80, 26, IdBrowseUnpackOutput);
    state.runUnpack = makeControl(window, state, L"BUTTON", L"Unpack", BS_DEFPUSHBUTTON, 0, 652, 108, 80, 28, IdRunUnpack);

    makeControl(window, state, L"BUTTON", L"Pack", BS_GROUPBOX, 0, 12, 158, 736, 136, 0);
    makeControl(window, state, L"STATIC", L"Manifest", 0, 0, 24, 186, 92, 20, 0);
    state.packManifest = makeControl(window, state, L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 120, 182, 520, 24, IdPackManifest);
    state.browsePackManifest = makeControl(window, state, L"BUTTON", L"Browse", 0, 0, 652, 181, 80, 26, IdBrowsePackManifest);
    makeControl(window, state, L"STATIC", L"Output .app", 0, 0, 24, 222, 92, 20, 0);
    state.packOutput = makeControl(window, state, L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 120, 218, 520, 24, IdPackOutput);
    state.browsePackOutput = makeControl(window, state, L"BUTTON", L"Browse", 0, 0, 652, 217, 80, 26, IdBrowsePackOutput);
    state.runPack = makeControl(window, state, L"BUTTON", L"Pack", 0, 0, 652, 254, 80, 28, IdRunPack);

    state.progress = makeControl(window, state, PROGRESS_CLASSW, L"", 0, 0, 12, 314, 736, 22, 0);
    SendMessageW(state.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
    SendMessageW(state.progress, PBM_SETPOS, 0, 0);

    state.status = makeControl(window, state, L"STATIC", L"Ready.", SS_LEFT, 0, 12, 346, 736, 22, 0);
    state.log = makeControl(
        window,
        state,
        L"EDIT",
        L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        WS_EX_CLIENTEDGE,
        12,
        374,
        736,
        104,
        0);
}

std::wstring browseOpenFile(HWND owner, const wchar_t* title, const wchar_t* filter, const std::wstring& currentPath) {
    std::vector<wchar_t> buffer(32768u, L'\0');
    if (!currentPath.empty()) {
        std::wcsncpy(buffer.data(), currentPath.c_str(), buffer.size() - 1u);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrTitle = title;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) {
        return {};
    }
    return buffer.data();
}

std::wstring browseSaveFile(
    HWND owner,
    const wchar_t* title,
    const wchar_t* filter,
    const wchar_t* defaultExtension,
    const std::wstring& currentPath) {
    std::vector<wchar_t> buffer(32768u, L'\0');
    if (!currentPath.empty()) {
        std::wcsncpy(buffer.data(), currentPath.c_str(), buffer.size() - 1u);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrTitle = title;
    ofn.lpstrFilter = filter;
    ofn.lpstrDefExt = defaultExtension;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) {
        return {};
    }
    return buffer.data();
}

std::wstring browseFolder(HWND owner) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Select output folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        return {};
    }

    std::vector<wchar_t> buffer(MAX_PATH, L'\0');
    std::wstring result;
    if (SHGetPathFromIDListW(pidl, buffer.data())) {
        result = buffer.data();
    }
    CoTaskMemFree(pidl);
    return result;
}

void setBusy(AppState& state, bool busy) {
    state.busy = busy;
    const BOOL enabled = busy ? FALSE : TRUE;
    const HWND controls[] = {
        state.unpackInput,
        state.browseUnpackInput,
        state.unpackOutput,
        state.browseUnpackOutput,
        state.runUnpack,
        state.packManifest,
        state.browsePackManifest,
        state.packOutput,
        state.browsePackOutput,
        state.runPack,
    };

    for (HWND control : controls) {
        EnableWindow(control, enabled);
    }
}

void postProgress(HWND window, std::uint32_t current, std::uint32_t total, const std::string& message) {
    auto* payload = new ProgressPayload;
    payload->current = current;
    payload->total = total;
    payload->message = utf8ToWide(message);
    if (!PostMessageW(window, kProgressMessage, 0, reinterpret_cast<LPARAM>(payload))) {
        delete payload;
    }
}

void postDone(HWND window, bool success, const std::wstring& message) {
    auto* payload = new DonePayload;
    payload->success = success;
    payload->message = message;
    if (!PostMessageW(window, kDoneMessage, 0, reinterpret_cast<LPARAM>(payload))) {
        delete payload;
    }
}

void startOperation(HWND window, AppState& state, Operation operation) {
    if (state.busy) {
        return;
    }

    std::wstring input;
    std::wstring output;
    const wchar_t* operationName = operation == Operation::Unpack ? L"Unpack" : L"Pack";

    if (operation == Operation::Unpack) {
        input = getText(state.unpackInput);
        output = getText(state.unpackOutput);
    } else {
        input = getText(state.packManifest);
        output = getText(state.packOutput);
    }

    if (blank(input) || blank(output)) {
        MessageBoxW(window, L"Select both input and output paths first.", operationName, MB_ICONWARNING | MB_OK);
        return;
    }

    setBusy(state, true);
    SendMessageW(state.progress, PBM_SETPOS, 0, 0);
    setText(state.status, L"Starting...");
    setText(state.log, L"");
    appendLog(state.log, operation == Operation::Unpack ? L"Starting unpack." : L"Starting pack.");

    std::thread([window, operation, input, output]() {
        bool success = true;
        std::wstring resultMessage;

        try {
            // Worker threads never touch controls directly; each update is
            // marshalled to the UI thread through a posted heap payload.
            dingoo::ProgressCallback progress = [window](std::uint32_t current, std::uint32_t total, const std::string& message) {
                postProgress(window, current, total, message);
            };

            if (operation == Operation::Unpack) {
                dingoo::unpackApp(std::filesystem::path(input), std::filesystem::path(output), progress);
                resultMessage = L"Unpack completed.";
            } else {
                dingoo::packApp(std::filesystem::path(input), std::filesystem::path(output), progress);
                resultMessage = L"Pack completed.";
            }
        } catch (const std::exception& e) {
            success = false;
            resultMessage = utf8ToWide(e.what());
        } catch (...) {
            success = false;
            resultMessage = L"Unknown error.";
        }

        postDone(window, success, resultMessage);
    }).detach();
}

bool shouldLogProgress(const ProgressPayload& payload) {
    if (payload.total == 0 || payload.current == payload.total || payload.current <= 4) {
        return true;
    }
    return payload.current % 25u == 0u;
}

void handleProgress(AppState& state, const ProgressPayload& payload) {
    std::wstring status = payload.message;
    if (payload.total > 0) {
        const auto pos = static_cast<int>((static_cast<unsigned long long>(payload.current) * 1000ull) / payload.total);
        const auto percent = static_cast<unsigned>((static_cast<unsigned long long>(payload.current) * 100ull) / payload.total);
        SendMessageW(state.progress, PBM_SETPOS, pos, 0);
        status = std::to_wstring(percent) + L"% - " + payload.message;
    }

    setText(state.status, status);
    if (shouldLogProgress(payload)) {
        appendLog(state.log, status);
    }
}

void handleDone(HWND window, AppState& state, const DonePayload& payload) {
    if (payload.success) {
        SendMessageW(state.progress, PBM_SETPOS, 1000, 0);
        setText(state.status, payload.message);
        appendLog(state.log, payload.message);
        MessageBoxW(window, payload.message.c_str(), L"Dingoo App Tool", MB_ICONINFORMATION | MB_OK);
    } else {
        const std::wstring message = L"Failed: " + payload.message;
        setText(state.status, message);
        appendLog(state.log, message);
        MessageBoxW(window, message.c_str(), L"Dingoo App Tool", MB_ICONERROR | MB_OK);
    }
    setBusy(state, false);
}

AppState* stateFrom(HWND window) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        auto* state = new AppState;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        createControls(window, *state);
        return 0;
    }
    case WM_COMMAND: {
        AppState* state = stateFrom(window);
        if (!state) {
            return 0;
        }

        const wchar_t appFilter[] = L"Dingoo app files (*.app)\0*.app;*.APP\0All files (*.*)\0*.*\0";
        const wchar_t jsonFilter[] = L"Manifest files (*.json)\0*.json\0All files (*.*)\0*.*\0";
        switch (LOWORD(wParam)) {
        case IdBrowseUnpackInput: {
            const auto path = browseOpenFile(window, L"Select .app file", appFilter, getText(state->unpackInput));
            if (!path.empty()) {
                setText(state->unpackInput, path);
            }
            return 0;
        }
        case IdBrowseUnpackOutput: {
            const auto path = browseFolder(window);
            if (!path.empty()) {
                setText(state->unpackOutput, path);
            }
            return 0;
        }
        case IdRunUnpack:
            startOperation(window, *state, Operation::Unpack);
            return 0;
        case IdBrowsePackManifest: {
            const auto path = browseOpenFile(window, L"Select manifest.json", jsonFilter, getText(state->packManifest));
            if (!path.empty()) {
                setText(state->packManifest, path);
            }
            return 0;
        }
        case IdBrowsePackOutput: {
            const auto path = browseSaveFile(window, L"Select output .app", appFilter, L"app", getText(state->packOutput));
            if (!path.empty()) {
                setText(state->packOutput, path);
            }
            return 0;
        }
        case IdRunPack:
            startOperation(window, *state, Operation::Pack);
            return 0;
        default:
            return 0;
        }
    }
    case kProgressMessage: {
        AppState* state = stateFrom(window);
        std::unique_ptr<ProgressPayload> payload(reinterpret_cast<ProgressPayload*>(lParam));
        if (state && payload) {
            handleProgress(*state, *payload);
        }
        return 0;
    }
    case kDoneMessage: {
        AppState* state = stateFrom(window);
        std::unique_ptr<DonePayload> payload(reinterpret_cast<DonePayload*>(lParam));
        if (state && payload) {
            handleDone(window, *state, *payload);
        }
        return 0;
    }
    case WM_CLOSE: {
        AppState* state = stateFrom(window);
        if (state && state->busy) {
            MessageBoxW(window, L"Wait for the current operation to finish.", L"Dingoo App Tool", MB_ICONWARNING | MB_OK);
            return 0;
        }
        DestroyWindow(window);
        return 0;
    }
    case WM_DESTROY: {
        AppState* state = stateFrom(window);
        if (state) {
            if (state->font) {
                DeleteObject(state->font);
            }
            delete state;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;
    }
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCommand) {
    (void)previousInstance;
    (void)commandLine;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&commonControls);

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&windowClass)) {
        CoUninitialize();
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        kWindowClassName,
        L"Dingoo App Tool",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        780,
        530,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!window) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CoUninitialize();
    return static_cast<int>(message.wParam);
}
