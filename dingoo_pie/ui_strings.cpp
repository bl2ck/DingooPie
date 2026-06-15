#include "ui_strings.h"

const wchar_t* uiText(UiLanguage language, UiTextId id)
{
    bool zh = language == UI_LANGUAGE_CHINESE;
    switch (id)
    {
    case TXT_ERROR_LAUNCH_FAILED:
        return zh ? L"\u542f\u52a8\u6240\u9009 .app \u5931\u8d25\u3002" : L"Failed to launch the selected .app.";
    case TXT_DIALOG_SAVE_TITLE:
        return zh ? L"\u4fdd\u5b58\u622a\u56fe" : L"Save Screenshot";
    case TXT_DIALOG_SAVE_FILTER:
        return zh ?
            L"PNG \u56fe\u7247 (*.png)\0*.png\0JPEG \u56fe\u7247 (*.jpg)\0*.jpg;*.jpeg\0\u4f4d\u56fe (*.bmp)\0*.bmp\0" :
            L"PNG Image (*.png)\0*.png\0JPEG Image (*.jpg)\0*.jpg;*.jpeg\0Bitmap (*.bmp)\0*.bmp\0";
    case TXT_DIALOG_SCREENSHOT_SAVED:
        return zh ? L"\u622a\u56fe\u5df2\u4fdd\u5b58\u3002" : L"Screenshot saved.";
    case TXT_DIALOG_SCREENSHOT_FAILED:
        return zh ? L"\u4fdd\u5b58\u622a\u56fe\u5931\u8d25\u3002\u8bf7\u786e\u8ba4\u6e38\u620f\u753b\u9762\u5df2\u663e\u793a\u4e14\u8def\u5f84\u53ef\u5199\u3002" :
            L"Failed to save screenshot. Make sure a game frame is visible and the path is writable.";
    case TXT_DIALOG_APP_FILTER:
        return zh ?
            L"\u4e01\u679c App (*.app)\0*.app\0\u6240\u6709\u6587\u4ef6 (*.*)\0*.*\0" :
            L"Dingoo App (*.app)\0*.app\0All Files (*.*)\0*.*\0";
    case TXT_DIALOG_APP_TITLE:
        return zh ? L"\u6253\u5f00\u4e01\u679c .app" : L"Open Dingoo .app";
    case TXT_FILE_OPEN:
        return zh ? L"\u6253\u5f00\u6e38\u620f .app(&O)..." : L"Open Game .app(&O)...";
    case TXT_FILE_RESTART:
        return zh ? L"\u91cd\u542f\u6e38\u620f(&R)" : L"Restart Game(&R)";
    case TXT_FILE_SAVE_SCREENSHOT:
        return zh ? L"\u4fdd\u5b58\u622a\u56fe(&S)..." : L"Save Screenshot(&S)...";
    case TXT_FILE_EXIT:
        return zh ? L"\u9000\u51fa(&X)" : L"Exit(&X)";
    case TXT_VIDEO_SCALE:
        return zh ? L"\u7f29\u653e(&S)" : L"Scale(&S)";
    case TXT_VIDEO_FULLSCREEN:
        return zh ? L"\u5168\u5c4f(&M)" : L"Fullscreen(&M)";
    case TXT_VIDEO_FILTER:
        return zh ? L"\u91c7\u6837\u65b9\u5f0f(&F)" : L"Sampling(&F)";
    case TXT_VIDEO_EFFECT:
        return zh ? L"\u6ee4\u955c(&E)" : L"Effect(&E)";
    case TXT_VIDEO_BRIGHTNESS:
        return zh ? L"\u4eae\u5ea6(&B)" : L"Brightness(&B)";
    case TXT_VIDEO_CONTRAST:
        return zh ? L"\u5bf9\u6bd4\u5ea6(&C)" : L"Contrast(&C)";
    case TXT_VIDEO_SATURATION:
        return zh ? L"\u9971\u548c\u5ea6(&A)" : L"Saturation(&A)";
    case TXT_VIDEO_SHOW_FPS:
        return zh ? L"\u663e\u793a FPS(&F)" : L"Show FPS(&F)";
    case TXT_VIDEO_PORTRAIT:
        return zh ? L"\u7ad6\u5c4f\u6a21\u5f0f(&P)" : L"Portrait Mode(&P)";
    case TXT_VIDEO_FILTER_NEAREST:
        return zh ? L"\u6700\u8fd1\u90bb" : L"Nearest";
    case TXT_VIDEO_FILTER_LINEAR:
        return zh ? L"\u7ebf\u6027" : L"Linear";
    case TXT_VIDEO_EFFECT_NORMAL:
        return zh ? L"\u6b63\u5e38" : L"Normal";
    case TXT_VIDEO_EFFECT_GRAYSCALE:
        return zh ? L"\u9ed1\u767d" : L"Black && White";
    case TXT_VIDEO_EFFECT_INVERT:
        return zh ? L"\u53cd\u8272" : L"Invert";
    case TXT_VIDEO_EFFECT_INVERT_GRAYSCALE:
        return zh ? L"\u53cd\u8272\u9ed1\u767d" : L"Inverted B&&W";
    case TXT_VIDEO_EFFECT_SEPIA:
        return zh ? L"\u6000\u65e7\u8910\u8272" : L"Sepia";
    case TXT_VIDEO_EFFECT_AMBER:
        return zh ? L"\u7425\u73c0\u5c4f" : L"Amber";
    case TXT_VIDEO_EFFECT_SHARPEN:
        return zh ? L"\u9510\u5316" : L"Sharpen";
    case TXT_VIDEO_EFFECT_SOFT_BLUR:
        return zh ? L"\u67d4\u5316" : L"Soft Blur";
    case TXT_VIDEO_EFFECT_LCD_SCANLINE:
        return zh ? L"LCD \u626b\u63cf\u7ebf" : L"LCD Scanline";
    case TXT_SETTINGS_CPU_BACKEND:
        return zh ? L"CPU \u540e\u7aef(&C)" : L"CPU Backend(&C)";
    case TXT_SETTINGS_CPU_CLOCK:
        return zh ? L"CPU \u65f6\u949f(&H)" : L"CPU Clock(&H)";
    case TXT_SETTINGS_RUNTIME_SPEED:
        return zh ? L"\u8fd0\u884c\u901f\u5ea6(&R)" : L"Runtime Speed(&R)";
    case TXT_SETTINGS_DELAY:
        return zh ? L"SDK \u5ef6\u8fdf\u6bd4\u4f8b(&D)" : L"SDK Delay Scale(&D)";
    case TXT_SETTINGS_BACKEND_IRJIT:
        return L"PPSSPP IR JIT";
    case TXT_SETTINGS_BACKEND_INTERPRETER:
        return zh ? L"\u89e3\u91ca\u5668" : L"Interpreter";
    case TXT_SETTINGS_BACKEND_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case TXT_SETTINGS_SPEED_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case TXT_SETTINGS_DELAY_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case TXT_SETTINGS_AUDIO_VOLUME:
        return zh ? L"\u4e3b\u97f3\u91cf(&V)" : L"Master Volume(&V)";
    case TXT_SETTINGS_AUDIO_BUFFER:
        return zh ? L"\u97f3\u9891\u7f13\u51b2(&B)" : L"Audio Buffer(&B)";
    case TXT_SETTINGS_DROP_AUDIO:
        return zh ? L"\u7981\u7528\u97f3\u9891(&A)" : L"Disable Audio(&A)";
    case TXT_SETTINGS_SAVE:
        return zh ? L"\u4fdd\u5b58\u8bbe\u7f6e(&S)" : L"Save Settings(&S)";
    case TXT_SETTINGS_RESET:
        return zh ? L"\u6062\u590d\u9ed8\u8ba4\u8bbe\u7f6e(&R)" : L"Restore Default Settings(&R)";
    case TXT_SETTINGS_LANGUAGE:
        return zh ? L"\u8bed\u8a00(&L)" : L"Language(&L)";
    case TXT_INPUT_VIRTUAL_CONTROLS:
        return zh ? L"\u663e\u793a\u865a\u62df\u6309\u952e(&V)" : L"Show Virtual Controls(&V)";
    case TXT_INPUT_DISABLE_IME:
        return zh ? L"\u7981\u7528\u8f93\u5165\u6cd5(&M)" : L"Disable IME(&M)";
    case TXT_DEBUG_CONSOLE:
        return zh ? L"\u663e\u793a\u8c03\u8bd5\u63a7\u5236\u53f0(&C)" : L"Show Debug Console(&C)";
    case TXT_DEBUG_PROFILE:
        return zh ? L"\u542f\u7528\u6027\u80fd\u65e5\u5fd7(&P)" : L"Enable Profile Log(&P)";
    case TXT_DEBUG_OPEN_LOG:
        return zh ? L"\u6253\u5f00\u8c03\u8bd5\u65e5\u5fd7(&L)" : L"Open Debug Log(&L)";
    case TXT_DEBUG_LOG_MISSING_TITLE:
        return zh ? L"\u65e5\u5fd7\u4e0d\u5b58\u5728" : L"Log Not Found";
    case TXT_DEBUG_LOG_MISSING_BODY:
        return zh ? L"\u5c1a\u672a\u751f\u6210 DingooPie-debug.log\u3002\u8fd0\u884c\u6e38\u620f\u6216\u542f\u7528\u8c03\u8bd5\u8f93\u51fa\u540e\u518d\u6253\u5f00\u3002" :
            L"DingooPie-debug.log has not been created yet. Run a game or enable debug output, then try again.";
    case TXT_HELP_ABOUT:
        return zh ? L"\u5173\u4e8e(&A)" : L"About(&A)";
    case TXT_ROOT_FILE:
        return zh ? L"\u6587\u4ef6(&F)" : L"File(&F)";
    case TXT_ROOT_OPTIONS:
        return zh ? L"\u9009\u9879(&O)" : L"Options(&O)";
    case TXT_ROOT_VIDEO:
        return zh ? L"\u89c6\u9891(&V)" : L"Video(&V)";
    case TXT_ROOT_AUDIO:
        return zh ? L"\u97f3\u9891(&A)" : L"Audio(&A)";
    case TXT_ROOT_INPUT:
        return zh ? L"\u8f93\u5165(&I)" : L"Input(&I)";
    case TXT_ROOT_SETTINGS:
        return zh ? L"\u8bbe\u7f6e(&S)" : L"Settings(&S)";
    case TXT_ROOT_DEBUG:
        return zh ? L"\u8c03\u8bd5(&D)" : L"Debug(&D)";
    case TXT_ROOT_HELP:
        return zh ? L"\u5e2e\u52a9(&H)" : L"Help(&H)";
    case TXT_CONFIRM_EXIT_TITLE:
        return zh ? L"\u9000\u51fa\u6a21\u62df\u5668" : L"Exit Emulator";
    case TXT_CONFIRM_EXIT_BODY:
        return zh ? L"\u662f\u5426\u9000\u51fa\u6a21\u62df\u5668\uff1f" : L"Exit the emulator?";
    case TXT_ABOUT_TITLE:
        return zh ? L"\u5173\u4e8e DingooPie" : L"About DingooPie";
    case TXT_ABOUT_BODY:
        return zh ?
            L"DingooPie\n"
            L"\u4e01\u679c A320 / \u6b4c\u7f8e X760+ \u6e38\u620f\u6a21\u62df\u5668\n"
            L"\u7248\u672c 1.0\n"
            L"Powered by BL2CK\n"
            L"Copyright \u00a9 BL2CK 2026\n\n"
            L".app \u6587\u4ef6\u683c\u5f0f\u5f52\u4e01\u679c\u79d1\u6280\u6240\u5c5e\u3002\n\n"
            L"\u952e\u76d8\uff1aWASD/\u65b9\u5411\u952e = \u65b9\u5411\u952e\uff0cI/K/J/L = X/B/Y/A\uff0c1/Q = SELECT\uff0c0/O = START\uff0c"
            L"\u5de6 Shift = \u5de6\u80a9\u952e\uff0c\u53f3 Shift = \u53f3\u80a9\u952e\uff0cEsc = \u9000\u51fa\u6a21\u62df\u5668\uff0cF12 = \u622a\u56fe\u3002" :
            L"DingooPie\n"
            L"Dingoo A320 / Gemei X760+ Game Emulator\n"
            L"Version 1.0\n"
            L"Powered by BL2CK\n"
            L"Copyright \u00a9 BL2CK 2026\n\n"
            L"The .app package format belongs to Dingoo Technology.\n\n"
            L"Keyboard: WASD/Arrows = D-pad, I/K/J/L = X/B/Y/A, 1/Q = SELECT, 0/O = START, "
            L"Left Shift = L shoulder, Right Shift = R shoulder, Esc = exit emulator, F12 = screenshot.";
    default:
        return L"";
    }
}
