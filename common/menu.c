#include <time.h>
#include "common.h"
#include "netloader.h"

#ifndef __SWITCH__
#include "switch/runtime/nxlink.h"
#endif

double menuTimer;

char rootPathBase[PATH_MAX];
char rootPath[PATH_MAX+8];

uint8_t *folder_icon_large, *folder_icon_small;
uint8_t *invalid_icon_large, *invalid_icon_small;
uint8_t *theme_icon_large, *theme_icon_small;

void computeFrontGradient(color_t baseColor, int height);

void menuLoadFileassoc(void);

char *menuGetRootPath(void) {
    return rootPath;
}

char *menuGetRootBasePath(void) {
    return rootPathBase;
}

static void launchMenuEntryConfirmed(void* arg) {
    launchMenuEntry((menuEntry_s*)arg);
}

void launchMenuEntryTask(menuEntry_s* arg) {
    menuEntry_s* me = arg;
    if (me->type == ENTRY_TYPE_FOLDER)
        menuScan(me->path);
        //changeDirTask(me->path);
    else if(me->type == ENTRY_TYPE_THEME)
        launchApplyThemeTask(me);
    else if (launchMenuEntryNeedsPlatformWarning(me))
        menuCreateConfirmMsgBox(780, 300, textGetString(StrId_Switch1Warning), launchMenuEntryConfirmed, me);
    else
        launchMenuEntry(me);
}

void toggleStarState(menuEntry_s* arg) {
    menuEntry_s* me = arg;
    if (me->starred) {
        if (fileExists(me->starpath))
            remove(me->starpath);
    } else {
        if (!fileExists(me->starpath)) {
            FILE* f  = fopen(me->starpath, "w");
            if (f) fclose(f);
        }
    }
    me->starred = fileExists(me->starpath);
    //todo: error handling/message?

    menuReorder();
    menu_s* menu = menuGetCurrent();
    menuEntry_s* meSearch = menu->firstEntry;
    menu->curEntry = -1;
    int i = 0;
    while (menu->curEntry < 0) {
        if (me == meSearch)
            menu->curEntry = i;
        else {
            meSearch = meSearch->next;
            i++;
        }
    }
}

static enum
{
    HBMENU_DEFAULT,
    HBMENU_NETLOADER_ACTIVE,
    HBMENU_NETLOADER_ERROR,
    HBMENU_NETLOADER_SUCCESS,
    HBMENU_THEME_MENU,
    HBMENU_SETTINGS_MENU,
} hbmenu_state = HBMENU_DEFAULT;

static char settingsReturnPath[PATH_MAX + 1];

void launchMenuNetloaderTask() {
    if(hbmenu_state == HBMENU_DEFAULT)
        workerSchedule(netloaderTask, NULL);
}

void launchMenuBackTask() {
    if (hbmenu_state != HBMENU_NETLOADER_ACTIVE && menuIsConfirmMsgBoxOpen()) {
        menuCloseMsgBox();
        return;
    }

    if(hbmenu_state == HBMENU_NETLOADER_ACTIVE) {
        netloaderSignalExit();
    }
    else if(hbmenu_state == HBMENU_SETTINGS_MENU) {
        hbmenu_state = HBMENU_DEFAULT;
        menuScan(settingsReturnPath[0] ? settingsReturnPath : rootPath);
    }
    else if(hbmenu_state == HBMENU_THEME_MENU) {
        hbmenu_state = HBMENU_DEFAULT;
        menuScan(rootPath);
    }
    else {
        menuScan("..");
    }

}

void menuScrollBy(menu_s* menu, int delta) {
    ThemeLayoutObject* tiles = &themeCurrent.layoutObjects[ThemeLayoutId_MenuListTiles];
    ThemeLayoutObject* list = &themeCurrent.layoutObjects[ThemeLayoutId_MenuList];
    const int visible = tiles->posEnd[0];
    const int entry_w = list->posEnd[0];
    if (!delta || visible <= 0 || entry_w <= 0 || menu->nEntries <= visible)
        return;

    const int min_x = -(menu->nEntries - visible) * entry_w;
    menu->xPos += delta;
    if (menu->xPos > 0) menu->xPos = 0;
    if (menu->xPos < min_x) menu->xPos = min_x;
    menu->slideSpeed = 0;
    menu->holdXPos = true;
}

void menuScrollPage(menu_s* menu, int direction) {
    ThemeLayoutObject* list = &themeCurrent.layoutObjects[ThemeLayoutId_MenuList];
    menuScrollBy(menu, -direction * list->posEnd[0]);
}

void menuHandleAButton(void) {
    menu_s* menu = menuGetCurrent();

    if (hbmenu_state != HBMENU_NETLOADER_ACTIVE && menuIsConfirmMsgBoxOpen()) {
        menuConfirmMsgBox();
    }
    else if (hbmenu_state != HBMENU_NETLOADER_ACTIVE && menuIsMsgBoxOpen()) {
        menuCloseMsgBox();
    }
    else if (menu->nEntries > 0 &&
             (hbmenu_state == HBMENU_DEFAULT || hbmenu_state == HBMENU_THEME_MENU))
    {
        int i;
        menuEntry_s* me = menu->firstEntry;
        for (i = 0; i < menu->curEntry && me; i ++, me = me->next);
        if (!me)
            return;
        /* A stale selection from the previous screen must not open anything:
         * a real folder entry is exactly one path component below dirname. */
        if (me->type == ENTRY_TYPE_FOLDER) {
            const char* d = menu->dirname;
            const size_t dlen = strlen(d);
            const char* rest;
            if (strncmp(me->path, d, dlen) != 0)
                return;
            rest = me->path + dlen;
            if (*rest == '/')
                rest++;
            if (*rest == '\0' || strchr(rest, '/') != NULL)
                return;
        }
        launchMenuEntryTask(me);
        //workerSchedule(launchMenuEntryTask, me);
    }
    else if (menu->nEntries > 0 && hbmenu_state == HBMENU_SETTINGS_MENU)
    {
        settingsMenuToggleCurrent();
    }
}

void menuHandleXButton(void) {
    menu_s* menu = menuGetCurrent();

    if (menu->nEntries > 0 && hbmenu_state == HBMENU_DEFAULT) {
        int i;
        menuEntry_s* me;
        for (i = 0, me = menu->firstEntry; i != menu->curEntry; i ++, me = me->next);
        toggleStarState(me);
    }
}

void menuStartupCommon(void) {
    FreeImageBuffer(folder_icon_large);
    FreeImageBuffer(folder_icon_small);
    FreeImageBuffer(invalid_icon_large);
    FreeImageBuffer(invalid_icon_small);
    FreeImageBuffer(theme_icon_large);
    FreeImageBuffer(theme_icon_small);

    ThemeLayoutObject *layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuActiveEntryIcon];
    ThemeLayoutObject *layoutobj2 = &themeCurrent.layoutObjects[ThemeLayoutId_MenuListIcon];
    assetsDataEntry *data = NULL;

    assetsGetData(AssetId_folder_icon, &data);
    folder_icon_large = downscaleImg(data->buffer, layoutobj->imageSize[0], layoutobj->imageSize[1], layoutobj->size[0], layoutobj->size[1], data->imageMode);
    folder_icon_small = downscaleImg(data->buffer, layoutobj->imageSize[0], layoutobj->imageSize[1], layoutobj2->size[0], layoutobj2->size[1], data->imageMode);
    #ifdef __SWITCH2__
    // The resized copies are drawn from the asset's vector too.
    renderAliasImageVector(folder_icon_large, data->buffer);
    renderAliasImageVector(folder_icon_small, data->buffer);
    #endif
    assetsGetData(AssetId_invalid_icon, &data);
    invalid_icon_large = downscaleImg(data->buffer, layoutobj->imageSize[0], layoutobj->imageSize[1], layoutobj->size[0], layoutobj->size[1], data->imageMode);
    invalid_icon_small = downscaleImg(data->buffer, layoutobj->imageSize[0], layoutobj->imageSize[1], layoutobj2->size[0], layoutobj2->size[1], data->imageMode);
    #ifdef __SWITCH2__
    renderAliasImageVector(invalid_icon_large, data->buffer);
    renderAliasImageVector(invalid_icon_small, data->buffer);
    #endif
    if(themeGlobalPreset == THEME_PRESET_DARK)
        assetsGetData(AssetId_theme_icon_dark, &data);
    else
        assetsGetData(AssetId_theme_icon_light, &data);
    theme_icon_large = downscaleImg(data->buffer, layoutobj->imageSize[0], layoutobj->imageSize[1], layoutobj->size[0], layoutobj->size[1], data->imageMode);
    theme_icon_small = downscaleImg(data->buffer, layoutobj->imageSize[0], layoutobj->imageSize[1], layoutobj2->size[0], layoutobj2->size[1], data->imageMode);
    #ifdef __SWITCH2__
    renderAliasImageVector(theme_icon_large, data->buffer);
    renderAliasImageVector(theme_icon_small, data->buffer);
    #endif

    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_FrontWave];
    computeFrontGradient(themeCurrent.frontWaveColor, layoutobj->size[1]);
}

void menuThemeSelectCurrentEntry(void) {
    menu_s* menu = menuGetCurrent();
    char themePath[PATH_MAX] = {0};
    GetThemePathFromConfig(themePath, PATH_MAX);
    if (themePath[0]==0) menu->curEntry = 0;
    else {
        int i;
        menuEntry_s* me;
        for (i = 0, me = menu->firstEntry; me != NULL; i ++, me = me->next) {
            if (strcmp(me->path, themePath)==0) {
                menu->curEntry = i;
                break;
            }
        }
    }
}

void launchApplyThemeTask(menuEntry_s* arg) {
    const char* themePath = arg->path;
    menu_s* menu = menuGetCurrent();
    SetThemePathToConfig(themePath);
    themeStartup(themeGlobalPreset);
    menuStartupCommon();
    menuLoadFileassoc();
    if (hbmenu_state == HBMENU_THEME_MENU) { // Normally this should never be used outside of theme-menu.
        themeMenuScan(menu->dirname);
        menuThemeSelectCurrentEntry();
    } else menuScan(menu->dirname);
}

bool menuIsNetloaderActive(void) {
    return hbmenu_state == HBMENU_NETLOADER_ACTIVE;
}

bool menuIsSettingsMenu(void) {
    return hbmenu_state == HBMENU_SETTINGS_MENU;
}

#ifdef __SWITCH2__
// Switch 2 draws image buffers as cached GPU textures (switch2/render.h).
static void drawImage(int x, int y, int width, int height, const uint8_t *image, ImageMode mode) {
    enum RenderImageFormat format = mode == IMAGE_MODE_RGB24 ? RenderImageFormat_Rgb24 : RenderImageFormat_Rgba32;
    renderImage(image, x, y, width, height, format, 0xffffffffu);
}

// Draws an image_width x image_height image at width x height, scaled on the GPU.
static void drawImageScaled(int x, int y, int width, int height, const uint8_t *image, int image_width, int image_height, ImageMode mode) {
    enum RenderImageFormat format = mode == IMAGE_MODE_RGB24 ? RenderImageFormat_Rgb24 : RenderImageFormat_Rgba32;
    renderImageScaled(image, image_width, image_height, x, y, width, height, format, 0xffffffffu);
}

static void drawIcon(int x, int y, int width, int height, const uint8_t *image, color_t color) {
    renderImage(image, x, y, width, height, RenderImageFormat_AlphaMask, color.abgr | 0xff000000u);
}
#else
//Draws an RGB888 or RGBA8888 image.
static void drawImage(int x, int y, int width, int height, const uint8_t *image, ImageMode mode) {
    int tmpx, tmpy;
    int pos;
    color_t current_color;

    for (tmpy=0; tmpy<height; tmpy++) {
        for (tmpx=0; tmpx<width; tmpx++) {
            switch (mode) {
                case IMAGE_MODE_RGB24:
                    pos = ((tmpy*width) + tmpx) * 3;
                    current_color = MakeColor(image[pos+0], image[pos+1], image[pos+2], 255);
                    break;

                case IMAGE_MODE_RGBA32:
                    pos = ((tmpy*width) + tmpx) * 4;
                    current_color = MakeColor(image[pos+0], image[pos+1], image[pos+2], image[pos+3]);
                    break;
            }

            DrawPixel(x+tmpx, y+tmpy, current_color);
        }
    }
}

//Draws an RGBA8888 image masked by the passed color.
static void drawIcon(int x, int y, int width, int height, const uint8_t *image, color_t color) {
    int tmpx, tmpy;
    int pos;
    color_t current_color;

    for (tmpy=0; tmpy<height; tmpy++) {
        for (tmpx=0; tmpx<width; tmpx++) {
            pos = ((tmpy*width) + tmpx) * 4;
            current_color = MakeColor(color.r, color.g, color.b, image[pos+3]);
            DrawPixel(x+tmpx, y+tmpy, current_color);
        }
    }
}
#endif

static void drawEntry(menuEntry_s* me, int off_x, int is_active) {
    ThemeLayoutObject *layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuList];
    int x, y;
    int start_y = layoutobj->posStart[1];//*(n % 2);
    int end_y = start_y + layoutobj->size[1];
    int start_x = off_x;//(n / 2);
    int end_x = start_x + layoutobj->size[0];
    int j;

    const uint8_t *smallimg = NULL;
    const uint8_t *largeimg = NULL;
    const char *strptr = NULL;
    char tmpstr[1024];

    int border_start_x, border_end_x;
    int border_start_y, border_end_y;
    color_t border_color = themeCurrent.borderColor;

    int shadow_start_y, shadow_y;
    int shadow_inset;
    color_t shadow_color;
    uint8_t shadow_alpha_base = 80;
    float highlight_multiplier;
    int shadow_size = 4;

    if (is_active) {
        highlight_multiplier = fmax(0.0, fabs(fmod(menuTimer, 1.0) - 0.5) / 0.5);
        border_color = MakeColor(themeCurrent.highlightColor.r + (themeCurrent.highlightGradientEdgeColor.r - themeCurrent.highlightColor.r) * highlight_multiplier, themeCurrent.highlightColor.g + (themeCurrent.highlightGradientEdgeColor.g - themeCurrent.highlightColor.g) * highlight_multiplier, themeCurrent.highlightColor.b + (themeCurrent.highlightGradientEdgeColor.b - themeCurrent.highlightColor.b) * highlight_multiplier, 255);
        border_start_x = start_x-6;
        border_end_x = end_x+6;
        border_start_y = start_y-5;
        border_end_y = end_y+5;
    }
    else {
        border_start_x = start_x-4;
        border_end_x = end_x+4;
        border_start_y = start_y-3;
        border_end_y = end_y+3;
    }

    //{
        for (x=border_start_x; x<border_end_x; x+=4) {
            Draw4PixelsRaw(x, end_y    , border_color);
            Draw4PixelsRaw(x, start_y-1, border_color);
            Draw4PixelsRaw(x, end_y  +1, border_color);
            Draw4PixelsRaw(x, start_y-2, border_color);
            Draw4PixelsRaw(x, end_y  +2, border_color);
            Draw4PixelsRaw(x, start_y-3, border_color);
            Draw4PixelsRaw(x, end_y  +3, border_color);

            if (is_active) {
                Draw4PixelsRaw(x, start_y-3, border_color);
                Draw4PixelsRaw(x, end_y  +3, border_color);
                Draw4PixelsRaw(x, start_y-4, border_color);
                Draw4PixelsRaw(x, end_y  +4, border_color);
                Draw4PixelsRaw(x, start_y-5, border_color);
                Draw4PixelsRaw(x, end_y  +5, border_color);
                shadow_start_y = 6;
            }
            else {
                shadow_start_y = 4;
            }

            for (shadow_y=shadow_start_y; shadow_y < shadow_start_y+shadow_size; shadow_y++) {
                shadow_color = MakeColor(0, 0, 0, shadow_alpha_base * (1.0 - (float)(shadow_y - shadow_start_y) / ((float)shadow_size)));
                shadow_inset =(shadow_y-shadow_start_y);

                if (x >= border_start_x + shadow_inset && x < border_end_x - shadow_inset) {
                    for (j=0; j<4; j++) DrawPixel(x+j, end_y  +shadow_y, shadow_color);
                }
            }
        }

        for (y=border_start_y; y<border_end_y; y++) {
            DrawPixelRaw(start_x  , y, border_color);
            DrawPixelRaw(end_x    , y, border_color);
            DrawPixelRaw(start_x-1, y, border_color);
            DrawPixelRaw(end_x  +1, y, border_color);
            DrawPixelRaw(start_x-2, y, border_color);
            DrawPixelRaw(end_x  +2, y, border_color);
            DrawPixelRaw(start_x-3, y, border_color);
            DrawPixelRaw(end_x  +3, y, border_color);
            DrawPixelRaw(start_x-4, y, border_color);

            if (is_active) {
                DrawPixelRaw(end_x  +4, y, border_color);
                DrawPixelRaw(start_x-5, y, border_color);
                DrawPixelRaw(end_x  +5, y, border_color);
                DrawPixelRaw(start_x-6, y, border_color);
            }
        }
    //}

    for (y=start_y; y<end_y; y++) {
        for (x=start_x; x<end_x; x+=4) {
            Draw4PixelsRaw(x, y, themeCurrent.borderColor);
        }
    }

    ImageMode iconmode = IMAGE_MODE_RGB24;
    bool entryicon = false;

    if (me->icon_gfx_small && me->icon_gfx) {
        smallimg = me->icon_gfx_small;
        largeimg = me->icon_gfx;
        iconmode = me->icon_mode;
        entryicon = true;
    }
    else if (me->type == ENTRY_TYPE_FOLDER) {
        smallimg = folder_icon_small;
        largeimg = folder_icon_large;
    }
    else if (me->type == ENTRY_TYPE_THEME){
        smallimg = theme_icon_small;
        largeimg = theme_icon_large;
    }
    else {
        smallimg = invalid_icon_small;
        largeimg = invalid_icon_large;
    }

    if (smallimg) {
        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuListIcon];
#ifdef __SWITCH2__
        // The small icon is downscaled for a 720p screen and looks blocky once
        // the menu is scaled for 1080p or 4K: scale the large icon instead.
        const ThemeLayoutObject *largeobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuActiveEntryIcon];
        if (entryicon)
            drawImageScaled(start_x + layoutobj->posStart[0], start_y + layoutobj->posStart[1], layoutobj->size[0], layoutobj->size[1], largeimg, largeobj->size[0], largeobj->size[1], iconmode);
        else
            drawImage(start_x + layoutobj->posStart[0], start_y + layoutobj->posStart[1], layoutobj->size[0], layoutobj->size[1], smallimg, iconmode);
#else
        (void)entryicon;
        drawImage(start_x + layoutobj->posStart[0], start_y + layoutobj->posStart[1], layoutobj->size[0], layoutobj->size[1], smallimg, iconmode);
#endif
    }

    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuActiveEntryIcon];
    if (is_active && largeimg && layoutobj->visible) {
        drawImage(layoutobj->posStart[0], layoutobj->posStart[1], layoutobj->size[0], layoutobj->size[1], largeimg, iconmode);

        shadow_start_y = layoutobj->posStart[1]+layoutobj->size[1];
        border_start_x = layoutobj->posStart[0];
        border_end_x = layoutobj->posStart[0]+layoutobj->size[0];

        for (shadow_y=shadow_start_y; shadow_y <shadow_start_y+shadow_size; shadow_y++) {
            for (x=border_start_x; x<border_end_x; x++) {
                shadow_color = MakeColor(0, 0, 0, shadow_alpha_base * (1.0 - (float)(shadow_y - shadow_start_y) / ((float)shadow_size)));
                shadow_inset =(shadow_y-shadow_start_y);

                if (x >= border_start_x + shadow_inset && x <= border_end_x - shadow_inset) {
                    DrawPixel(x, shadow_y, shadow_color);
                }
            }
        }
    }

    if (me->type != ENTRY_TYPE_THEME)
        strptr = me->starred ? themeCurrent.labelStarOnText : "";
    else
        strptr = "";

    memset(tmpstr, 0, sizeof(tmpstr));
    snprintf(tmpstr, sizeof(tmpstr)-1, "%s%s", strptr, me->name);

    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuListName];
    DrawTextTruncate(layoutobj->font, start_x + layoutobj->posStart[0], start_y + layoutobj->posStart[1], themeCurrent.borderTextColor, tmpstr, layoutobj->size[0], "...");

    if (is_active) {
        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuActiveEntryName];
        if (layoutobj->visible) DrawTextTruncate(layoutobj->font, layoutobj->posStart[0], layoutobj->posStart[1], themeCurrent.textColor, tmpstr, layoutobj->size[0], "...");

        if (me->type != ENTRY_TYPE_FOLDER) {
            memset(tmpstr, 0, sizeof(tmpstr));
            snprintf(tmpstr, sizeof(tmpstr)-1, "%s: %s", textGetString(StrId_AppInfo_Author), me->author);
            layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuActiveEntryAuthor];
            if (layoutobj->visible) DrawText(layoutobj->font, layoutobj->posStart[0], layoutobj->posStart[1], themeCurrent.textColor, tmpstr);

            memset(tmpstr, 0, sizeof(tmpstr));
            snprintf(tmpstr, sizeof(tmpstr)-1, "%s: %s", textGetString(StrId_AppInfo_Version), me->version);
            layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuActiveEntryVersion];
            if (layoutobj->visible) DrawText(layoutobj->font, layoutobj->posStart[0], layoutobj->posStart[1], themeCurrent.textColor, tmpstr);

            if (me->type != ENTRY_TYPE_THEME && me->abi_revision < NRO_ABI_CURRENT_REVISION) {
                strptr = textGetString(StrId_RecompileWarning);
                layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuActiveEntryRecompileWarning];
                if (layoutobj->visible) DrawText(layoutobj->font, layoutobj->posStart[0], layoutobj->posStart[1], themeCurrent.attentionTextColor, strptr);
            }
        }
    }
}

color_t frontWaveGradient[720];

void computeFrontGradient(color_t baseColor, int height) {
    int y;
    int alpha;
    float dark_mult, dark_sub = 75;
    color_t color;

    if (height < 0 || height > 720) return;

    for (y=0; y<720; y++) {
        alpha = y - (720 - height);

        if (alpha < 0)
            color = baseColor;
        else {
            dark_mult = clamp((float)(alpha - 50) / (float)height, 0.0, 1.0);
            color = MakeColor(baseColor.r - dark_sub * dark_mult, baseColor.g - dark_sub * dark_mult, baseColor.b - dark_sub * dark_mult, 255);
        }

        frontWaveGradient[y] = color;
    }
}

void menuStartupPath(void) {
    char tmp_path[PATH_MAX+28];

    #ifdef __SWITCH__
    strncpy(rootPathBase, "sdmc:", sizeof(rootPathBase)-1);
    #else
    getcwd(rootPathBase, sizeof(rootPathBase));
    #endif
    snprintf(rootPath, sizeof(rootPath)-1, "%s%s%s", rootPathBase, DIRECTORY_SEPARATOR, "switch");

    struct stat st = {0};

    if (stat(rootPath, &st) == -1) {
        mkdir(rootPath, 0755);
    }

    snprintf(tmp_path, sizeof(tmp_path)-1, "%s/config/nx-hbmenu/themes", rootPathBase);
    if (stat(tmp_path, &st) == -1) {
        snprintf(tmp_path, sizeof(tmp_path)-1, "%s/config", rootPathBase);
        mkdir(tmp_path, 0755);

        snprintf(tmp_path, sizeof(tmp_path)-1, "%s/config/nx-hbmenu", rootPathBase);
        mkdir(tmp_path, 0755);

        snprintf(tmp_path, sizeof(tmp_path)-1, "%s/config/nx-hbmenu/themes", rootPathBase);
        mkdir(tmp_path, 0755);
    }

    snprintf(tmp_path, sizeof(tmp_path)-1, "%s/config/nx-hbmenu/fileassoc", rootPathBase);
    if (stat(tmp_path, &st) == -1) {
        mkdir(tmp_path, 0755);
    }
}

void menuLoadFileassoc(void) {
    char tmp_path[PATH_MAX+28];

    memset(tmp_path, 0, sizeof(tmp_path)-1);
    snprintf(tmp_path, sizeof(tmp_path)-1, "%s/config/nx-hbmenu/fileassoc", rootPathBase);
    menuFileassocScan(tmp_path);
}

void menuStartup(void) {
    menuLoadFileassoc();
    menuHiddenLoad();

    menuScan(rootPath);

    menuStartupCommon();
}

void themeMenuStartup(void) {
    if(hbmenu_state != HBMENU_DEFAULT) return;
    hbmenu_state = HBMENU_THEME_MENU;
    char tmp_path[PATH_MAX+25];

    snprintf(tmp_path, sizeof(tmp_path)-1, "%s%s%s%s%s%s%s", rootPathBase, DIRECTORY_SEPARATOR, "config", DIRECTORY_SEPARATOR, "nx-hbmenu" , DIRECTORY_SEPARATOR, "themes");

    themeMenuScan(tmp_path);
    menuThemeSelectCurrentEntry();
}

void settingsMenuStartup(void) {
    if(hbmenu_state != HBMENU_DEFAULT) return;
    menu_s* menu = menuGetCurrent();
    snprintf(settingsReturnPath, sizeof(settingsReturnPath), "%s", menu->dirname);
    hbmenu_state = HBMENU_SETTINGS_MENU;
    settingsMenuScan(rootPath);
}

color_t waveBlendAdd(color_t a, color_t b, float alpha) {
    return MakeColor(a.r*(1.0f-alpha) + b.r*alpha, a.g*(1.0f-alpha) + b.g*alpha, a.b*(1.0f-alpha) + b.b*alpha, 255);
}

#ifdef __SWITCH2__
// Switch 2 draws the same columns as upstream as GPU rectangles. Rows that
// upstream mixes with the existing pixel (the anti-aliased top edge, and
// every row with enableWaveBlending) blend on the GPU instead of reading the
// framebuffer back. Below them each column is one filled rectangle.
// Columns do not overlap, so all edge rows are drawn before all fills; this
// keeps the fills, which may use the gradient texture, in one draw.
typedef struct {
    float top_y;      // upstream's wave_top_y
    int fill_start_y; // first row of the filled rectangle
    int end_y;        // one past the last row upstream draws
} waveColumn_s;

static waveColumn_s waveColumnAt(int id, int x, float timer, int height, float phase, float speed) {
    waveColumn_s column;
    int y;

    column.top_y = approxSin(x*speed/1280.0+timer+phase) * 10.0 + height;

    // Upstream stops at 720, or for waves 0 and 1 after y > top + 30.
    column.end_y = 720;
    if (id != 2 && floorf(column.top_y + 30) + 1 < column.end_y)
        column.end_y = floorf(column.top_y + 30) + 1;

    // Upstream blends rows with alpha (y - top) below 1.0 when wave blending,
    // and anti-aliases rows below 0.3 otherwise; the rest are uniform.
    float edge_alpha = themeCurrent.enableWaveBlending ? 1.0 : 0.3;
    y = column.top_y;
    while (y < column.end_y && y - column.top_y < edge_alpha)
        y++;
    column.fill_start_y = y;

    return column;
}

static uint32_t waveAlpha(float coverage) {
    return (uint32_t)roundf(clamp(coverage, 0.0, 1.0) * 255.0f) << 24;
}

void drawWave(int id, float timer, color_t color, int height, float phase, float speed) {
    int x, y;
    float alpha;
    uint32_t rgb = color.abgr & 0xffffffu;
    waveColumn_s column;

    if (height < 0 || height > 720) return;
    height = 720 - height;

    for (x=0; x<1280; x++) {
        column = waveColumnAt(id, x, timer, height, phase, speed);

        for (y=column.top_y; y<column.fill_start_y; y++) {
            alpha = y-column.top_y;
            if (themeCurrent.enableWaveBlending)
                renderRect(x, y, x+1, y+1, rgb | waveAlpha(clamp(alpha, 0.0, 1.0) * 0.3));
            else // anti-aliasing
                renderRect(x, y, x+1, y+1, rgb | waveAlpha(1.0 - fabs(alpha)));
        }
    }

    for (x=0; x<1280; x++) {
        column = waveColumnAt(id, x, timer, height, phase, speed);

        if (themeCurrent.enableWaveBlending)
            renderRect(x, column.fill_start_y, x+1, column.end_y, rgb | waveAlpha(0.3));
        else if (id == 2)
            renderGradientRect(x, column.fill_start_y, x+1, column.end_y, &frontWaveGradient[0].abgr);
        else
            renderRect(x, column.fill_start_y, x+1, column.end_y, rgb | 0xff000000u);
    }
}
#else
void drawWave(int id, float timer, color_t color, int height, float phase, float speed) {
    int x, y;
    float wave_top_y, alpha, one_minus_alpha;
    color_t existing_color, new_color;

    if (height < 0 || height > 720) return;
    height = 720 - height;

    for (x=0; x<1280; x++) {
        wave_top_y = approxSin(x*speed/1280.0+timer+phase) * 10.0 + height;

        for (y=wave_top_y; y<720; y++) {
            if (id != 2 && y > wave_top_y + 30)
                break;

            alpha = y-wave_top_y;

            if (themeCurrent.enableWaveBlending) {
                existing_color = FetchPixelColor(x, y);
                new_color = waveBlendAdd(existing_color, color, clamp(alpha, 0.0, 1.0) * 0.3);
            }
            else if (alpha >= 0.3) {
                if (id == 2) {
                    new_color = frontWaveGradient[y];
                }
                else { // no anti-aliasing
                    new_color = color;
                }
            }
            else { // anti-aliasing
                existing_color = FetchPixelColor(x, y);
                alpha = fabs(alpha);
                one_minus_alpha = (1.0 - alpha);
                new_color = MakeColor(color.r * one_minus_alpha + existing_color.r * alpha, color.g * one_minus_alpha + existing_color.g * alpha, color.b * one_minus_alpha + existing_color.b * alpha, 255);
            }

            DrawPixelRaw(x, y, new_color);
        }
    }
}
#endif

void drawCharge() {
    char chargeString[5];
    uint32_t batteryCharge;
    bool isCharging;
    bool validPower;

    validPower = powerGetDetails(&batteryCharge, &isCharging);

    if (validPower)
    {
        batteryCharge = (batteryCharge > 100) ? 100 : batteryCharge;

        sprintf(chargeString, "%d%%", batteryCharge);

        ThemeLayoutObject *layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_BatteryCharge];

        if (layoutobj->visible) {
            int tmpX = GetTextXCoordinate(layoutobj->font, layoutobj->posStart[0], chargeString, 'r');
            DrawText(layoutobj->font, tmpX, layoutobj->posStart[1], themeCurrent.textColor, chargeString);
        }

        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_BatteryIcon];
        assetsDataEntry *data = NULL;
        assetsGetData(AssetId_battery_icon, &data);
        if (layoutobj->visible) drawIcon(layoutobj->posStart[0], layoutobj->posStart[1], data->imageSize[0], data->imageSize[1], data->buffer, themeCurrent.textColor);

        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_ChargingIcon];
        assetsGetData(AssetId_charging_icon, &data);
        if (isCharging && layoutobj->visible)
            drawIcon(layoutobj->posStart[0], layoutobj->posStart[1], data->imageSize[0], data->imageSize[1], data->buffer, themeCurrent.textColor);
    }

}

void drawNetwork(int tmpX, AssetId id) {
    ThemeLayoutObject *layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_NetworkIcon];
    assetsDataEntry *data = NULL;
    assetsGetData(id, &data);
    if (layoutobj->visible) drawIcon(layoutobj->posType ? tmpX + layoutobj->posStart[0] : layoutobj->posStart[0], layoutobj->posStart[1], data->imageSize[0], data->imageSize[1], data->buffer, themeCurrent.textColor);
}

u32 drawStatus() {
    bool netstatusFlag=0;
    bool temperatureFlag=0;
    s32 temperature=0;
    AssetId id;

    char tmpstr[32];

    time_t unixTime = time(NULL);
    struct tm* timeStruct = localtime((const time_t *)&unixTime);

    int hours = timeStruct->tm_hour;
    int minutes = timeStruct->tm_min;
    int seconds = timeStruct->tm_sec;

    snprintf(tmpstr, sizeof(tmpstr)-1, "%02d:%02d:%02d", hours, minutes, seconds);

    ThemeLayoutObject *layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_Status];

    u32 tmpX = GetTextXCoordinate(layoutobj->font, layoutobj->posStart[0], tmpstr, 'r');

    if (layoutobj->visible) DrawText(layoutobj->font, tmpX, layoutobj->posStart[1], themeCurrent.textColor, tmpstr);

    drawCharge();

    if (statusGet(&netstatusFlag, &id, &temperatureFlag, &temperature)) {
        if (netstatusFlag) drawNetwork(tmpX, id);
        if (temperatureFlag) {
            snprintf(tmpstr, sizeof(tmpstr)-1, "%d°C", temperature);
            DrawTextFromLayout(ThemeLayoutId_Temperature, themeCurrent.textColor, tmpstr);
        }
    }

    return tmpX;
}

static void fillRect(int x, int y, int width, int height, color_t color) {
    for (int row = 0; row < height; row++) {
        int col = 0;
        for (; col + 3 < width; col += 4)
            Draw4PixelsRaw(x + col, y + row, color);
        for (; col < width; col++)
            DrawPixelRaw(x + col, y + row, color);
    }
}

static void drawMenuScrollbar(menu_s* menu) {
    ThemeLayoutObject* list = &themeCurrent.layoutObjects[ThemeLayoutId_MenuList];
    ThemeLayoutObject* tiles = &themeCurrent.layoutObjects[ThemeLayoutId_MenuListTiles];
    const int visible = tiles->posEnd[0];
    if (visible <= 0 || menu->nEntries <= visible)
        return;

    const int track_x = list->posStart[0];
    const int track_w = HBMENU_LOGICAL_WIDTH - track_x * 2;
    const int track_y = list->posStart[1] + list->size[1] + 10;
    const int track_h = 4;
    const int entry_w = list->posEnd[0];
    const int max_start = menu->nEntries - visible;
    int start = entry_w > 0 ? (int)(-menu->xPos / entry_w + 0.5f) : 0;
    if (start < 0) start = 0;
    if (start > max_start) start = max_start;

    int thumb_w = track_w * visible / menu->nEntries;
    if (thumb_w < 40) thumb_w = 40;
    if (thumb_w > track_w) thumb_w = track_w;
    const int thumb_x = track_x + (track_w - thumb_w) * start / max_start;

    fillRect(track_x, track_y, track_w, track_h, themeCurrent.borderColor);
    fillRect(thumb_x, track_y, thumb_w, track_h, themeCurrent.textColor);
}

void drawButtons(menu_s* menu, bool emptyDir, int *out_basePos) {
    ThemeLayoutObject *layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_ButtonA];
    int basePos[2]={0};

    basePos[0] = layoutobj->posStart[0];
    basePos[1] = layoutobj->posStart[1];

    #ifdef __SWITCH__
    if (strcmp( menu->dirname, "sdmc:/") != 0)
    #else
    if (strcmp( menu->dirname, "/") != 0)
    #endif
    {
        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_ButtonBText];
        DrawTextFromLayoutRelative(ThemeLayoutId_ButtonBText, basePos[0], basePos[1], !emptyDir ? layoutobj->posStart : layoutobj->posEnd, basePos, themeCurrent.textColor, textGetString(StrId_Actions_Back), 'l');
        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_ButtonB];
        DrawTextFromLayoutRelative(ThemeLayoutId_ButtonB, basePos[0], basePos[1], layoutobj->posStart, basePos, themeCurrent.textColor, themeCurrent.buttonBText, 'l');
    }

    if(hbmenu_state == HBMENU_DEFAULT)
    {
        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_ButtonYText];
        DrawTextFromLayoutRelative(ThemeLayoutId_ButtonYText, basePos[0], basePos[1], layoutobj->posStart, basePos, themeCurrent.textColor, textGetString(StrId_NetLoader), 'r');
        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_ButtonY];
        DrawTextFromLayoutRelative(ThemeLayoutId_ButtonY, basePos[0], basePos[1], layoutobj->posStart, basePos, themeCurrent.textColor, themeCurrent.buttonYText, 'l');
        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_ButtonMText];
        DrawTextFromLayoutRelative(ThemeLayoutId_ButtonMText, basePos[0], basePos[1], layoutobj->posStart, basePos, themeCurrent.textColor, textGetString(StrId_ThemeMenu), 'r');
        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_ButtonM];
        DrawTextFromLayoutRelative(ThemeLayoutId_ButtonM, basePos[0], basePos[1], layoutobj->posStart, basePos, themeCurrent.textColor, themeCurrent.buttonMText, 'l');
    }

    out_basePos[0] = basePos[0];
    out_basePos[1] = basePos[1];
}

void menuUpdateNetloader(netloaderState *netloader_state) {
    bool enable_progress = 0;
    float progress = 0;
    char netloader_displaytext[260];
    char textbody[256];

    memset(netloader_displaytext, 0, sizeof(netloader_displaytext));
    memset(textbody, 0, sizeof(textbody));

    u32 ip = gethostid();

    if (ip == INADDR_LOOPBACK)
        snprintf(textbody, sizeof(textbody)-1, "%s", textGetString(StrId_NetLoaderOffline));
    else {
        if (!netloader_state->sock_connected)
            snprintf(textbody, sizeof(textbody)-1, textGetString(StrId_NetLoaderActive), ip&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF, NXLINK_SERVER_PORT);
        else {
            enable_progress = 1;
            progress = (float)netloader_state->filetotal / netloader_state->filelen;
            snprintf(textbody, sizeof(textbody)-1, textGetString(StrId_NetLoaderTransferring), netloader_state->filetotal/1024, netloader_state->filelen/1024);
        }
    }

    snprintf(netloader_displaytext, sizeof(netloader_displaytext)-1, "%s\n\n\n%s", textGetString(StrId_NetLoader), textbody);

    menuMsgBoxSetNetloaderState(1, netloader_displaytext, enable_progress, progress);
}

void menuLoop(void) {
    menuEntry_s* me;
    menu_s* menu = NULL;
    int i;
    int x, y, endy = 720;
    int curPos[2]={0};
    netloaderState netloader_state;
    ThemeLayoutObject *layoutobj = NULL;

    for (i=0; i<3; i++) {
        if (i==2) layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_BackWave];
        if (i==1) layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MiddleWave];
        if (i==0) layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_FrontWave];
        if (layoutobj->visible && layoutobj->size[1] >= 0 && layoutobj->size[1] <= 720-10) {
            endy = 720 - layoutobj->size[1] + 10;
            break;
        }
    }

    for (y=0; y<endy; y++) {
        for (x=0; x<1280; x+=4) {// don't draw bottom pixels as they are covered by the waves
            Draw4PixelsRaw(x, y, themeCurrent.backgroundColor);
        }
    }

    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_BackgroundImage];
    assetsDataEntry *data = NULL;
    assetsGetData(AssetId_background_image, &data);
    if (layoutobj->visible && data) drawImage(layoutobj->posStart[0], layoutobj->posStart[1], data->imageSize[0], endy < data->imageSize[1] ? endy : data->imageSize[1], data->buffer, data->imageMode);

    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_BackWave];
    if (layoutobj->visible) drawWave(0, menuTimer, themeCurrent.backWaveColor, layoutobj->size[1], 0.0, 3.0);
    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MiddleWave];
    if (layoutobj->visible) drawWave(1, menuTimer, themeCurrent.middleWaveColor, layoutobj->size[1], 2.0, 3.5);
    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_FrontWave];
    if (layoutobj->visible) drawWave(2, menuTimer, themeCurrent.frontWaveColor, layoutobj->size[1], 4.0, -2.5);
    menuTimer += 0.05;

    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_Logo];
    if(themeGlobalPreset == THEME_PRESET_DARK)
        assetsGetData(AssetId_hbmenu_logo_dark, &data);
    else assetsGetData(AssetId_hbmenu_logo_light, &data);

    if (layoutobj->visible) {
        if (!themeCurrent.logoColor_set)
            drawImage(layoutobj->posStart[0], layoutobj->posStart[1], data->imageSize[0], data->imageSize[1], data->buffer, data->imageMode);
        else {
            drawIcon(layoutobj->posStart[0], layoutobj->posStart[1], data->imageSize[0], data->imageSize[1], data->buffer, themeCurrent.logoColor);
        }
    }

    DrawTextFromLayout(ThemeLayoutId_HbmenuVersion, themeCurrent.textColor, VERSION);
    u32 statusXPos = drawStatus();

    #ifdef __SWITCH__
    AppletType at = appletGetAppletType();
    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_AttentionText];
    if (at != AppletType_Application && at != AppletType_SystemApplication && layoutobj->visible) {
        const char* appletMode = textGetString(StrId_AppletMode);
        u32 x_pos = GetTextXCoordinate(layoutobj->font, statusXPos, appletMode, 'r');
        DrawText(layoutobj->font, layoutobj->posType ? x_pos + layoutobj->posStart[0] : layoutobj->posStart[0], layoutobj->posStart[1], themeCurrent.attentionTextColor, appletMode);
    }
    const char* loaderInfo = envGetLoaderInfo();
    char loaderDisplay[128];
    const char* shownLoader = loaderInfo;
    if (loaderInfo && strncmp(loaderInfo, "ounce-hbloader", 14) == 0) {
        snprintf(loaderDisplay, sizeof(loaderDisplay), "clownce-hbloader%s", loaderInfo + 14);
        shownLoader = loaderDisplay;
    }
    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_LoaderInfo];
    if (shownLoader && layoutobj->visible) {
        u32 x_pos = layoutobj->posStart[0];
        char* spacePos = strchr(shownLoader, ' ');
        if (spacePos) {
            char tempbuf[64] = {0};
            size_t tempsize = spacePos - shownLoader + 1;
            if (tempsize > sizeof(tempbuf)-1) tempsize = sizeof(tempbuf)-1;
            memcpy(tempbuf, shownLoader, tempsize);
            x_pos = GetTextXCoordinate(layoutobj->font, layoutobj->posEnd[0], tempbuf, 'r');
        }
        DrawText(layoutobj->font, x_pos, layoutobj->posStart[1], themeCurrent.textColor, shownLoader);
    }
    #endif

    #ifdef PERF_LOG_DRAW//Seperate from the PERF_LOG define since this might affect perf.
    extern u64 g_tickdiff_frame;

    char tmpstr[64];

    snprintf(tmpstr, sizeof(tmpstr)-1, "%lu", g_tickdiff_frame);
    DrawTextFromLayout(ThemeLayoutId_LogInfo, themeCurrent.textColor, tmpstr);
    #endif

    memset(&netloader_state, 0, sizeof(netloader_state));
    netloaderGetState(&netloader_state);

    if(hbmenu_state == HBMENU_DEFAULT && netloader_state.activated) {
        hbmenu_state = HBMENU_NETLOADER_ACTIVE;

        menuCloseMsgBox();
        menuCreateMsgBox(780,300, "");
    } else if(hbmenu_state == HBMENU_NETLOADER_ACTIVE && !netloader_state.activated && !netloader_state.launch_app) {
        hbmenu_state = HBMENU_DEFAULT;
        menuScan(".");//Reload the menu since netloader may have deleted the NRO if the transfer aborted.

        menuCloseMsgBox();
        menuMsgBoxSetNetloaderState(0, NULL, 0, 0);
    }

    if (netloader_state.errormsg[0]) {
        menuCloseMsgBox();
        menuCreateMsgBox(780,300, netloader_state.errormsg);
    }

    if(hbmenu_state == HBMENU_NETLOADER_ACTIVE) {
        menuUpdateNetloader(&netloader_state);
    }

    menu = menuGetCurrent();

    if (menu->nEntries==0 || hbmenu_state == HBMENU_NETLOADER_ACTIVE)
    {
        if (hbmenu_state == HBMENU_NETLOADER_ACTIVE) {
            if (netloader_state.launch_app) {
                hbmenu_state = HBMENU_DEFAULT;
                menuCloseMsgBox();
                menuMsgBoxSetNetloaderState(0, NULL, 0, 0);
                menuCreateMsgBox(240,240,  textGetString(StrId_Loading));
                launchMenuEntryTask(netloader_state.me);
            }
        } else {
            DrawTextFromLayout(ThemeLayoutId_InfoMsg, themeCurrent.textColor, textGetString(StrId_NoAppsFound_Msg));
        }
        drawButtons(menu, true, curPos);
    }
    else
    {
        static int v = 0;
        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuListTiles];
        int entries_count = layoutobj->posEnd[0];
        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuList];

        // Gentle Realign only when not manually moving
        if (menu->slideSpeed == 0) {
            if (menu->holdXPos) {
                v = 0;
            }
            else if (menu->nEntries > entries_count) {
                int wanted_x = clamp(-menu->curEntry * layoutobj->posEnd[0], -(menu->nEntries - entries_count) * layoutobj->posEnd[0], 0);
                menu->xPos += v;
                v += (wanted_x - menu->xPos) / 3;
                v /= 2;
            }
            else {
                menu->xPos = v = 0;
            }
        }
        else {
            menu->xPos += menu->slideSpeed;

            if (abs(menu->slideSpeed) > 2) {
                // Slow down way faster when outside the normal bounds
                if (menu->xPos > 0 || menu->xPos < -(menu->nEntries) * layoutobj->posEnd[0]) {
                    menu->slideSpeed *= .5f;
                }
                else {
                    menu->slideSpeed *= .9f;
                }
            }
            else {
                menu->slideSpeed = 0;
            }

            menu->curEntry = clamp(roundf(-((float) menu->xPos / layoutobj->posEnd[0])), 0, menu->nEntries);
        }

        menuEntry_s *active_entry = NULL;

        // Draw menu entries
        for (me = menu->firstEntry, i = 0; me; me = me->next, i ++) {
            int entry_start_x = layoutobj->posStart[0] + i * layoutobj->posEnd[0];
            int entry_draw_x = entry_start_x + menu->xPos;

            int screen_width = 1280;
            if (entry_start_x >= (screen_width - menu->xPos))
                break;

            int is_active = i==menu->curEntry;

            if (is_active)
                active_entry = me;

            if (!is_active && entry_draw_x < -(layoutobj->posStart[0] + layoutobj->posEnd[0]))
                continue;

            drawEntry(me, entry_draw_x, is_active);
        }

        drawMenuScrollbar(menu);

        layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuTypeMsg];
        int getX=0;

        if (layoutobj->visible) {
            getX = GetTextXCoordinate(layoutobj->font, layoutobj->posStart[0], textGetString(StrId_ThemeMenu), 'r');

            if(hbmenu_state == HBMENU_THEME_MENU) {
                DrawText(layoutobj->font, getX, layoutobj->posStart[1], themeCurrent.textColor, textGetString(StrId_ThemeMenu));
            } else {
                //DrawText(interuiregular18, getX, 30 + 26 + 32 + 10, themeCurrent.textColor, textGetString(StrId_ThemeMenu));
                //DrawText(fontscale7, getX - 40,  30 + 26 + 32 + 10, themeCurrent.textColor, themeCurrent.buttonMText);
            }
        }

        if(active_entry != NULL) {
            const char *buttonstr = "";

            if (active_entry->type == ENTRY_TYPE_THEME)
                buttonstr = textGetString(StrId_Actions_Apply);
            else if (active_entry->type != ENTRY_TYPE_FOLDER)
                buttonstr = textGetString(StrId_Actions_Launch);
            else
                buttonstr = textGetString(StrId_Actions_Open);

            layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_ButtonAText];
            DrawTextFromLayoutRelative(ThemeLayoutId_ButtonAText, curPos[0], curPos[1], layoutobj->posStart, curPos, themeCurrent.textColor, buttonstr, 'l');
            layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_ButtonA];
            DrawTextFromLayoutRelative(ThemeLayoutId_ButtonA, curPos[0], curPos[1], layoutobj->posStart, curPos, themeCurrent.textColor, themeCurrent.buttonAText, 'l');
        }

        drawButtons(menu, false, curPos);

        if (active_entry && active_entry->type != ENTRY_TYPE_THEME) {
            const char *buttonstr = "";
            if (active_entry->starred)
                buttonstr = textGetString(StrId_Actions_Unstar);
            else
                buttonstr = textGetString(StrId_Actions_Star);

            layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_ButtonXText];
            DrawTextFromLayoutRelative(ThemeLayoutId_ButtonXText, curPos[0], curPos[1], layoutobj->posStart, curPos, themeCurrent.textColor, buttonstr, 'r');
            layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_ButtonX];
            DrawTextFromLayoutRelative(ThemeLayoutId_ButtonX, curPos[0], curPos[1], layoutobj->posStart, curPos, themeCurrent.textColor, themeCurrent.buttonXText, 'l');
        }

    }

    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuPath];
    if (layoutobj->visible) DrawTextTruncate(layoutobj->font, layoutobj->posStart[0], layoutobj->posStart[1], themeCurrent.textColor, menu->dirname, layoutobj->size[0], "...");

    menuDrawMsgBox();
#ifdef __SWITCH2__
    extern void drawMouseCursor(void);
    drawMouseCursor();
#endif
}
