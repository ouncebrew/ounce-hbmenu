#include <switch.h>
#include <string.h>
#include <stdio.h>
#include <physfs.h>

#include "../common/common.h"
#include "nx_graphics.h"
#include "nx_touch.h"
#ifdef __SWITCH2__
#include "../switch2/switch2_ui.h"
#include "../switch2/switch2_sfx.h"
#endif

#ifdef __SWITCH2__
#include <switch2/runtime/dynlink.h>

// The string that explains a program's load result, or StrId_Max for a
// result without one. The loader writes the details, such as a missing
// library's name, to sdmc:/kaihen/nro-cleanup.txt.
static StrId loadResultExplanation(Result result) {
    switch (result) {
    case DYNLINK_RESULT_NOT_LINKED:         return StrId_LoadResult_NotLinked;
    case DYNLINK_RESULT_LIBRARY_NOT_FOUND:  return StrId_LoadResult_LibraryNotFound;
    case DYNLINK_RESULT_LIBRARY_NOT_LOADED: return StrId_LoadResult_LibraryNotLoaded;
    case DYNLINK_RESULT_UNDEFINED_SYMBOL:   return StrId_LoadResult_UndefinedSymbol;
    case DYNLINK_RESULT_BAD_RELOCATION:     return StrId_LoadResult_BadRelocation;
    case DYNLINK_RESULT_PRELOAD_NOT_LOADED: return StrId_LoadResult_PreloadNotLoaded;
    case DYNLINK_RESULT_NO_RUNTIME_LIBRARY: return StrId_LoadResult_NoRuntimeLibrary;
    default:                                return StrId_Max;
    }
}
#endif

// Define the desired framebuffer resolution (here we set it to 720p).
#ifdef __SWITCH2__
// Switch 2 draws at the window's size, which follows the console's default
// resolution (switch2/ouncevk_platform.c); graphicsInit does not use these.
#define FB_WIDTH  1920
#define FB_HEIGHT 1080
#else
#define FB_WIDTH  1280
#define FB_HEIGHT 720
#endif

uint8_t* g_framebuf;
u32 g_framebuf_width;

PadState g_pad;
PadRepeater g_pad_repeater;

bool menuUpdateErrorScreen(void);

#ifdef PERF_LOG
u64 g_tickdiff_frame=0;
#endif

#ifdef ENABLE_AUDIO
void audio_initialize(void);
void audio_exit(void);
#endif

extern u32 __nx_applet_exit_mode;

int main(int argc, char **argv)
{
    /* Fast-path Auto-Donor Pivot handoff: only chain when /kaihen/autolaunch.txt
       starts with "once:" so stale or unrelated files never hijack HBMenu. */
    if (envHasNextLoad() && fileExists("sdmc:/kaihen/autolaunch.txt")) {
        char head[8] = {0};
        FILE *af = fopen("sdmc:/kaihen/autolaunch.txt", "rb");
        if (af) {
            fread(head, 1, 5, af);
            fclose(af);
        }
        if (strncmp(head, "once:", 5) == 0) {
            static const char* const loaders[] = {
                "sdmc:/switch/nup-loader/nup-loader.nro",
                "sdmc:/switch/nup-loader.nro",
                "sdmc:/kaihen/programs/nup-loader.nro",
                "sdmc:/nup-loader.nro",
            };
            for (size_t i = 0; i < sizeof(loaders) / sizeof(loaders[0]); i++) {
                if (fileExists(loaders[i])) {
                    char arg[256];
                    snprintf(arg, sizeof(arg), "\"%s\"", loaders[i]);
                    if (R_SUCCEEDED(envSetNextLoad(loaders[i], arg)))
                        return 0;
                    break;
                }
            }
        }
    }

    bool error_screen=0;
    Result lastret=0;
    Result rc=0;
    char msg[768];
    char errormsg[256];//Can't use StrId for these error messages since it would be unavailable if textInit fails.

    #ifdef PERF_LOG
    u64 start_tick=0;
    #endif

    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&g_pad);
    padRepeaterInitialize(&g_pad_repeater, 23, 5); /* ~383ms hold delay, ~83ms repeat */
    hidSetNpadHandheldActivationMode(HidNpadHandheldActivationMode_Single);
    touchInit();

    memset(errormsg, 0, sizeof(errormsg));

    appletLockExit();
    appletSetScreenShotPermission(AppletScreenShotPermission_Enable);

    ColorSetId theme = ColorSetId_Light;
    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        setsysGetColorSetId(&theme);
        setsysExit();
    }

    if (R_SUCCEEDED(rc)) {
        rc = textInit();
        if (R_FAILED(rc)) {
            snprintf(errormsg, sizeof(errormsg)-1, "Error: textInit() failed: 2%03d-%04d", R_MODULE(rc), R_DESCRIPTION(rc));
        }
    }

    if (R_SUCCEEDED(rc)) menuStartupPath();

    if (R_SUCCEEDED(rc)) {
        if (!PHYSFS_init(argv[0])) {
            rc = 1;
            snprintf(errormsg, sizeof(errormsg)-1, "Error: PHYSFS_init() failed: %s", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        }
    }

    if (R_SUCCEEDED(rc)) {
        rc = assetsInit();
        if (R_FAILED(rc)) {
            snprintf(errormsg, sizeof(errormsg)-1, "Error: assetsInit() failed: 2%03d-%04d", R_MODULE(rc), R_DESCRIPTION(rc));
        }
    }

    if (R_SUCCEEDED(rc)) themeStartup((ThemePreset)theme);

    if (R_SUCCEEDED(rc)) powerInit();

    if (R_SUCCEEDED(rc)) {
        rc = netloaderInit();
        if (R_FAILED(rc)) {
            snprintf(errormsg, sizeof(errormsg)-1, "Error: netloaderInit() failed: 2%03d-%04d", R_MODULE(rc), R_DESCRIPTION(rc));
        }
    }

    if (R_SUCCEEDED(rc) && !workerInit()) {
        rc = 1;
        snprintf(errormsg, sizeof(errormsg)-1, "Error: workerInit() failed.");
    }

    if (R_SUCCEEDED(rc) && !statusInit()) {
        rc = 1;
        snprintf(errormsg, sizeof(errormsg)-1, "Error: statusInit() failed.");
    }

    if (R_SUCCEEDED(rc)) menuStartup();

    #ifdef __SWITCH2__
    if (R_SUCCEEDED(rc)) {
        switch2SfxInit();
        switch2UiInit((ThemePreset)theme);
    }
    #endif

    if (R_SUCCEEDED(rc)) {
        if (!launchInit()) {
            rc = 2;
            snprintf(errormsg, sizeof(errormsg)-1, "Error: launchInit() failed.");
        }
    }

    if (R_SUCCEEDED(rc) && !fontInitialize()) {
        rc = 3;
        snprintf(errormsg, sizeof(errormsg)-1, "Error: fontInitialize() failed.");
    }

    #ifdef ENABLE_AUDIO
    if (R_SUCCEEDED(rc)) audio_initialize();
    #endif

    if (R_SUCCEEDED(rc)) {
        lastret = envGetLastLoadResult();

        if (R_FAILED(lastret)) {
            memset(msg, 0, sizeof(msg));
            snprintf(msg, sizeof(msg)-1, "%s\n2%03d-%04d", textGetString(StrId_LastLoadResult), R_MODULE(lastret), R_DESCRIPTION(lastret));

            #ifdef __SWITCH2__
            const StrId explanation = loadResultExplanation(lastret);
            if (explanation != StrId_Max) {
                const size_t used = strlen(msg);
                snprintf(msg + used, sizeof(msg) - 1 - used, "\n\n%s\nDetails: sdmc:/kaihen/nro-cleanup.txt", textGetString(explanation));
                // The box draws no text at all if the text does not fit.
                menuCreateMsgBox(900, 420, msg);
            }
            else
            #endif
            menuCreateMsgBox(780, 300, msg);
        }
    }

    if (errormsg[0]) error_screen = 1;

    if (!error_screen) {
        graphicsInit(FB_WIDTH, FB_HEIGHT);
    }
    else {
        consoleInit(NULL);
        printf("%s\n", errormsg);
        printf("Press the + button to exit.\n");
    }

    while (appletMainLoop())
    {
        // Scan the gamepad. This should be done once for each frame
        padUpdate(&g_pad);
        padRepeaterUpdate(&g_pad_repeater, padGetButtons(&g_pad) & (
            HidNpadButton_Left | HidNpadButton_Up | HidNpadButton_Right | HidNpadButton_Down |
            HidNpadButton_StickLLeft | HidNpadButton_StickLUp |
            HidNpadButton_StickLRight | HidNpadButton_StickLDown
        ));

        if (!error_screen) {
            if (!uiUpdate()) break;
            g_framebuf = graphicsFrameBegin(&g_framebuf_width);
            #ifdef PERF_LOG
            start_tick = armGetSystemTick();
            #endif
            #ifndef __SWITCH2__ // Switch 2 clears in the GPU render pass
            memset(g_framebuf, 237, g_framebuf_width * FB_HEIGHT);
            #endif
            menuLoop();
        }
        else {
            if (menuUpdateErrorScreen()) break;
        }

        if (!error_screen) {
            graphicsFrameEnd();

            #ifdef PERF_LOG
            g_tickdiff_frame = armGetSystemTick() - start_tick;
            #endif
        }
        else {
            consoleUpdate(NULL);
        }
    }

    // Only the error screen leaves through the applet; Plus is intentionally
    // ignored in the menu. A real launch returns to the loader instead.
    if (error_screen)
        __nx_applet_exit_mode = 1;

    if (!error_screen) {
        graphicsExit();
    }
    else {
        consoleExit(NULL);
    }

    #ifdef ENABLE_AUDIO
    audio_exit();
    #endif

    mouseExit();
    #ifdef __SWITCH2__
    switch2SfxExit();
    #endif
    fontExit();
    launchExit();
    netloaderSignalExit();
    statusExit();
    workerExit();
    netloaderExit();
    powerExit();
    assetsExit();
    PHYSFS_deinit();

    appletUnlockExit();

    return 0;
}

u64 menuGetKeysDown(void) {
    /* Left-stick pseudo-buttons repeat like the d-pad (stripped only in
     * mouse mode, where the sticks scroll the list instead); the right
     * stick is never navigation. */
    const u64 right_stick_pseudo =
        HidNpadButton_StickRLeft | HidNpadButton_StickRUp |
        HidNpadButton_StickRRight | HidNpadButton_StickRDown;
    const u64 left_stick_pseudo =
        HidNpadButton_StickLLeft | HidNpadButton_StickLUp |
        HidNpadButton_StickLRight | HidNpadButton_StickLDown;
    const u64 mask = right_stick_pseudo | (mouseIsActive() ? left_stick_pseudo : 0);
    u64 keys = padGetButtonsDown(&g_pad) & ~mask;
    keys |= padRepeaterGetButtons(&g_pad_repeater) & ~mask;
    return keys;
}

static void mouseStickScroll(menu_s* menu) {
    const HidAnalogStickState left = padGetStickPos(&g_pad, 0);
    const HidAnalogStickState right = padGetStickPos(&g_pad, 1);
    const int x = abs(left.x) > abs(right.x) ? left.x : right.x;
    if (x > 10000 || x < -10000)
        menuScrollBy(menu, -x / 4096);
}

//This is implemented here due to the hid code.
bool menuUpdate(void) {
    bool exitflag = 0;
    menu_s* menu = menuGetCurrent();
    const bool mouse_mode = mouseIsActive();
    u64 down = menuGetKeysDown();

#ifdef __SWITCH2__
    handleMouse(menu);
    if (down)
        mouseControllerInput();
    switch2HandleTouch(menu);
    switch2SetHeldButtons(padGetButtons(&g_pad));
    return switch2MenuUpdate(down);
#endif

    if (down)
        mouseControllerInput();

    /* Official Switch 2 bindings: R and ZR both execute; B is Back. */
    if (down & HidNpadButton_R)
        down = (down & ~HidNpadButton_R) | HidNpadButton_A;
    if (down & HidNpadButton_ZR)
        down = (down & ~HidNpadButton_ZR) | HidNpadButton_A;

    ThemeLayoutObject *layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuListTiles];
    int entries_count = layoutobj->posEnd[0];

    handleTouch(menu);
    handleMouse(menu);
    if (mouse_mode && !menuIsSettingsMenu() &&
        !menuIsMsgBoxOpen() && !menuIsConfirmMsgBoxOpen())
        mouseStickScroll(menu);

    // A confirmation box takes only its two choices.
    if (menuIsConfirmMsgBoxOpen())
    {
        if (down & HidNpadButton_A)
            menuHandleAButton();
        else if (down & HidNpadButton_B)
            launchMenuBackTask();
        return exitflag;
    }

    if (menuIsSettingsMenu())
    {
        if (down & HidNpadButton_A) {
            settingsMenuToggleCurrent();
            return exitflag;
        }
        if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
            launchMenuBackTask();
            return exitflag;
        }
    }

    if (!menuIsSettingsMenu() && (down & HidNpadButton_Y))
    {
        launchMenuNetloaderTask();
    }
    else if (!menuIsSettingsMenu() && (down & HidNpadButton_X))
    {
        menuHandleXButton();
    }
    else if (down & HidNpadButton_A)
    {
        menuHandleAButton();
    }
    else if (down & HidNpadButton_B)
    {
        launchMenuBackTask();
    }
    else if(!menuIsSettingsMenu() && (down & HidNpadButton_Minus)){
        themeMenuStartup();
    }
    else if(down & HidNpadButton_Plus){
        if (menuIsSettingsMenu())
            launchMenuBackTask();
        else
            settingsMenuStartup();
    }
    else if (menu->nEntries > 0 && !mouse_mode)
    {
        int move = 0;

        /* D-pad only; both analog sticks use the same latched horizontal scroll. */
        if (down & HidNpadButton_Left) move--;
        if (down & HidNpadButton_Right) move++;
        if (down & (HidNpadButton_Down | HidNpadButton_ZL)) move-=entries_count;
        if (down & (HidNpadButton_Up | HidNpadButton_ZR)) move+=entries_count;

        static int leftStickDirection;
        static int rightStickDirection;
        static u64 leftStickNextTick;
        static u64 rightStickNextTick;
        const u64 now_tick = svcGetSystemTick();

        const HidAnalogStickState left_stick = padGetStickPos(&g_pad, 0);
        const int left_direction = left_stick.x > 10000 ? 1 : left_stick.x < -10000 ? -1 : 0;
        if (left_direction != leftStickDirection) {
            leftStickDirection = left_direction;
            if (left_direction) {
                move += left_direction;
                leftStickNextTick = now_tick + armNsToTicks(180000000ULL);
            }
        } else if (left_direction && now_tick >= leftStickNextTick) {
            move += left_direction;
            leftStickNextTick = now_tick + armNsToTicks(180000000ULL);
        }

        const HidAnalogStickState right_stick = padGetStickPos(&g_pad, 1);
        const int right_direction = right_stick.x > 10000 ? 1 : right_stick.x < -10000 ? -1 : 0;
        if (right_direction != rightStickDirection) {
            rightStickDirection = right_direction;
            if (right_direction) {
                move += right_direction;
                rightStickNextTick = now_tick + armNsToTicks(180000000ULL);
            }
        } else if (right_direction && now_tick >= rightStickNextTick) {
            move += right_direction;
            rightStickNextTick = now_tick + armNsToTicks(180000000ULL);
        }

        int newEntry = menu->curEntry + move;
        if (newEntry < 0) newEntry = 0;
        if (newEntry >= menu->nEntries) newEntry = menu->nEntries-1;
        if (newEntry != menu->curEntry)
            menu->holdXPos = false;
        menu->curEntry = newEntry;
    }

    return exitflag;
}

bool menuUpdateErrorScreen(void) {
    bool exitflag = 0;
    u64 down = menuGetKeysDown();

    if (down & HidNpadButton_Plus)
    {
        exitflag = 1;
    }

    return exitflag;
}
