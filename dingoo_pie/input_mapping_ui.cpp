#include "input_mapping_ui.h"

#ifdef _WIN32

#include "input_controls.h"
#include "platform_win32.h"
#include "resource_ids.h"

#include <SDL2/SDL.h>
#include <commctrl.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <uxtheme.h>
#include <wchar.h>
#include <windows.h>

static HWND g_inputMappingWindow = NULL;
static HBRUSH g_inputMappingBackgroundBrush = NULL;
static HFONT g_inputMappingFont = NULL;
static bool g_inputMappingOwnFont = false;
static bool g_keyboardMappingPending = false;
static uint32_t g_keyboardMappingTarget = 0;
static UiLanguage g_inputMappingLanguage = UI_LANGUAGE_ENGLISH;
static EmulatorSettings* g_inputMappingSettings = NULL;
static InputMappingUiCallbacks g_inputMappingCallbacks = {};

struct InputMappingControlRow
{
    uint32_t controlBit;
    const wchar_t* labelEn;
    const wchar_t* labelZh;
};

static const InputMappingControlRow kInputMappingRows[] =
{
    { CONTROL_BUTTON_A, L"A", L"A" },
    { CONTROL_BUTTON_B, L"B", L"B" },
    { CONTROL_BUTTON_X, L"X", L"X" },
    { CONTROL_BUTTON_Y, L"Y", L"Y" },
    { CONTROL_BUTTON_START, L"START", L"START" },
    { CONTROL_BUTTON_SELECT, L"SELECT", L"SELECT" },
    { CONTROL_TRIGGER_LEFT, L"Left Shoulder", L"\u5de6\u80a9\u952e" },
    { CONTROL_TRIGGER_RIGHT, L"Right Shoulder", L"\u53f3\u80a9\u952e" },
    { CONTROL_DPAD_UP, L"D-pad Up", L"\u65b9\u5411\u952e\u4e0a" },
    { CONTROL_DPAD_DOWN, L"D-pad Down", L"\u65b9\u5411\u952e\u4e0b" },
    { CONTROL_DPAD_LEFT, L"D-pad Left", L"\u65b9\u5411\u952e\u5de6" },
    { CONTROL_DPAD_RIGHT, L"D-pad Right", L"\u65b9\u5411\u952e\u53f3" },
};

static const int kInputMappingIdKeyboardBase = 41000;
static const int kInputMappingIdControllerBase = 41100;
static const int kInputMappingIdKeyboardTextBase = 41200;
static const int kInputMappingIdControllerTextBase = 41300;
static const int kInputMappingIdResetKeyboard = 41400;
static const int kInputMappingIdResetController = 41401;
static const int kInputMappingIdClose = 41402;
static const int kInputMappingIdStatus = 41403;

static bool inputMappingChinese(void)
{
    return g_inputMappingLanguage == UI_LANGUAGE_CHINESE;
}

static uint32_t inputMappingControlMask(uint32_t controlBit)
{
    return 1u << controlBit;
}

static const char* inputMappingControlName(uint32_t controlBit)
{
    switch (controlBit)
    {
    case CONTROL_BUTTON_A: return "A";
    case CONTROL_BUTTON_B: return "B";
    case CONTROL_BUTTON_X: return "X";
    case CONTROL_BUTTON_Y: return "Y";
    case CONTROL_BUTTON_START: return "Start";
    case CONTROL_BUTTON_SELECT: return "Select";
    case CONTROL_TRIGGER_LEFT: return "L";
    case CONTROL_TRIGGER_RIGHT: return "R";
    case CONTROL_DPAD_UP: return "Up";
    case CONTROL_DPAD_DOWN: return "Down";
    case CONTROL_DPAD_LEFT: return "Left";
    case CONTROL_DPAD_RIGHT: return "Right";
    default: return "None";
    }
}

static std::wstring asciiToWide(const char* text)
{
    std::wstring wide;
    if (!text)
    {
        return wide;
    }
    while (*text)
    {
        wide.push_back((wchar_t)(unsigned char)*text);
        text++;
    }
    return wide;
}

static std::wstring utf8ToWideSimple(const std::string& text)
{
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (size <= 0)
    {
        return asciiToWide(text.c_str());
    }
    std::wstring out((size_t)size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], size);
    return out;
}

static std::wstring inputMappingControlDisplayName(uint32_t controlBit)
{
    uint32_t targetMask = inputMappingControlMask(controlBit);
    bool zh = inputMappingChinese();
    if (targetMask == inputMappingControlMask(CONTROL_TRIGGER_LEFT))
    {
        return zh ? L"\u5de6\u80a9\u952e" : L"Left shoulder";
    }
    if (targetMask == inputMappingControlMask(CONTROL_TRIGGER_RIGHT))
    {
        return zh ? L"\u53f3\u80a9\u952e" : L"Right shoulder";
    }
    if (targetMask == inputMappingControlMask(CONTROL_DPAD_UP))
    {
        return zh ? L"\u65b9\u5411\u952e\u4e0a" : L"D-pad up";
    }
    if (targetMask == inputMappingControlMask(CONTROL_DPAD_DOWN))
    {
        return zh ? L"\u65b9\u5411\u952e\u4e0b" : L"D-pad down";
    }
    if (targetMask == inputMappingControlMask(CONTROL_DPAD_LEFT))
    {
        return zh ? L"\u65b9\u5411\u952e\u5de6" : L"D-pad left";
    }
    if (targetMask == inputMappingControlMask(CONTROL_DPAD_RIGHT))
    {
        return zh ? L"\u65b9\u5411\u952e\u53f3" : L"D-pad right";
    }
    return asciiToWide(inputMappingControlName(controlBit));
}

static const wchar_t* inputMappingRowLabel(const InputMappingControlRow& row)
{
    return inputMappingChinese() ? row.labelZh : row.labelEn;
}

static int inputMappingRowIndexForButton(int id, int base)
{
    int index = id - base;
    if (index < 0 || index >= (int)(sizeof(kInputMappingRows) / sizeof(kInputMappingRows[0])))
    {
        return -1;
    }
    return index;
}

static HICON inputMappingLoadIcon(int size)
{
    return (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_DINGOO_PIE),
        IMAGE_ICON, size, size, LR_DEFAULTCOLOR | LR_SHARED);
}

static void applyInputMappingIcon(HWND window)
{
    if (!window)
    {
        return;
    }

    HICON largeIcon = inputMappingLoadIcon(32);
    HICON smallIcon = inputMappingLoadIcon(16);
    if (largeIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
    }
    if (smallIcon)
    {
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
    }
}

static HWND inputMappingItem(int id)
{
    return g_inputMappingWindow ? GetDlgItem(g_inputMappingWindow, id) : NULL;
}

static HFONT inputMappingFont(void)
{
    if (g_inputMappingFont)
    {
        return g_inputMappingFont;
    }

    NONCLIENTMETRICSW metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0))
    {
        g_inputMappingFont = CreateFontIndirectW(&metrics.lfMessageFont);
        g_inputMappingOwnFont = g_inputMappingFont != NULL;
    }
    if (!g_inputMappingFont)
    {
        g_inputMappingFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_inputMappingOwnFont = false;
    }
    return g_inputMappingFont;
}

void inputMappingUiSetStatus(const wchar_t* text)
{
    HWND status = inputMappingItem(kInputMappingIdStatus);
    if (status)
    {
        SendMessageW(status, WM_SETREDRAW, FALSE, 0);
        SetWindowTextW(status, L"");
        SetWindowTextW(status, text ? text : L"");
        SendMessageW(status, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(status, NULL, TRUE);
        UpdateWindow(status);
    }
}

static std::string inputMappingControllerSourceForControl(uint32_t controlBit)
{
    if (!g_inputMappingCallbacks.controllerSourceForControl)
    {
        return "None";
    }
    return g_inputMappingCallbacks.controllerSourceForControl(controlBit);
}

void inputMappingUiRefresh(void)
{
    if (!g_inputMappingWindow)
    {
        return;
    }
    for (int i = 0; i < (int)(sizeof(kInputMappingRows) / sizeof(kInputMappingRows[0])); ++i)
    {
        uint32_t controlBit = kInputMappingRows[i].controlBit;
        SetWindowTextW(inputMappingItem(kInputMappingIdKeyboardTextBase + i),
            utf8ToWideSimple(inputKeyboardSourceForControl(controlBit)).c_str());
        SetWindowTextW(inputMappingItem(kInputMappingIdControllerTextBase + i),
            utf8ToWideSimple(inputMappingControllerSourceForControl(controlBit)).c_str());
    }
}

static void saveKeyboardMappingFromRuntime(void)
{
    if (!g_inputMappingSettings)
    {
        return;
    }
    g_inputMappingSettings->keyboardMapping = inputCurrentKeyboardMapping();
    emulatorSaveSettings(*g_inputMappingSettings);
    if (g_inputMappingCallbacks.settingsChanged)
    {
        g_inputMappingCallbacks.settingsChanged();
    }
}

static void beginKeyboardMappingCapture(uint32_t controlBit)
{
    g_keyboardMappingPending = true;
    g_keyboardMappingTarget = controlBit;
    std::wstring status;
    if (inputMappingChinese())
    {
        status = L"\u8bf7\u6309\u4e0b\u8981\u6620\u5c04\u5230 ";
        status += inputMappingControlDisplayName(controlBit);
        status += L" \u7684\u952e\u76d8\u6309\u952e\uff0cEsc \u53d6\u6d88\u3002";
    }
    else
    {
        status = L"Press a keyboard key for ";
        status += inputMappingControlDisplayName(controlBit);
        status += L"; Esc cancels.";
    }
    inputMappingUiSetStatus(status.c_str());
    SetFocus(g_inputMappingWindow);
}

static SDL_Scancode win32VirtualKeyToScancode(WPARAM virtualKey, LPARAM keyData)
{
    if (virtualKey >= 'A' && virtualKey <= 'Z')
    {
        return (SDL_Scancode)(SDL_SCANCODE_A + (virtualKey - 'A'));
    }
    if (virtualKey >= '1' && virtualKey <= '9')
    {
        return (SDL_Scancode)(SDL_SCANCODE_1 + (virtualKey - '1'));
    }
    if (virtualKey == '0') return SDL_SCANCODE_0;

    switch (virtualKey)
    {
    case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
    case VK_BACK: return SDL_SCANCODE_BACKSPACE;
    case VK_TAB: return SDL_SCANCODE_TAB;
    case VK_RETURN: return (keyData & (1 << 24)) ? SDL_SCANCODE_KP_ENTER : SDL_SCANCODE_RETURN;
    case VK_SPACE: return SDL_SCANCODE_SPACE;
    case VK_PRIOR: return SDL_SCANCODE_PAGEUP;
    case VK_NEXT: return SDL_SCANCODE_PAGEDOWN;
    case VK_END: return SDL_SCANCODE_END;
    case VK_HOME: return SDL_SCANCODE_HOME;
    case VK_LEFT: return SDL_SCANCODE_LEFT;
    case VK_UP: return SDL_SCANCODE_UP;
    case VK_RIGHT: return SDL_SCANCODE_RIGHT;
    case VK_DOWN: return SDL_SCANCODE_DOWN;
    case VK_INSERT: return SDL_SCANCODE_INSERT;
    case VK_DELETE: return SDL_SCANCODE_DELETE;
    case VK_LSHIFT: return SDL_SCANCODE_LSHIFT;
    case VK_RSHIFT: return SDL_SCANCODE_RSHIFT;
    case VK_SHIFT: return SDL_SCANCODE_LSHIFT;
    case VK_LCONTROL: return SDL_SCANCODE_LCTRL;
    case VK_RCONTROL: return SDL_SCANCODE_RCTRL;
    case VK_CONTROL: return SDL_SCANCODE_LCTRL;
    case VK_LMENU: return SDL_SCANCODE_LALT;
    case VK_RMENU: return SDL_SCANCODE_RALT;
    case VK_MENU: return SDL_SCANCODE_LALT;
    case VK_F1: return SDL_SCANCODE_F1;
    case VK_F2: return SDL_SCANCODE_F2;
    case VK_F3: return SDL_SCANCODE_F3;
    case VK_F4: return SDL_SCANCODE_F4;
    case VK_F5: return SDL_SCANCODE_F5;
    case VK_F6: return SDL_SCANCODE_F6;
    case VK_F7: return SDL_SCANCODE_F7;
    case VK_F8: return SDL_SCANCODE_F8;
    case VK_F9: return SDL_SCANCODE_F9;
    case VK_F10: return SDL_SCANCODE_F10;
    case VK_F11: return SDL_SCANCODE_F11;
    case VK_F12: return SDL_SCANCODE_F12;
    default:
        break;
    }

    return SDL_GetScancodeFromKey((SDL_Keycode)MapVirtualKeyW((UINT)virtualKey, MAPVK_VK_TO_CHAR));
}

static void finishKeyboardMappingCapture(SDL_Scancode scancode)
{
    if (!g_keyboardMappingPending)
    {
        return;
    }
    uint32_t target = g_keyboardMappingTarget;
    g_keyboardMappingPending = false;
    g_keyboardMappingTarget = 0;
    if (scancode != SDL_SCANCODE_ESCAPE && inputSetKeyboardMappingForControl(target, scancode))
    {
        saveKeyboardMappingFromRuntime();
        inputMappingUiRefresh();
        inputMappingUiSetStatus(inputMappingChinese() ?
            L"\u952e\u76d8\u6620\u5c04\u5df2\u4fdd\u5b58\u3002" : L"Keyboard mapping saved.");
        printf("frontend: keyboard mapping saved target=%s source=%s spec='%s'\n",
            inputMappingControlName(target),
            SDL_GetScancodeName(scancode),
            g_inputMappingSettings ? g_inputMappingSettings->keyboardMapping.c_str() :
                inputCurrentKeyboardMapping().c_str());
    }
    else
    {
        inputMappingUiSetStatus(inputMappingChinese() ?
            L"\u952e\u76d8\u6620\u5c04\u5df2\u53d6\u6d88\u3002" : L"Keyboard mapping cancelled.");
    }
}

bool inputMappingUiKeyboardCapturePending(void)
{
    return g_keyboardMappingPending;
}

void inputMappingUiHandleKeyboardScancode(SDL_Scancode scancode)
{
    finishKeyboardMappingCapture(scancode);
}

static LRESULT CALLBACK inputMappingWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int keyboardIndex = inputMappingRowIndexForButton(id, kInputMappingIdKeyboardBase);
        if (keyboardIndex >= 0)
        {
            beginKeyboardMappingCapture(kInputMappingRows[keyboardIndex].controlBit);
            return 0;
        }
        int controllerIndex = inputMappingRowIndexForButton(id, kInputMappingIdControllerBase);
        if (controllerIndex >= 0)
        {
            if (g_inputMappingCallbacks.beginControllerMapping)
            {
                g_inputMappingCallbacks.beginControllerMapping(kInputMappingRows[controllerIndex].controlBit);
            }
            inputMappingUiRefresh();
            return 0;
        }
        if (id == kInputMappingIdResetKeyboard)
        {
            inputResetKeyboardMapping();
            saveKeyboardMappingFromRuntime();
            inputMappingUiRefresh();
            inputMappingUiSetStatus(inputMappingChinese() ?
                L"\u952e\u76d8\u6620\u5c04\u5df2\u6062\u590d\u9ed8\u8ba4\u3002" :
                L"Keyboard mapping restored to defaults.");
            return 0;
        }
        if (id == kInputMappingIdResetController)
        {
            if (g_inputMappingCallbacks.resetControllerMapping)
            {
                g_inputMappingCallbacks.resetControllerMapping();
            }
            inputMappingUiRefresh();
            inputMappingUiSetStatus(inputMappingChinese() ?
                L"\u624b\u67c4\u6620\u5c04\u5df2\u6062\u590d\u9ed8\u8ba4\u3002" :
                L"Controller mapping restored to defaults.");
            return 0;
        }
        if (id == kInputMappingIdClose)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        if (g_keyboardMappingPending)
        {
            finishKeyboardMappingCapture(win32VirtualKeyToScancode(wParam, lParam));
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
    {
        int id = GetDlgCtrlID((HWND)lParam);
        int rowCount = (int)(sizeof(kInputMappingRows) / sizeof(kInputMappingRows[0]));
        bool valueField =
            (id >= kInputMappingIdKeyboardTextBase && id < kInputMappingIdKeyboardTextBase + rowCount) ||
            (id >= kInputMappingIdControllerTextBase && id < kInputMappingIdControllerTextBase + rowCount);
        if (valueField || id == kInputMappingIdStatus)
        {
            SetBkMode((HDC)wParam, OPAQUE);
            if (valueField)
            {
                SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
                SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
                return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            }
            SetBkColor((HDC)wParam, GetSysColor(COLOR_BTNFACE));
            SetTextColor((HDC)wParam, GetSysColor(COLOR_BTNTEXT));
            return (LRESULT)(g_inputMappingBackgroundBrush ?
                g_inputMappingBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
        }
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)(g_inputMappingBackgroundBrush ?
            g_inputMappingBackgroundBrush : GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
        SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_inputMappingWindow == hwnd)
        {
            g_inputMappingWindow = NULL;
            g_keyboardMappingPending = false;
            g_keyboardMappingTarget = 0;
        }
        if (g_inputMappingFont && g_inputMappingOwnFont)
        {
            DeleteObject(g_inputMappingFont);
        }
        g_inputMappingFont = NULL;
        g_inputMappingOwnFont = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensureInputMappingWindowClass(void)
{
    static bool registered = false;
    if (registered)
    {
        return;
    }
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = inputMappingWindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"DingooPieInputMappingWindow";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = inputMappingLoadIcon(32);
    wc.hbrBackground = g_inputMappingBackgroundBrush;
    RegisterClassW(&wc);
    registered = true;
}

static HWND createInputMappingChild(HWND parent, const wchar_t* className, const wchar_t* text,
    DWORD exStyle, DWORD style, int x, int y, int w, int h, int id)
{
    DWORD childStyle = (DWORD)platformWin32NormalizeChildStyle(className, style);
    HWND child = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | childStyle,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (child)
    {
        SendMessageW(child, WM_SETFONT, (WPARAM)inputMappingFont(), TRUE);
        SetWindowTheme(child, L"Explorer", NULL);
    }
    return child;
}

void inputMappingUiOpenWindow(
    HWND owner,
    UiLanguage language,
    EmulatorSettings* settings,
    const InputMappingUiCallbacks& callbacks)
{
    (void)owner;
    g_inputMappingLanguage = language;
    g_inputMappingSettings = settings;
    g_inputMappingCallbacks = callbacks;

    if (g_inputMappingWindow)
    {
        ShowWindow(g_inputMappingWindow, SW_SHOWNOACTIVATE);
        inputMappingUiRefresh();
        return;
    }

    INITCOMMONCONTROLSEX controls;
    memset(&controls, 0, sizeof(controls));
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    bool zh = inputMappingChinese();
    g_inputMappingBackgroundBrush = GetSysColorBrush(COLOR_BTNFACE);
    ensureInputMappingWindowClass();
    int rowCount = (int)(sizeof(kInputMappingRows) / sizeof(kInputMappingRows[0]));
    const int controlX = 18;
    const int keyboardX = 150;
    const int keyboardWidth = 150;
    const int keyboardButtonX = 306;
    const int keyboardButtonWidth = 74;
    const int keyboardToControllerGap = 32;
    const int controllerX = keyboardButtonX + keyboardButtonWidth + keyboardToControllerGap;
    const int controllerWidth = 170;
    const int controllerButtonGap = 6;
    const int controllerButtonX = controllerX + controllerWidth + controllerButtonGap;
    const int controllerButtonWidth = 82;
    const int closeButtonX = 650;
    int width = 760;
    int height = 100 + rowCount * 34 + 54;
    g_inputMappingWindow = CreateWindowExW(WS_EX_CONTROLPARENT,
        L"DingooPieInputMappingWindow",
        zh ? L"\u6309\u952e\u6620\u5c04" : L"Input Mapping",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_inputMappingWindow)
    {
        return;
    }
    applyInputMappingIcon(g_inputMappingWindow);

    createInputMappingChild(g_inputMappingWindow, L"STATIC", zh ? L"\u63a7\u5236" : L"Control",
        0, 0, controlX, 16, 120, 20, -1);
    createInputMappingChild(g_inputMappingWindow, L"STATIC", zh ? L"\u952e\u76d8" : L"Keyboard",
        0, 0, keyboardX, 16, 160, 20, -1);
    createInputMappingChild(g_inputMappingWindow, L"STATIC", zh ? L"\u624b\u67c4 / \u6447\u6746" : L"Controller / Stick",
        0, 0, controllerX, 16, 180, 20, -1);

    for (int i = 0; i < rowCount; ++i)
    {
        int y = 44 + i * 34;
        createInputMappingChild(g_inputMappingWindow, L"STATIC", inputMappingRowLabel(kInputMappingRows[i]),
            0, 0, controlX, y + 4, 120, 22, -1);
        createInputMappingChild(g_inputMappingWindow, L"EDIT", L"",
            WS_EX_CLIENTEDGE, ES_READONLY | ES_AUTOHSCROLL,
            keyboardX, y, keyboardWidth, 24, kInputMappingIdKeyboardTextBase + i);
        createInputMappingChild(g_inputMappingWindow, L"BUTTON", zh ? L"\u8bbe\u7f6e\u952e\u76d8" : L"Set Key",
            0, 0, keyboardButtonX, y - 1, keyboardButtonWidth, 26, kInputMappingIdKeyboardBase + i);
        createInputMappingChild(g_inputMappingWindow, L"EDIT", L"",
            WS_EX_CLIENTEDGE, ES_READONLY | ES_AUTOHSCROLL,
            controllerX, y, controllerWidth, 24, kInputMappingIdControllerTextBase + i);
        createInputMappingChild(g_inputMappingWindow, L"BUTTON", zh ? L"\u8bbe\u7f6e\u624b\u67c4" : L"Set Gamepad",
            0, 0, controllerButtonX, y - 1, controllerButtonWidth, 26, kInputMappingIdControllerBase + i);
    }

    int bottomY = 48 + rowCount * 34;
    createInputMappingChild(g_inputMappingWindow, L"STATIC", L"",
        0, SS_ENDELLIPSIS | SS_NOPREFIX, 18, bottomY, 520, 24, kInputMappingIdStatus);
    createInputMappingChild(g_inputMappingWindow, L"BUTTON", zh ? L"\u6062\u590d\u952e\u76d8\u9ed8\u8ba4" : L"Reset Keyboard",
        0, 0, 18, bottomY + 30, 136, 28, kInputMappingIdResetKeyboard);
    createInputMappingChild(g_inputMappingWindow, L"BUTTON", zh ? L"\u6062\u590d\u624b\u67c4\u9ed8\u8ba4" : L"Reset Controller",
        0, 0, 162, bottomY + 30, 140, 28, kInputMappingIdResetController);
    createInputMappingChild(g_inputMappingWindow, L"BUTTON", zh ? L"\u5173\u95ed" : L"Close",
        0, 0, closeButtonX, bottomY + 30, 74, 28, kInputMappingIdClose);

    inputMappingUiRefresh();
    inputMappingUiSetStatus(zh ?
        L"\u70b9\u51fb\u8bbe\u7f6e\u952e\u76d8\u6216\u8bbe\u7f6e\u624b\u67c4\u540e\u6309\u4e0b\u76ee\u6807\u6309\u952e\u3002" :
        L"Choose Set Key or Set Gamepad, then press the target input.");
    ShowWindow(g_inputMappingWindow, SW_SHOWNOACTIVATE);
    UpdateWindow(g_inputMappingWindow);
}

#endif
