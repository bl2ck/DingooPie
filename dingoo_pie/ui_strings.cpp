#include "ui_strings.h"

#include "app_metadata.h"

const wchar_t* uiText(UiLanguage language, UiTextId id)
{
    bool zh = language == UI_LANGUAGE_CHINESE;
    switch (id)
    {
    case TXT_ROOT_FILE:
        return zh ? L"\u6587\u4ef6(&F)" : L"File(&F)";
    case TXT_FILE_OPEN:
        return zh ? L"\u6253\u5f00\u6e38\u620f(&O)..." : L"Open Game(&O)...";
    case TXT_DIALOG_APP_TITLE:
        return zh ? L"\u6253\u5f00\u6e38\u620f" : L"Open Game";
    case TXT_DIALOG_APP_FILTER:
        return zh ?
            L"\u4e01\u679c App (*.app)\0*.app\0\u6240\u6709\u6587\u4ef6 (*.*)\0*.*\0" :
            L"Dingoo App (*.app)\0*.app\0All Files (*.*)\0*.*\0";
    case TXT_ERROR_LAUNCH_FAILED:
        return zh ? L"\u542f\u52a8\u6240\u9009 .app \u5931\u8d25\u3002" : L"Failed to launch the selected .app.";
    case TXT_FILE_RECENT:
        return zh ? L"\u6700\u8fd1\u6e38\u620f(&N)" : L"Recent Games(&N)";
    case TXT_FILE_RECENT_EMPTY:
        return zh ? L"\u65e0\u6700\u8fd1\u6e38\u620f" : L"No recent games";
    case TXT_FILE_RECENT_CLEAR:
        return zh ? L"\u6e05\u9664\u6700\u8fd1\u6e38\u620f(&C)" : L"Clear Recent Games(&C)";
    case TXT_FILE_RESTART:
        return zh ? L"\u91cd\u542f\u6e38\u620f(&R)" : L"Restart Game(&R)";
    case TXT_FILE_PAUSE:
        return zh ? L"\u6682\u505c\u6e38\u620f(&P)" : L"Pause Game(&P)";
    case TXT_FILE_RESUME:
        return zh ? L"\u6062\u590d\u6e38\u620f(&P)" : L"Resume Game(&P)";
    case TXT_FILE_SAVE_SCREENSHOT:
        return zh ? L"\u4fdd\u5b58\u622a\u56fe(&S)..." : L"Save Screenshot(&S)...";
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
    case TXT_FILE_SAVE_STATE:
        return zh ? L"\u5373\u65f6\u5b58\u6863(&T)" : L"Save State(&T)";
    case TXT_DIALOG_STATE_SAVE_TITLE:
        return zh ? L"\u5373\u65f6\u5b58\u6863" : L"Save State";
    case TXT_DIALOG_STATE_CONFIRM_SAVE:
        return zh ? L"\u662f\u5426\u4fdd\u5b58\u5230\u8be5\u5373\u65f6\u5b58\u6863\u6863\u4f4d\uff1f" : L"Save to this state slot?";
    case TXT_DIALOG_STATE_CONFIRM_OVERWRITE:
        return zh ? L"\u8be5\u6863\u4f4d\u5df2\u6709\u5b58\u6863\uff0c\u662f\u5426\u8986\u76d6\uff1f" : L"This state slot already has a save. Overwrite it?";
    case TXT_DIALOG_STATE_COMPRESSING:
        return zh ? L"\u6b63\u5728\u538b\u7f29\u5373\u65f6\u5b58\u6863..." : L"Compressing save state...";
    case TXT_DIALOG_STATE_SAVED:
        return zh ? L"\u5373\u65f6\u5b58\u6863\u5df2\u4fdd\u5b58\u3002" : L"Save state saved.";
    case TXT_DIALOG_STATE_SAVE_FAILED:
        return zh ? L"\u4fdd\u5b58\u5373\u65f6\u5b58\u6863\u5931\u8d25\u3002" : L"Failed to save state.";
    case TXT_FILE_LOAD_STATE:
        return zh ? L"\u8bfb\u53d6\u5b58\u6863(&L)" : L"Load State(&L)";
    case TXT_DIALOG_STATE_LOAD_TITLE:
        return zh ? L"\u8bfb\u53d6\u5b58\u6863" : L"Load State";
    case TXT_DIALOG_STATE_EMPTY:
        return zh ? L"\u8be5\u6863\u4f4d\u8fd8\u6ca1\u6709\u5b58\u6863\u3002" : L"This state slot is empty.";
    case TXT_DIALOG_STATE_CONFIRM_LOAD:
        return zh ? L"\u662f\u5426\u8bfb\u53d6\u8be5\u5373\u65f6\u5b58\u6863\uff1f\u5f53\u524d\u8fdb\u5ea6\u4f1a\u88ab\u66ff\u6362\u3002" :
            L"Load this state? Current progress will be replaced.";
    case TXT_DIALOG_STATE_DECOMPRESSING:
        return zh ? L"\u6b63\u5728\u89e3\u538b\u5373\u65f6\u5b58\u6863..." : L"Decompressing save state...";
    case TXT_DIALOG_STATE_LOADED:
        return zh ? L"\u5373\u65f6\u5b58\u6863\u5df2\u8bfb\u53d6\u3002" : L"Save state loaded.";
    case TXT_DIALOG_STATE_LOAD_FAILED:
        return zh ? L"\u8bfb\u53d6\u5373\u65f6\u5b58\u6863\u5931\u8d25\u3002" : L"Failed to load state.";
    case TXT_DIALOG_STATE_STAGE_MISMATCH:
        return zh ?
            L"\u5f53\u524d\u6e38\u620f\u9636\u6bb5\u4e0e\u5b58\u6863\u4e0d\u4e00\u81f4\u3002\u8bf7\u5148\u8fdb\u5165\u4e0e\u5b58\u6863\u76f8\u540c\u7684\u573a\u666f\uff0c\u518d\u8bfb\u53d6\u8be5\u6863\u4f4d\u3002" :
            L"The current game stage does not match this state. Enter the same scene as the saved state, then load this slot again.";
    case TXT_FILE_EXIT:
        return zh ? L"\u9000\u51fa(&X)" : L"Exit(&X)";
    case TXT_CONFIRM_EXIT_TITLE:
        return zh ? L"\u9000\u51fa\u6a21\u62df\u5668" : L"Exit Emulator";
    case TXT_CONFIRM_EXIT_BODY:
        return zh ? L"\u662f\u5426\u9000\u51fa\u6a21\u62df\u5668\uff1f" : L"Exit the emulator?";
    case TXT_ROOT_OPTIONS:
        return zh ? L"\u9009\u9879(&O)" : L"Options(&O)";
    case TXT_ROOT_VIDEO:
        return zh ? L"\u89c6\u9891(&V)" : L"Video(&V)";
    case TXT_VIDEO_SCALE:
        return zh ? L"\u7f29\u653e(&S)" : L"Scale(&S)";
    case TXT_VIDEO_FULLSCREEN:
        return zh ? L"\u5168\u5c4f(&M)" : L"Fullscreen(&M)";
    case TXT_VIDEO_ANTI_ALIASING:
        return zh ? L"\u6297\u952f\u9f7f(&A)" : L"Anti-aliasing(&A)";
    case TXT_VIDEO_AA_OFF:
        return zh ? L"\u5173\u95ed" : L"Off";
    case TXT_VIDEO_AA_LOW:
        return zh ? L"\u8f7b\u5ea6" : L"Low";
    case TXT_VIDEO_AA_CLEAR:
        return zh ? L"\u6e05\u6670" : L"Clear";
    case TXT_VIDEO_EFFECT:
        return zh ? L"\u6ee4\u955c(&E)" : L"Effect(&E)";
    case TXT_VIDEO_EFFECT_NORMAL:
        return zh ? L"\u6b63\u5e38" : L"Normal";
    case TXT_VIDEO_EFFECT_GRAYSCALE:
        return zh ? L"\u9ed1\u767d" : L"Black && White";
    case TXT_VIDEO_EFFECT_INVERT:
        return zh ? L"\u53cd\u8272" : L"Invert";
    case TXT_VIDEO_EFFECT_SOFT_BLUR:
        return zh ? L"\u67d4\u5316" : L"Soft Blur";
    case TXT_VIDEO_EFFECT_SHARPEN:
        return zh ? L"\u9510\u5316" : L"Sharpen";
    case TXT_VIDEO_EFFECT_VIVID:
        return zh ? L"\u8272\u5f69\u589e\u5f3a" : L"Vivid";
    case TXT_VIDEO_EFFECT_SEPIA:
        return zh ? L"\u6000\u65e7\u8910\u8272" : L"Sepia";
    case TXT_VIDEO_EFFECT_PIXEL_GRID:
        return zh ? L"\u50cf\u7d20\u7f51\u683c" : L"Pixel Grid";
    case TXT_VIDEO_EFFECT_LCD_SCANLINE:
        return zh ? L"LCD \u626b\u63cf\u7ebf" : L"LCD Scanline";
    case TXT_VIDEO_EFFECT_LIGHT_CRT:
        return zh ? L"\u8f7b\u91cf CRT" : L"Light CRT";
    case TXT_VIDEO_BRIGHTNESS:
        return zh ? L"\u4eae\u5ea6(&B)" : L"Brightness(&B)";
    case TXT_VIDEO_CONTRAST:
        return zh ? L"\u5bf9\u6bd4\u5ea6(&C)" : L"Contrast(&C)";
    case TXT_VIDEO_GAMMA:
        return zh ? L"\u4f3d\u9a6c(&G)" : L"Gamma(&G)";
    case TXT_VIDEO_SATURATION:
        return zh ? L"\u9971\u548c\u5ea6(&A)" : L"Saturation(&A)";
    case TXT_VIDEO_MINIMIZED_BEHAVIOR:
        return zh ? L"\u6700\u5c0f\u5316\u65f6(&M)" : L"When Minimized(&M)";
    case TXT_VIDEO_MINIMIZED_NORMAL:
        return zh ? L"\u6b63\u5e38\u8fd0\u884c" : L"Run Normally";
    case TXT_VIDEO_MINIMIZED_PAUSE:
        return zh ? L"\u81ea\u52a8\u6682\u505c" : L"Auto Pause";
    case TXT_VIDEO_MINIMIZED_THROTTLE:
        return zh ? L"\u964d\u4f4e\u5e27\u7387" : L"Throttle Frame Rate";
    case TXT_VIDEO_PORTRAIT:
        return zh ? L"\u7ad6\u5c4f\u6a21\u5f0f(&P)" : L"Portrait Mode(&P)";
    case TXT_VIDEO_SHOW_FPS:
        return zh ? L"\u663e\u793a FPS(&F)" : L"Show FPS(&F)";
    case TXT_ROOT_AUDIO:
        return zh ? L"\u97f3\u9891(&A)" : L"Audio(&A)";
    case TXT_SETTINGS_AUDIO_VOLUME:
        return zh ? L"\u4e3b\u97f3\u91cf(&V)" : L"Master Volume(&V)";
    case TXT_SETTINGS_AUDIO_BUFFER:
        return zh ? L"\u97f3\u9891\u7f13\u51b2(&B)" : L"Audio Buffer(&B)";
    case TXT_SETTINGS_DROP_AUDIO:
        return zh ? L"\u7981\u7528\u97f3\u9891(&A)" : L"Disable Audio(&A)";
    case TXT_ROOT_INPUT:
        return zh ? L"\u8f93\u5165(&I)" : L"Input(&I)";
    case TXT_INPUT_DISABLE_IME:
        return zh ? L"\u7981\u7528\u8f93\u5165\u6cd5(&M)" : L"Disable IME(&M)";
    case TXT_INPUT_VIRTUAL_CONTROLS:
        return zh ? L"\u663e\u793a\u865a\u62df\u6309\u952e(&V)" : L"Show Virtual Controls(&V)";
    case TXT_INPUT_MAPPING_WINDOW:
        return zh ? L"\u6309\u952e\u6620\u5c04(&K)..." : L"Input Mapping(&K)...";
    case TXT_ROOT_SETTINGS:
        return zh ? L"\u8bbe\u7f6e(&S)" : L"Settings(&S)";
    case TXT_SETTINGS_CPU_BACKEND:
        return zh ? L"CPU \u540e\u7aef(&C)" : L"CPU Backend(&C)";
    case TXT_SETTINGS_BACKEND_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case TXT_SETTINGS_BACKEND_IRJIT:
        return L"PPSSPP IR JIT";
    case TXT_SETTINGS_BACKEND_INTERPRETER:
        return L"Interpreter";
    case TXT_SETTINGS_CPU_CLOCK:
        return zh ? L"CPU \u65f6\u949f(&H)" : L"CPU Clock(&H)";
    case TXT_SETTINGS_SPEED_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case TXT_SETTINGS_RUNTIME_SPEED:
        return zh ? L"\u8fd0\u884c\u901f\u5ea6(&R)" : L"Runtime Speed(&R)";
    case TXT_SETTINGS_DELAY:
        return zh ? L"\u5ef6\u8fdf\u6bd4\u4f8b(&D)" : L"Delay Scale(&D)";
    case TXT_SETTINGS_DELAY_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case TXT_SETTINGS_ENABLE_CHEATS:
        return zh ? L"\u542f\u7528\u91d1\u624b\u6307(&T)" : L"Enable Cheats(&T)";
    case TXT_SETTINGS_CHEAT_LIST:
        return zh ? L"\u91d1\u624b\u6307(&C)" : L"Cheats(&C)";
    case TXT_DIALOG_CHEATS_TITLE:
        return zh ? L"\u91d1\u624b\u6307" : L"Cheats";
    case TXT_CHEATS_NO_FILE:
        return zh ? L"\u65e0\u6e38\u620f\u540c\u540d .cht \u6587\u4ef6" : L"No same-name .cht file";
    case TXT_CHEATS_SHA_MISMATCH:
        return zh ? L"\u91d1\u624b\u6307\u6587\u4ef6\u4e0d\u5c5e\u4e8e\u5f53\u524d\u6e38\u620f" : L"Cheat file does not match the current game";
    case TXT_SETTINGS_LANGUAGE:
        return zh ? L"\u8bed\u8a00(&L)" : L"Language(&L)";
    case TXT_SETTINGS_RESET:
        return zh ? L"\u6062\u590d\u9ed8\u8ba4\u8bbe\u7f6e(&R)" : L"Restore Default Settings(&R)";
    case TXT_ROOT_DEBUG:
        return zh ? L"\u8c03\u8bd5(&D)" : L"Debug(&D)";
    case TXT_DEBUG_CONSOLE:
        return zh ? L"\u663e\u793a\u8c03\u8bd5\u63a7\u5236\u53f0(&C)" : L"Show Debug Console(&C)";
    case TXT_DEBUG_PROFILE:
        return zh ? L"\u542f\u7528\u6027\u80fd\u65e5\u5fd7(&P)" : L"Enable Performance Log(&P)";
    case TXT_DEBUG_OPEN_LOG:
        return zh ? L"\u6253\u5f00\u8c03\u8bd5\u65e5\u5fd7(&L)" : L"Open Debug Log(&L)";
    case TXT_DEBUG_CHEAT_FINDER:
        return zh ? L"\u5185\u5b58\u641c\u7d22\u5668(&F)..." : L"Cheat Finder(&F)...";
    case TXT_DEBUG_DEBUGGER:
        return zh ? L"\u8c03\u8bd5\u5668(&G)..." : L"Debugger(&G)...";
    case TXT_DEBUG_LOG_MISSING_TITLE:
        return zh ? L"\u65e5\u5fd7\u4e0d\u5b58\u5728" : L"Log Not Found";
    case TXT_DEBUG_LOG_MISSING_BODY:
        return zh ? L"\u5c1a\u672a\u751f\u6210 DingooPie-debug.log\u3002\u8fd0\u884c\u6e38\u620f\u6216\u542f\u7528\u8c03\u8bd5\u8f93\u51fa\u540e\u518d\u6253\u5f00\u3002" :
            L"DingooPie-debug.log has not been created yet. Run a game or enable debug output, then try again.";
    case TXT_ROOT_HELP:
        return zh ? L"\u5e2e\u52a9(&H)" : L"Help(&H)";
    case TXT_HELP_ABOUT:
        return zh ? L"\u5173\u4e8e(&A)" : L"About(&A)";
    case TXT_ABOUT_TITLE:
        return zh ? L"\u5173\u4e8e \u4e01\u679c\u6d3e DingooPie" : L"About DingooPie";
    case TXT_ABOUT_BODY:
        return zh ?
            L"\u4e01\u679c\u6d3e DingooPie \u7248\u672c " DINGOO_PIE_VERSION_TEXT_W L"\n"
            L"\u4e01\u679c A320 / \u6b4c\u7f8e X760+ \u6e38\u620f\u6a21\u62df\u5668\n"
            L".app \u683c\u5f0f\u6587\u4ef6\u5f52\u4e01\u679c\u79d1\u6280\u6240\u6709\u3002\n\n"
            L"Powered by BL2CK Software" :
            L"DingooPie Version " DINGOO_PIE_VERSION_TEXT_W L"\n"
            L"Dingoo A320 / Gemei X760+ game emulator\n"
            L"The .app package format belongs to Dingoo Technology.\n\n"
            L"Powered by BL2CK Software";
    default:
        return L"";
    }
}
