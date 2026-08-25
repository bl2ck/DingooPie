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
            L"\u4e01\u679c\u6e38\u620f (*.app;*.cc)\0*.app;*.cc\0\u6240\u6709\u6587\u4ef6 (*.*)\0*.*\0" :
            L"Dingoo Games (*.app;*.cc)\0*.app;*.cc\0All Files (*.*)\0*.*\0";
    case TXT_ERROR_LAUNCH_FAILED:
        return zh ? L"\u542f\u52a8\u6240\u9009\u6e38\u620f\u5931\u8d25\u3002" : L"Failed to launch the selected game.";
    case TXT_FILE_RECENT:
        return zh ? L"\u6700\u8fd1\u6e38\u620f(&N)" : L"Recent Games(&N)";
    case TXT_FILE_RECENT_EMPTY:
        return zh ? L"\u65e0\u6700\u8fd1\u6e38\u620f" : L"No recent games";
    case TXT_FILE_RECENT_CLEAR:
        return zh ? L"\u6e05\u9664\u6700\u8fd1\u6e38\u620f(&C)" : L"Clear Recent Games(&C)";
    case TXT_FILE_RESTART:
        return zh ? L"\u91cd\u542f\u6e38\u620f(&R)" : L"Restart Game(&R)";
    case TXT_CONFIRM_RESTART_GAME_TITLE:
        return zh ? L"\u91cd\u542f\u6e38\u620f" : L"Restart Game";
    case TXT_CONFIRM_RESTART_GAME_BODY:
        return zh ? L"\u91cd\u542f\u5f53\u524d\u6e38\u620f\uff1f\u672a\u4fdd\u5b58\u7684\u8fdb\u5ea6\u5c06\u4e22\u5931\u3002" :
            L"Restart the current game? Unsaved progress will be lost.";
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
    case TXT_FILE_SAVE_SLOT:
        return zh ? L"\u4fdd\u5b58\u5373\u65f6\u5b58\u6863(&T)" : L"Save State(&T)";
    case TXT_DIALOG_STATE_SAVE_TITLE:
        return zh ? L"\u4fdd\u5b58\u5b58\u6863" : L"Save State";
    case TXT_DIALOG_STATE_CONFIRM_SAVE:
        return zh ? L"\u4fdd\u5b58\u5230\u6b64\u6863\u4f4d\uff1f" : L"Save to this slot?";
    case TXT_DIALOG_STATE_CONFIRM_OVERWRITE:
        return zh ? L"\u6b64\u6863\u4f4d\u5df2\u6709\u5b58\u6863\uff0c\u662f\u5426\u8986\u76d6\uff1f" :
            L"This slot already contains a save. Overwrite it?";
    case TXT_DIALOG_STATE_COMPRESSING:
        return zh ? L"\u6b63\u5728\u538b\u7f29\u5b58\u6863\u2026" : L"Compressing state\u2026";
    case TXT_DIALOG_STATE_SAVED:
        return zh ? L"\u5b58\u6863\u5df2\u4fdd\u5b58\u3002" : L"State saved.";
    case TXT_DIALOG_STATE_SAVE_FAILED:
        return zh ? L"\u65e0\u6cd5\u4fdd\u5b58\u5b58\u6863\u3002" : L"Could not save state.";
    case TXT_FILE_LOAD_SLOT:
        return zh ? L"\u8bfb\u53d6\u5373\u65f6\u5b58\u6863(&L)" : L"Load State(&L)";
    case TXT_DIALOG_STATE_LOAD_TITLE:
        return zh ? L"\u8bfb\u53d6\u5b58\u6863" : L"Load State";
    case TXT_DIALOG_STATE_EMPTY:
        return zh ? L"\u6b64\u6863\u4f4d\u4e3a\u7a7a\u3002" : L"This slot is empty.";
    case TXT_DIALOG_STATE_CONFIRM_LOAD:
        return zh ? L"\u8bfb\u53d6\u6b64\u5b58\u6863\uff1f\u5f53\u524d\u8fdb\u5ea6\u5c06\u88ab\u8986\u76d6\u3002" :
            L"Load this state? Current progress will be overwritten.";
    case TXT_DIALOG_STATE_DECOMPRESSING:
        return zh ? L"\u6b63\u5728\u89e3\u538b\u5b58\u6863\u2026" : L"Decompressing state\u2026";
    case TXT_DIALOG_STATE_LOADED:
        return zh ? L"\u5b58\u6863\u5df2\u8bfb\u53d6\u3002" : L"State loaded.";
    case TXT_DIALOG_STATE_LOAD_FAILED:
        return zh ? L"\u65e0\u6cd5\u8bfb\u53d6\u5b58\u6863\u3002" : L"Could not load state.";
    case TXT_DIALOG_STATE_STAGE_MISMATCH:
        return zh ?
            L"\u5f53\u524d\u6e38\u620f\u8fdb\u5ea6\u4e0e\u5b58\u6863\u4e0d\u5339\u914d\u3002\n\u8bf7\u8fdb\u5165\u4fdd\u5b58\u65f6\u7684\u76f8\u540c\u573a\u666f\u540e\u91cd\u8bd5\u3002" :
            L"The current game state does not match this save. Return to the same scene and try again.";
    case TXT_FILE_SAVE_STATE_MANAGER:
        return zh ? L"\u5b58\u6863\u7ba1\u7406\u5668(&M)..." : L"Save Manager(&M)...";
    case TXT_FILE_EXIT:
        return zh ? L"\u9000\u51fa\u6a21\u62df\u5668(&X)" : L"Exit Emulator(&X)";
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
        return zh ? L"\u6ee4\u955c(&E)" : L"Filter(&E)";
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
    case TXT_VIDEO_SCREEN_ORIENTATION:
        return zh ? L"\u5c4f\u5e55\u65b9\u5411(&O)" : L"Screen Orientation(&O)";
    case TXT_VIDEO_SCREEN_ORIENTATION_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case TXT_VIDEO_SCREEN_ORIENTATION_LANDSCAPE:
        return zh ? L"\u6a2a\u5c4f" : L"Landscape";
    case TXT_VIDEO_SCREEN_ORIENTATION_PORTRAIT:
        return zh ? L"\u7ad6\u5c4f" : L"Portrait";
    case TXT_VIDEO_SCREEN_FILL:
        return zh ? L"\u753b\u9762\u586b\u5145(&F)" : L"Screen Fill(&F)";
    case TXT_VIDEO_SCREEN_FILL_ASPECT:
        return zh ? L"\u4fdd\u6301\u5bbd\u9ad8\u6bd4" : L"Keep Aspect Ratio";
    case TXT_VIDEO_SCREEN_FILL_BLURRED_EXTENSION:
        return zh ? L"\u6a21\u7cca\u5ef6\u5c55" : L"Blurred Extension";
    case TXT_VIDEO_SCREEN_FILL_STRETCH:
        return zh ? L"\u62c9\u4f38\u586b\u5145" : L"Stretch to Fill";
    case TXT_VIDEO_SHOW_FPS:
        return zh ? L"\u663e\u793a FPS(&F)" : L"Show FPS(&F)";
    case TXT_ROOT_AUDIO:
        return zh ? L"\u97f3\u9891(&A)" : L"Audio(&A)";
    case TXT_AUDIO_VOLUME:
        return zh ? L"\u4e3b\u97f3\u91cf(&V)" : L"Master Volume(&V)";
    case TXT_AUDIO_BUFFER:
        return zh ? L"\u97f3\u9891\u7f13\u51b2(&B)" : L"Audio Buffer(&B)";
    case TXT_AUDIO_BUFFER_LATENCY:
        return zh ? L"\u97f3\u9891\u7f13\u51b2\u5ef6\u8fdf(&L)" : L"Audio Buffer Latency(&L)";
    case TXT_AUDIO_BUFFER_LATENCY_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case TXT_AUDIO_BUFFER_LATENCY_110MS:
        return L"110 ms";
    case TXT_AUDIO_BUFFER_LATENCY_120MS:
        return L"120 ms";
    case TXT_AUDIO_BUFFER_LATENCY_130MS:
        return L"130 ms";
    case TXT_AUDIO_BUFFER_LATENCY_140MS:
        return L"140 ms";
    case TXT_AUDIO_BUFFER_LATENCY_150MS:
        return L"150 ms";
    case TXT_AUDIO_EFFECT:
        return zh ? L"\u97f3\u9891\u6548\u679c(&E)" : L"Audio Effect(&E)";
    case TXT_AUDIO_EFFECT_OFF:
        return zh ? L"\u5173\u95ed" : L"Off";
    case TXT_AUDIO_EFFECT_SOFT:
        return zh ? L"\u67d4\u548c" : L"Soft";
    case TXT_AUDIO_EFFECT_CLEAR:
        return zh ? L"\u6e05\u4eae" : L"Clear";
    case TXT_AUDIO_EFFECT_BASS_BOOST:
        return zh ? L"\u4f4e\u97f3\u589e\u5f3a" : L"Bass Boost";
    case TXT_AUDIO_EFFECT_MONO:
        return zh ? L"\u5355\u58f0\u9053" : L"Mono";
    case TXT_AUDIO_NOISE_REDUCTION:
        return zh ? L"\u6570\u5b57\u964d\u566a(&N)" : L"Digital Noise Reduction(&N)";
    case TXT_AUDIO_NOISE_REDUCTION_HIGH:
        return zh ? L"\u9ad8" : L"High";
    case TXT_AUDIO_NOISE_REDUCTION_MEDIUM:
        return zh ? L"\u4e2d" : L"Medium";
    case TXT_AUDIO_NOISE_REDUCTION_LOW:
        return zh ? L"\u4f4e" : L"Low";
    case TXT_AUDIO_DISABLE:
        return zh ? L"\u7981\u7528\u97f3\u9891(&A)" : L"Disable Audio(&A)";
    case TXT_ROOT_INPUT:
        return zh ? L"\u8f93\u5165(&I)" : L"Input(&I)";
    case TXT_INPUT_DISABLE_IME:
        return zh ? L"\u7981\u7528\u7cfb\u7edf\u8f93\u5165\u6cd5(&M)" : L"Disable System IME(&M)";
    case TXT_INPUT_VIRTUAL_CONTROLS:
        return zh ? L"\u663e\u793a\u865a\u62df\u6309\u952e(&V)" : L"Show Virtual Controls(&V)";
    case TXT_INPUT_VIRTUAL_CONTROL_SCALE:
        return zh ? L"\u865a\u62df\u6309\u952e\u5927\u5c0f(&S)" : L"Virtual Control Size(&S)";
    case TXT_INPUT_MAPPING_WINDOW:
        return zh ? L"\u6309\u952e\u624b\u67c4\u6620\u5c04(&K)..." : L"Input Mapping(&K)...";
    case TXT_ROOT_SETTINGS:
        return zh ? L"\u8bbe\u7f6e(&S)" : L"Settings(&S)";
    case TXT_SETTINGS_EXECUTION_MODE:
        return zh ? L"CPU \u6267\u884c\u6a21\u5f0f(&C)" : L"CPU Execution Mode(&C)";
    case TXT_SETTINGS_EXECUTION_MODE_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case TXT_SETTINGS_EXECUTION_MODE_COMPATIBILITY:
        return zh ? L"\u517c\u5bb9\u6a21\u5f0f" : L"Compatibility Mode";
    case TXT_SETTINGS_CPU_CLOCK:
        return zh ? L"CPU \u65f6\u949f(&H)" : L"CPU Clock(&H)";
    case TXT_SETTINGS_AUTO:
        return zh ? L"\u81ea\u52a8" : L"Auto";
    case TXT_SETTINGS_RUNTIME_SPEED:
        return zh ? L"\u6e38\u620f\u901f\u5ea6(&R)" : L"Game Speed(&R)";
    case TXT_SETTINGS_DELAY_SCALE:
        return zh ? L"\u7cfb\u7edf\u5ef6\u8fdf\u6bd4\u4f8b(&D)" : L"System Delay Scale(&D)";
    case TXT_SETTINGS_CHEATS:
        return zh ? L"\u91d1\u624b\u6307(&C)" : L"Cheats(&C)";
    case TXT_SETTINGS_ENABLE_CHEATS:
        return zh ? L"\u542f\u7528\u91d1\u624b\u6307(&T)" : L"Enable Cheats(&T)";
    case TXT_SETTINGS_CHEAT_MANAGER:
        return zh ? L"\u91d1\u624b\u6307\u7ba1\u7406\u5668(&M)..." : L"Cheat Manager(&M)...";
    case TXT_DIALOG_CHEATS_TITLE:
        return zh ? L"\u91d1\u624b\u6307\u7ba1\u7406\u5668" : L"Cheat Manager";
    case TXT_CHEATS_NO_FILE:
        return zh ? L"\u672a\u627e\u5230\u5339\u914d\u7684 .cht \u6587\u4ef6" : L"No matching .cht file";
    case TXT_CHEATS_SHA_MISMATCH:
        return zh ? L"\u4e0d\u9002\u7528\u4e8e\u5f53\u524d\u6e38\u620f" : L"Not for current game";
    case TXT_SETTINGS_LANGUAGE:
        return zh ? L"\u8bed\u8a00(&L)" : L"Language(&L)";
    case TXT_SETTINGS_RESET:
        return zh ? L"\u6062\u590d\u9ed8\u8ba4\u8bbe\u7f6e(&R)" : L"Restore Default Settings(&R)";
    case TXT_SETTINGS_RESET_TITLE:
        return zh ? L"\u6062\u590d\u9ed8\u8ba4\u8bbe\u7f6e" : L"Restore Default Settings";
    case TXT_SETTINGS_RESET_CONFIRM:
        return zh ? L"\u786e\u5b9a\u6062\u590d\u6240\u6709\u9ed8\u8ba4\u8bbe\u7f6e\u5417\uff1f" :
            L"Restore all settings to their defaults?";
    case TXT_SETTINGS_RESET_SUCCESS:
        return zh ? L"\u9ed8\u8ba4\u8bbe\u7f6e\u5df2\u6062\u590d\u3002" : L"Default settings restored.";
    case TXT_SETTINGS_RESET_SAVE_FAILED:
        return zh ? L"\u8bbe\u7f6e\u5df2\u6062\u590d\uff0c\u4f46\u65e0\u6cd5\u4fdd\u5b58\u3002" :
            L"Settings restored, but could not be saved.";
    case TXT_ROOT_DEBUG:
        return zh ? L"\u8c03\u8bd5\u5668(&D)" : L"Debugger(&D)";
    case TXT_DEBUG_CONSOLE:
        return zh ? L"\u663e\u793a\u8c03\u8bd5\u63a7\u5236\u53f0(&C)" : L"Show Debug Console(&C)";
    case TXT_DEBUG_PROFILE:
        return zh ? L"\u542f\u7528\u6027\u80fd\u65e5\u5fd7(&P)" : L"Enable Performance Log(&P)";
    case TXT_DEBUG_OPEN_LOG:
        return zh ? L"\u6253\u5f00\u8c03\u8bd5\u65e5\u5fd7(&L)" : L"Open Debug Log(&L)";
    case TXT_DEBUG_LOG_MISSING_TITLE:
        return zh ? L"\u65e5\u5fd7\u4e0d\u5b58\u5728" : L"Log Not Found";
    case TXT_DEBUG_LOG_MISSING_BODY:
        return zh ? L"\u5c1a\u672a\u751f\u6210 DingooPie-debug-*.log\u3002\u8fd0\u884c\u6e38\u620f\u6216\u542f\u7528\u8c03\u8bd5\u8f93\u51fa\u540e\u518d\u6253\u5f00\u3002" :
            L"DingooPie-debug-*.log has not been created yet. Run a game or enable debug output, then try again.";
    case TXT_DEBUG_RESOURCE_MONITOR:
        return zh ? L"\u8d44\u6e90\u76d1\u89c6\u5668(&R)..." : L"Resource Monitor(&R)...";
    case TXT_DEBUG_MEMORY_SEARCHER:
        return zh ? L"\u5185\u5b58\u641c\u7d22\u5668(&F)..." : L"Memory Searcher(&F)...";
    case TXT_DEBUG_DEBUGGER:
        return zh ? L"\u8c03\u8bd5\u5668(&G)..." : L"Debugger(&G)...";
    case TXT_ROOT_HELP:
        return zh ? L"\u5e2e\u52a9(&H)" : L"Help(&H)";
    case TXT_HELP_ABOUT:
        return zh ? L"\u5173\u4e8e(&A)" : L"About(&A)";
    case TXT_ABOUT_TITLE:
        return zh ? L"\u5173\u4e8e \u4e01\u679c\u6d3e DingooPie" : L"About DingooPie";
    case TXT_ABOUT_BODY:
        return zh ?
            L"\u4e01\u679c\u6d3e DingooPie PC " DINGOO_PIE_VERSION_TEXT_W L"\n"
            L"\u9002\u7528\u4e8e\u4e01\u679c A320\u3001\u6b4c\u7f8e X760+ \u548c\u6b4c\u7f8e A330 \u7684\u6e38\u620f\u6a21\u62df\u5668\n"
            L".app \u548c .cc \u683c\u5f0f\u5f52\u4e01\u679c\u79d1\u6280\u6240\u6709\u3002\n"
            L"\u7531 BL2CK Software \u63d0\u4f9b\u652f\u6301" :
            L"DingooPie PC " DINGOO_PIE_VERSION_TEXT_W L"\n"
            L"Game emulator for Dingoo A320, Gemei X760+, and Gemei A330\n"
            L"The .app and .cc formats are owned by Dingoo Technology.\n"
            L"Powered by BL2CK Software";
    default:
        return L"";
    }
}
