/* SPDX-License-Identifier: ISC
 * Switch 2 HOME Menu UI for ounce-hbmenu.
 *
 * Based on ImDella/Switch2-HBmenu-Home, used with credit to its author.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <time.h>

#include <libconfig.h>
#include "switch2_ui.h"
#include "switch2_sfx.h"
#include "plugin_settings.h"
#include "sfx_pack.h"
#include "render.h"
#include "../common/netloader.h"
#ifdef __SWITCH2__
#include <switch/applets/swkbd.h>
#endif

/* Default PS4 sound pack URL (see switch2/sfx_pack.c; public archives are
 * Cloudflare-walled, so a self-hosted/GitHub zip is the reliable route). */
#define DL_URL_DEFAULT_TEXT "https://www.sounds-resource.com/download/26815/"

extern uint8_t *folder_icon_large;
extern uint8_t *invalid_icon_large;
extern uint8_t *theme_icon_large;

extern void themeMenuStartup(void);
extern void menuStartupCommon(void);
extern bool menuIsThemeMenuActive(void);

typedef enum {
    S2View_Carousel = 0,
    S2View_Grid = 1,
    S2View_Settings = 2,
} S2ViewMode;

typedef enum {
    S2Focus_Apps = 0,
    S2Focus_Pill = 1,
    S2Focus_Top = 2,
} S2FocusZone;

enum {
    PILL_NETLOADER = 0,
    PILL_FAVORITES = 1,
    PILL_ALL_SOFTWARE = 2,
    PILL_THEMES = 3,
    PILL_SETTINGS = 4,
    PILL_EXIT = 5,
    PILL_COUNT = 6,
};

enum {
    SET_SOUNDS = 0,
    SET_THEME = 1,
    SET_UI_STYLE = 2,
    SET_RING_SPEED = 3,
    SET_TRANSITIONS = 4,
    SET_DEFAULT_VIEW = 5,
    SET_SORT_ORDER = 6,
    SET_SHOW_SUBTITLE = 7,
    SET_HIDE_FOLDERS = 8,
    SET_PROFILE_NAME = 9,
    SET_PS4_SOUNDS = 10,
    SET_BGM = 11,
    SET_COUNT = 12,
};

typedef struct {
    int ui_sounds;      /* 0 = Off, 1 = On */
    int theme_mode;     /* 0 = Auto, 1 = Light, 2 = Dark */
    int ui_style;       /* 0 = Switch 2, 1 = PS4 */
    int ring_speed;     /* 0 = Slow, 1 = Normal, 2 = Fast, 3 = Static */
    int transitions;    /* 0 = Slide & Fade, 1 = Fade, 2 = Instant */
    int default_view;   /* 0 = Carousel, 1 = All Software */
    int sort_order;     /* 0 = Favorites First, 1 = Alphabetical */
    int show_subtitle;  /* 0 = Off, 1 = On */
    char display_name[33]; /* 32-char limit */
    char sound_pack_url[256]; /* empty = built-in URL */
    int ps4_sounds_on;        /* installed pack active (toggle in Themes) */
    int bgm_on;               /* looping background music */
} Switch2Settings;

static Switch2Settings s_cfg = {
    .ui_sounds = 1,
    .theme_mode = 0,
    .ui_style = 0,
    .ring_speed = 1,
    .transitions = 0,
    .default_view = 0,
    .sort_order = 0,
    .show_subtitle = 1,
    .display_name = "Switchstation 2",
    .sound_pack_url = "",
    .ps4_sounds_on = 1,
    .bgm_on = 1,
};
static ThemePreset s_system_preset = THEME_PRESET_DARK;
static S2ViewMode s_prev_view = S2View_Carousel;
static int s_prev_focus = 0;   /* S2FocusMode at settings entry */
static int s_prev_top_idx = 0;
static S2ViewMode s_last_view = S2View_Carousel;
static float s_trans_t = 1.0f;
static float s_ps4_boot_ms = 5000.0f;
static bool s_ps4_boot_chime_played = true;
static bool s_ps4_launch_active = false;
static float s_ps4_launch_ms = 0.0f;
static float s_ps4_launch_sx = 0.0f, s_ps4_launch_sy = 0.0f, s_ps4_launch_sw = 256.0f, s_ps4_launch_sh = 256.0f;
static menuEntry_s *s_ps4_launch_entry = NULL;
static int s_settings_sel = 0;

/* Exact cubic-bezier evaluator (zero bounce, zero overshoot) */
static float cubicBezierEval(float x1, float y1, float x2, float y2, float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    float u = x;
    for (int it = 0; it < 6; it++) {
        float iu = 1.0f - u;
        float bx = 3.0f * iu * iu * u * x1 + 3.0f * iu * u * u * x2 + u * u * u;
        float dx = 3.0f * iu * iu * x1 + 6.0f * iu * u * (x2 - x1) + 3.0f * u * u * (1.0f - x2);
        if (fabsf(dx) < 1e-5f) break;
        u -= (bx - x) / dx;
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;
    }
    float iu = 1.0f - u;
    return 3.0f * iu * iu * u * y1 + 3.0f * iu * u * u * y2 + u * u * u;
}
static float easeSoft(float t)    { return cubicBezierEval(0.4f, 0.0f, 0.2f, 1.0f, t); }
static float easeOutSoft(float t) { return cubicBezierEval(0.0f, 0.0f, 0.2f, 1.0f, t); }
static float easeInSoft(float t)  { return cubicBezierEval(0.4f, 0.0f, 1.0f, 1.0f, t); }
static float segProg(float ms, float a, float b) {
    if (ms <= a) return 0.0f;
    if (ms >= b) return 1.0f;
    return (ms - a) / (b - a);
}

static float transSlideOffset(void);
static void triggerTransition(void);

/* Layout constants (logical 1280x720 coordinates, scaled natively to 1080p/4K) */
#define SCREEN_W 1280.0f
#define SCREEN_H 720.0f

#define CAROUSEL_START_X 96.0f
#define CAROUSEL_TILE_Y  196.0f
#define CAROUSEL_TILE_SZ 256.0f
#define CAROUSEL_STRIDE  272.0f

#define GRID_COLS        5
#define GRID_START_X     160.0f
#define GRID_START_Y     166.0f
#define GRID_TILE_SZ     154.0f
#define GRID_STRIDE_X    198.0f
#define GRID_STRIDE_Y    172.0f
#define PS4_LIB_X     77.0f
#define PS4_LIB_Y     172.0f
#define PS4_CARD_W    366.0f
#define PS4_CARD_H    170.0f
#define PS4_CARD_SX   382.0f
#define PS4_CARD_SY   186.0f
#define GRID_CLIP_TOP    150.0f
#define GRID_CLIP_BOTTOM 506.0f

#define PILL_W           540.0f
#define PILL_H           86.0f
#define PILL_X           ((SCREEN_W - PILL_W) * 0.5f)
#define PILL_Y           516.0f
#define PILL_CENTER_Y    (PILL_Y + PILL_H * 0.5f)
#define PILL_BTN_STRIDE  82.0f
#define PILL_BTN_RADIUS  27.0f

#define MAX_VISIBLE_ENTRIES 1024

static S2ViewMode s_view = S2View_Carousel;
static S2FocusZone s_focus = S2Focus_Apps;
static int s_pill_idx = 0;
static int s_top_idx = 0;
static bool s_favorites_only = false;
static bool s_exit_requested = false;

enum { SETTINGS_TAB_GENERAL = 0, SETTINGS_TAB_PLUGINS = 1, SETTINGS_TAB_THEMES = 2 };
static int s_settings_tab = SETTINGS_TAB_GENERAL;
static int s_plugin_view = 0; /* 0 = plugin list, 1 = one plugin's options, 2 = game mod packs */
static int s_plugin_sel = 0;
static int s_plugin_option_sel = 0;
static HbPluginSettingsList s_plugin_settings;
static bool s_plugin_settings_loaded = false;
static HbModPackList s_mod_packs;
static int s_mod_pack_sel = 0;
#define MAX_THEMES 16
static char s_theme_paths[MAX_THEMES][PATH_MAX];
static char s_theme_names[MAX_THEMES][96];
static int s_theme_count = 0;
static int s_theme_sel = 0;
static char s_theme_status[160] = "";
static u64 s_held_buttons = 0;

void switch2SetHeldButtons(u64 held) {
    s_held_buttons = held;
}

/* PS4 boundary behavior: hitting the first/last item plays cursor_end once;
 * staying held against the wall stays silent until the button is released. */
static void boundaryEdgeTick(u64 down) {
    static u64 s_wall_latched = 0;
    /* clear latches for directions no longer held */
    const u64 dir = down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight |
                            HidNpadButton_AnyUp | HidNpadButton_AnyDown);
    if (!dir)
        return;
    s_wall_latched &= s_held_buttons;
    if (s_wall_latched & dir)
        return;
    s_wall_latched |= dir;
    /* Boundary hits stay silent: PS4 Orbis plays cursor_end here, but the
     * cue was removed from the pack at the user's request. */
}
static bool s_themes_loaded = false;
static bool s_folders_dirty = false;
static char s_settings_return_dir[PATH_MAX];

/* Visible filtered entry list (rebuilt cleanly per frame/input) */
static menuEntry_s *s_visible[MAX_VISIBLE_ENTRIES];
static int s_visible_indices[MAX_VISIBLE_ENTRIES]; /* index in menu_s */
static int s_visible_count = 0;
static int s_sel = 0; /* index in s_visible; == s_visible_count means "All Software" end tile in carousel */

/* Smooth 60fps animation state */
static float s_scroll_x = 0.0f;
static float s_grid_scroll_y = 0.0f;
static float s_app_pop = 1.0f;
static float s_app_pop_vel = 0.0f;
static float s_focus_pos = 0.0f;
static float s_last_focus_pos = 0.0f;
static float s_dust_parallax = 0.0f;
static float s_ps4_panel_t = 1.0f;
static float s_topbar_t = 0.0f;
static float s_pill_pop = 1.0f;
static float s_pill_pop_vel = 0.0f;
static int s_last_sel = -1;
static int s_last_pill = -1;
static S2FocusZone s_last_focus = S2Focus_Apps;

/* Touch state */
static bool s_touch_active = false;
static bool s_touch_is_tap = false;
static int s_touch_start_x = 0, s_touch_start_y = 0;
static int s_touch_prev_x = 0, s_touch_prev_y = 0;
static float s_touch_vel_x = 0.0f;

static bool isDarkTheme(void) {
    /* Infer dark/light from current background luminance so custom .cfg themes work too */
    color_t bg = themeCurrent.backgroundColor;
    int lum = (bg.r * 299 + bg.g * 587 + bg.b * 114) / 1000;
    return lum < 128;
}

static uint32_t makeRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static color_t makeColorMuted(color_t base, uint8_t alpha) {
    return MakeColor(base.r, base.g, base.b, alpha);
}

/* DrawText() uses y as the baseline, and GetTextDimensions() returns height=0
 * for single-line text. This returns half the visual cap-height so text drawn
 * at (center_y + fontBaselineOffset(font)) is visually centered on center_y. */
static float fontBaselineOffset(u32 font) {
    switch (font) {
    case interuiregular14: return 5.5f;
    case interuiregular18: return 7.0f;
    case interuimedium20:  return 8.5f;
    case interuimedium30:  return 11.5f;
    default:               return 7.0f;
    }
}

static float ringTimer(void) {
    switch (s_cfg.ring_speed) {
    case 0:  return (float)menuTimer * 0.45f; /* Slow */
    case 1:  return (float)menuTimer * 1.0f;  /* Normal */
    case 2:  return (float)menuTimer * 2.0f;  /* Fast */
    case 3:  return 0.0f;                     /* Static */
    default: return (float)menuTimer;
    }
}

static void getSettingsPath(char *out, size_t out_size) {
    snprintf(out, out_size, "%s/config/nx-hbmenu/switch2_settings.cfg", menuGetRootBasePath());
}

static void switch2ApplyThemeSetting(void) {
    ThemePreset target = s_system_preset;
    if (s_cfg.theme_mode == 1) target = THEME_PRESET_LIGHT;
    else if (s_cfg.theme_mode == 2) target = THEME_PRESET_DARK;
    themeStartup(target);
    menuStartupCommon();
}

static void switch2SettingsSave(void) {
    config_t cfg;
    config_init(&cfg);
    config_setting_t *root = config_root_setting(&cfg);

    config_setting_t *s;
    s = config_setting_add(root, "ui_sounds", CONFIG_TYPE_INT);     if (s) config_setting_set_int(s, s_cfg.ui_sounds);
    s = config_setting_add(root, "theme_mode", CONFIG_TYPE_INT);    if (s) config_setting_set_int(s, s_cfg.theme_mode);
    s = config_setting_add(root, "ui_style", CONFIG_TYPE_INT);      if (s) config_setting_set_int(s, s_cfg.ui_style);
    s = config_setting_add(root, "ring_speed", CONFIG_TYPE_INT);    if (s) config_setting_set_int(s, s_cfg.ring_speed);
    s = config_setting_add(root, "transitions", CONFIG_TYPE_INT);   if (s) config_setting_set_int(s, s_cfg.transitions);
    s = config_setting_add(root, "default_view", CONFIG_TYPE_INT);  if (s) config_setting_set_int(s, s_cfg.default_view);
    s = config_setting_add(root, "sort_order", CONFIG_TYPE_INT);    if (s) config_setting_set_int(s, s_cfg.sort_order);
    s = config_setting_add(root, "show_subtitle", CONFIG_TYPE_INT); if (s) config_setting_set_int(s, s_cfg.show_subtitle);
    s = config_setting_add(root, "display_name", CONFIG_TYPE_STRING); if (s) config_setting_set_string(s, s_cfg.display_name);
    s = config_setting_add(root, "sound_pack_url", CONFIG_TYPE_STRING); if (s) config_setting_set_string(s, s_cfg.sound_pack_url);
    s = config_setting_add(root, "ps4_sounds_on", CONFIG_TYPE_INT); if (s) config_setting_set_int(s, s_cfg.ps4_sounds_on);
    s = config_setting_add(root, "bgm_on", CONFIG_TYPE_INT); if (s) config_setting_set_int(s, s_cfg.bgm_on);

    char path[PATH_MAX + 64];
    getSettingsPath(path, sizeof(path));
    config_write_file(&cfg, path);
    config_destroy(&cfg);
}

static void switch2SettingsLoad(void) {
    config_t cfg;
    config_init(&cfg);
    char path[PATH_MAX + 64];
    getSettingsPath(path, sizeof(path));
    if (config_read_file(&cfg, path) == CONFIG_TRUE) {
        int v = 0;
        if (config_lookup_int(&cfg, "ui_sounds", &v))     s_cfg.ui_sounds     = (v != 0) ? 1 : 0;
        if (config_lookup_int(&cfg, "theme_mode", &v))    s_cfg.theme_mode    = (v >= 0 && v <= 2) ? v : 0;
        if (config_lookup_int(&cfg, "ui_style", &v))      s_cfg.ui_style      = (v != 0) ? 1 : 0;
        if (config_lookup_int(&cfg, "ring_speed", &v))    s_cfg.ring_speed    = (v >= 0 && v <= 3) ? v : 1;
        if (config_lookup_int(&cfg, "transitions", &v))   s_cfg.transitions   = (v >= 0 && v <= 2) ? v : 0;
        if (config_lookup_int(&cfg, "default_view", &v))  s_cfg.default_view  = (v != 0) ? 1 : 0;
        if (config_lookup_int(&cfg, "sort_order", &v))    s_cfg.sort_order    = (v != 0) ? 1 : 0;
        if (config_lookup_int(&cfg, "show_subtitle", &v)) s_cfg.show_subtitle = (v != 0) ? 1 : 0;
        const char *dname = NULL;
        if (config_lookup_string(&cfg, "display_name", &dname) && dname[0])
            snprintf(s_cfg.display_name, sizeof(s_cfg.display_name), "%.32s", dname);
        const char *packurl = NULL;
        if (config_lookup_string(&cfg, "sound_pack_url", &packurl))
            snprintf(s_cfg.sound_pack_url, sizeof(s_cfg.sound_pack_url), "%.255s", packurl);
        if (config_lookup_int(&cfg, "ps4_sounds_on", &v)) s_cfg.ps4_sounds_on = (v != 0) ? 1 : 0;
        if (config_lookup_int(&cfg, "bgm_on", &v)) s_cfg.bgm_on = (v != 0) ? 1 : 0;
    }
    config_destroy(&cfg);
}

void switch2UiInit(ThemePreset system_preset) {
    s_system_preset = system_preset;
    switch2SettingsLoad();
    switch2SfxSetEnabled(s_cfg.ui_sounds != 0);
    if (s_cfg.theme_mode != 0)
        switch2ApplyThemeSetting();
    s_view = (s_cfg.default_view == 1) ? S2View_Grid : S2View_Carousel;
    if (s_cfg.sound_pack_url[0])
        sfxPackSetUrl(s_cfg.sound_pack_url);
    if (s_cfg.ui_style == 1 && sfxPackInstalled()) {
        /* Load the installed pack (cues + bgm) whenever it exists; the
         * Themes toggle only governs the SFX cues, bgm_on governs the music,
         * and the startup chime follows the cues. */
        sfxPackLoadActive();
        if (!s_cfg.ps4_sounds_on)
            switch2SfxResetSynth(); /* cues off: built-in ticks, music stays */
        switch2SfxSetBgmEnabled(s_cfg.bgm_on != 0);
        if (s_cfg.ps4_sounds_on && (s_cfg.ui_style != 1 || s_cfg.transitions == 2))
            switch2SfxPlay(S2Sfx_Startup);
        /* On-device audio diagnostic, visible in Settings -> Themes */
        if (switch2SfxBgmActive())
            snprintf(s_theme_status, sizeof(s_theme_status), "Audio: BGM playing");
        else
            snprintf(s_theme_status, sizeof(s_theme_status),
                     "Audio: BGM off (%s)", s_cfg.bgm_on ? "bgm.wav missing" : "disabled");
    } else if (s_cfg.ui_style == 1) {
        snprintf(s_theme_status, sizeof(s_theme_status),
                 "Audio: pack not installed - drop ps4-orbis.zip into themes/");
    }
    s_last_view = s_view;
    s_trans_t = 1.0f;
    if (s_cfg.ui_style == 1 && s_cfg.transitions != 2) {
        s_ps4_boot_ms = 0.0f;
        s_ps4_boot_chime_played = false;
    } else {
        s_ps4_boot_ms = 5000.0f;
        s_ps4_boot_chime_played = true;
    }
    s_ps4_launch_active = false;
    s_ps4_launch_ms = 0.0f;
    s_ps4_launch_entry = NULL;
}

static bool hasEndAllSoftwareTile(void) {
    return s_view == S2View_Carousel &&
           !menuIsThemeMenuActive() && s_visible_count > 0;
}

typedef struct {
    menuEntry_s *me;
    int orig_idx;
} VisiblePair;

static int cmpVisibleAlpha(const void *a, const void *b) {
    const VisiblePair *pa = (const VisiblePair *)a;
    const VisiblePair *pb = (const VisiblePair *)b;
    /* Keep folders grouped before files, then pure case-insensitive alphabetical */
    int fa = (pa->me->type == ENTRY_TYPE_FOLDER) ? 0 : 1;
    int fb = (pb->me->type == ENTRY_TYPE_FOLDER) ? 0 : 1;
    if (fa != fb) return fa - fb;
    return strcasecmp(pa->me->name, pb->me->name);
}

static void rebuildVisibleList(menu_s *menu) {
    s_visible_count = 0;
    if (!menu) return;

    bool filter_fav = s_favorites_only && !menuIsThemeMenuActive();
    VisiblePair pairs[MAX_VISIBLE_ENTRIES];
    int idx = 0;
    for (menuEntry_s *me = menu->firstEntry; me && s_visible_count < MAX_VISIBLE_ENTRIES; me = me->next, idx++) {
        if (filter_fav && !me->starred)
            continue;
        pairs[s_visible_count].me = me;
        pairs[s_visible_count].orig_idx = idx;
        s_visible_count++;
    }

    if (s_cfg.sort_order == 1 && !menuIsThemeMenuActive() && s_visible_count > 1) {
        qsort(pairs, (size_t)s_visible_count, sizeof(VisiblePair), cmpVisibleAlpha);
    }

    for (int i = 0; i < s_visible_count; i++) {
        s_visible[i] = pairs[i].me;
        s_visible_indices[i] = pairs[i].orig_idx;
    }

    int max_sel = hasEndAllSoftwareTile() ? s_visible_count : (s_visible_count > 0 ? s_visible_count - 1 : 0);
    if (s_sel > max_sel) s_sel = max_sel;
    if (s_sel < 0) s_sel = 0;

    /* Do not write menu->curEntry here: this list is a view of the menu, and
     * a stale s_sel must not overwrite a newer menu selection before input. */
}

static void syncSelectionFromMenu(menu_s *menu) {
    rebuildVisibleList(menu);
    if (!menu || s_visible_count == 0) {
        s_sel = 0;
        return;
    }
    for (int i = 0; i < s_visible_count; i++) {
        if (s_visible_indices[i] == menu->curEntry) {
            s_sel = i;
            return;
        }
    }
    if (s_sel >= s_visible_count)
        s_sel = s_visible_count - 1;
    menu->curEntry = s_visible_indices[s_sel];
}

static float pillButtonCenterX(int index) {
    float start_x = SCREEN_W * 0.5f - (PILL_COUNT - 1) * 0.5f * PILL_BTN_STRIDE;
    return start_x + index * PILL_BTN_STRIDE;
}

static void triggerPillAction(int idx) {
    menu_s *menu = menuGetCurrent();
    switch (idx) {
    case PILL_NETLOADER:
        if (!menuIsThemeMenuActive()) {
            switch2SfxPlay(S2Sfx_DialogOpen);
            launchMenuNetloaderTask();
        }
        break;
    case PILL_FAVORITES:
        if (!menuIsThemeMenuActive()) {
            switch2SfxPlay(S2Sfx_Confirm);
            s_favorites_only = !s_favorites_only;
            s_sel = 0;
            s_scroll_x = 0.0f;
            s_grid_scroll_y = 0.0f;
            rebuildVisibleList(menu);
            if (s_visible_count > 0)
                s_focus = S2Focus_Apps;
            triggerTransition();
        }
        break;
    case PILL_ALL_SOFTWARE:
        if (!menuIsThemeMenuActive()) {
            switch2SfxPlay(S2Sfx_Confirm);
            s_view = (s_view == S2View_Carousel) ? S2View_Grid : S2View_Carousel;
            if (s_sel >= s_visible_count && s_visible_count > 0)
                s_sel = s_visible_count - 1;
            s_focus = S2Focus_Apps;
            triggerTransition();
        }
        break;
    case PILL_THEMES:
        switch2SfxPlay(S2Sfx_Confirm);
        if (menuIsThemeMenuActive()) {
            launchMenuBackTask();
            s_view = S2View_Carousel;
            s_focus = S2Focus_Apps;
            s_sel = 0;
            s_scroll_x = 0.0f;
        } else {
            s_view = S2View_Carousel;
            s_focus = S2Focus_Apps;
            themeMenuStartup();
            syncSelectionFromMenu(menuGetCurrent());
        }
        triggerTransition();
        break;
    case PILL_SETTINGS:
        switch2SfxPlay(S2Sfx_EnterMenu);
        s_prev_focus = s_focus;
        s_prev_top_idx = s_top_idx;
        s_prev_view = (s_view == S2View_Grid) ? S2View_Grid : S2View_Carousel;
        s_view = S2View_Settings;
        s_settings_sel = 0;
        s_settings_tab = SETTINGS_TAB_GENERAL;
        s_plugin_view = 0;
        s_plugin_sel = 0;
        s_plugin_option_sel = 0;
        break;
    case PILL_EXIT:
        switch2SfxPlay(S2Sfx_Back);
        s_exit_requested = true;
        break;
    }
}

/* --- Drawing helpers ------------------------------------------------------ */

static void drawIconAlpha(int x, int y, int width, int height, const uint8_t *image, color_t color) {
    renderImage(image, x, y, width, height, RenderImageFormat_AlphaMask, color.abgr | 0xff000000u);
}

static void drawHorizontalBattery(float x, float y, uint32_t charge, bool charging, color_t fg, bool dark) {
    /* Authentic Switch 2 horizontal battery icon (36x19 + 3px nub) */
    uint32_t col = fg.abgr | 0xff000000u;
    uint32_t bg = dark ? makeRGBA(24, 24, 26, 255) : makeRGBA(235, 235, 238, 255);
    renderRoundedRectF(x, y, 36.0f, 19.0f, 4.0f, col);
    renderRoundedRectF(x + 2.2f, y + 2.2f, 31.6f, 14.6f, 2.2f, bg);
    /* Right positive terminal nub */
    renderRoundedRectF(x + 36.5f, y + 5.5f, 3.0f, 8.0f, 1.5f, col);
    /* Charge fill bar */
    float fill_max = 27.6f;
    float fill_w = fill_max * ((float)charge / 100.0f);
    if (fill_w < 2.0f && charge > 0) fill_w = 2.0f;
    uint32_t fill_col = col;
    if (charging)
        fill_col = makeRGBA(82, 224, 96, 255);
    else if (charge <= 15)
        fill_col = makeRGBA(236, 56, 72, 255);
    if (fill_w > 0.0f)
        renderRoundedRectF(x + 4.2f, y + 4.2f, fill_w, 10.6f, 1.5f, fill_col);
}

static void drawTopBar(menu_s *menu) {
    bool dark = isDarkTheme();
    color_t txt = themeCurrent.textColor;
    const float bar_cy = 60.0f;

    /* Left: Switch 2 circular profile-style icon (with crisp white rim) */
    float cx = 74.0f, cy = bar_cy, br = 24.0f;
    uint32_t badge_bg = dark ? makeRGBA(46, 48, 56, 255) : makeRGBA(255, 255, 255, 255);
    uint32_t badge_rim = dark ? makeRGBA(190, 194, 204, 255) : makeRGBA(185, 190, 200, 255);
    renderSoftShadowF(cx - br, cy - br + 2.0f, br * 2.0f, br * 2.0f, br, 6.0f, dark ? 55 : 26);
    renderCircleF(cx, cy, br + 2.0f, badge_rim);
    renderCircleF(cx, cy, br, badge_bg);
    /* Mini Switch 2 Joy-Con dual icon inside circular badge */
    renderRoundedRectF(cx - 11.0f, cy - 10.5f, 9.5f, 21.0f, 4.75f, makeRGBA(0, 195, 227, 255));
    renderRoundedRectF(cx + 1.5f, cy - 10.5f, 9.5f, 21.0f, 4.75f, makeRGBA(255, 69, 84, 255));

    const char *title = "Homebrew Menu";
    uint32_t tw = 0, th = 0;
    GetTextDimensions(interuiregular18, title, &tw, &th);
    DrawText(interuiregular18, 114, (uint32_t)(bar_cy + fontBaselineOffset(interuiregular18)), makeColorMuted(txt, dark ? 210 : 200), title);

    float tag_x = 114.0f + (float)tw + 14.0f;

    /* Context pill tag (Theme Menu / Favorites / All Software / Subfolder) */
    const char *tag_text = NULL;
    uint32_t tag_bg = dark ? makeRGBA(52, 54, 62, 255) : makeRGBA(218, 221, 228, 255);
    color_t tag_fg = txt;
    if (menuIsThemeMenuActive()) {
        tag_text = "Themes";
        tag_bg = makeRGBA(124, 92, 255, 255);
        tag_fg = MakeColor(255, 255, 255, 255);
    } else if (s_favorites_only) {
        tag_text = "Favorites";
        tag_bg = makeRGBA(240, 162, 24, 255);
        tag_fg = MakeColor(255, 255, 255, 255);
    } else if (s_view == S2View_Grid) {
        tag_text = "All Software";
        tag_bg = makeRGBA(230, 40, 58, 255);
        tag_fg = MakeColor(255, 255, 255, 255);
    } else if (menu && strcmp(menu->dirname, menuGetRootPath()) != 0) {
        const char *slash = strrchr(menu->dirname, '/');
        tag_text = (slash && slash[1]) ? (slash + 1) : menu->dirname;
    }

    if (tag_text && tag_text[0]) {
        uint32_t pw = 0, ph = 0;
        GetTextDimensions(interuiregular14, tag_text, &pw, &ph);
        float pill_w = (float)pw + 22.0f;
        if (pill_w > 220.0f) pill_w = 220.0f;
        renderRoundedRectF(tag_x, bar_cy - 13.0f, pill_w, 26.0f, 13.0f, tag_bg);
        DrawTextTruncate(interuiregular14, (uint32_t)(tag_x + 11.0f), (uint32_t)(bar_cy + fontBaselineOffset(interuiregular14)),
                         tag_fg, tag_text, (uint32_t)(pill_w - 22.0f), "...");
    }

    /* Right side (all cleanly vertically centered on bar_cy, matching Switch 2 HOME):
     * [Applet Mode]   HH:MM   [Wi-Fi]   [Horizontal Battery] */
    float right_x = 1232.0f;

    /* Horizontal Battery */
    uint32_t batteryCharge = 100;
    bool isCharging = false;
    if (powerGetDetails(&batteryCharge, &isCharging)) {
        if (batteryCharge > 100) batteryCharge = 100;
        right_x -= 40.0f;
        drawHorizontalBattery(right_x, bar_cy - 9.5f, batteryCharge, isCharging, txt, dark);
        right_x -= 22.0f;
    }

    /* Network icon */
    bool netFlag = false, tempFlag = false;
    s32 temperature = 0;
    AssetId netId = AssetId_wifi_none_icon;
    if (statusGet(&netFlag, &netId, &tempFlag, &temperature) && netFlag) {
        assetsDataEntry *netData = NULL;
        assetsGetData(netId, &netData);
        if (netData) {
            int nw = netData->imageSize[0];
            int nh = netData->imageSize[1];
            right_x -= (float)nw;
            drawIconAlpha((int)right_x, (int)(bar_cy - (float)nh * 0.5f), nw, nh, netData->buffer, txt);
            right_x -= 20.0f;
        }
    }

    /* Clock (HH:MM) */
    time_t unixTime = time(NULL);
    struct tm *tmNow = localtime(&unixTime);
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", tmNow->tm_hour, tmNow->tm_min);
    uint32_t cw = 0, ch = 0;
    GetTextDimensions(interuimedium20, timeStr, &cw, &ch);
    right_x -= (float)cw;
    DrawText(interuimedium20, (uint32_t)right_x, (uint32_t)(bar_cy + fontBaselineOffset(interuimedium20)), txt, timeStr);
    right_x -= 18.0f;

    /* Applet Mode pill badge (only when running in Applet Mode) */
    #ifdef __SWITCH__
    AppletType at = appletGetAppletType();
    if (at != AppletType_Application && at != AppletType_SystemApplication) {
        const char *appletStr = "Applet Mode";
        uint32_t aw = 0, ah = 0;
        GetTextDimensions(interuiregular14, appletStr, &aw, &ah);
        float pw = (float)aw + 20.0f;
        right_x -= pw;
        renderRoundedRectF(right_x, bar_cy - 13.0f, pw, 26.0f, 13.0f, makeRGBA(225, 48, 66, 255));
        DrawText(interuiregular14, (uint32_t)(right_x + 10.0f), (uint32_t)(bar_cy + fontBaselineOffset(interuiregular14)),
                 MakeColor(255, 255, 255, 255), appletStr);
    }
    #endif
}

static const uint8_t g_ps4_disc_icon_rgba[16384] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,42,255,255,255,121,255,255,255,159,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,158,255,255,255,83,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,15,
    255,255,255,166,255,255,255,254,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,147,255,255,255,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,15,255,255,255,211,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,148,
    255,255,255,1,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,166,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,148,255,255,255,1,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,42,255,255,255,254,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,148,255,255,255,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,122,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,207,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,232,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,148,255,255,255,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,158,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,208,255,255,255,2,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,40,255,255,255,231,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,148,
    255,255,255,1,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,38,
    255,255,255,229,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,154,255,255,255,1,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,40,255,255,255,231,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,147,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,40,255,255,255,231,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,82,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,40,255,255,255,231,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,158,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,38,
    255,255,255,229,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,40,255,255,255,232,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,3,
    255,255,255,56,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,55,
    255,255,255,3,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,45,255,255,255,209,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,217,255,255,255,48,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,15,255,255,255,225,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,228,255,255,255,17,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,122,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,120,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,165,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,164,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,225,255,255,255,91,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,91,255,255,255,226,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,170,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,170,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,170,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,170,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,170,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,170,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,170,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,170,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,170,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,170,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,170,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,170,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,170,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,170,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,170,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,170,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,170,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,170,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,170,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,170,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,176,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,176,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,248,255,255,255,176,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,176,255,255,255,248,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,157,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,156,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,90,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,88,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,3,255,255,255,189,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,187,255,255,255,3,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,5,255,255,255,126,
    255,255,255,225,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,224,
    255,255,255,124,255,255,255,5,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,16,255,255,255,74,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,73,255,255,255,15,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,34,
    255,255,255,231,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,230,
    255,255,255,34,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,145,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,144,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,145,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,143,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,34,
    255,255,255,230,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,230,
    255,255,255,33,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,15,255,255,255,73,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,73,255,255,255,14,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,158,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,208,255,255,255,2,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,2,255,255,255,209,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,157,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,122,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,208,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,208,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,120,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,41,255,255,255,254,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,253,255,255,255,40,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,165,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,162,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,14,255,255,255,210,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,209,255,255,255,14,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,14,
    255,255,255,164,255,255,255,254,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,253,255,255,255,162,
    255,255,255,14,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,41,255,255,255,119,255,255,255,159,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,159,255,255,255,118,255,255,255,40,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const int g_ps4_disc_icon_rgba_W=64, g_ps4_disc_icon_rgba_H=64;

static void drawAppTile(menuEntry_s *me, float x, float y, float w, float h,
                        bool is_focused, float pop_amount) {
    const bool ps4 = s_cfg.ui_style == 1;
    bool dark = isDarkTheme();

    const uint8_t *img = NULL;
    enum RenderImageFormat fmt = ps4 ? RenderImageFormat_Rgb24 : RenderImageFormat_Rgb24Rounded;
    int img_w = 256, img_h = 256;
    const ThemeLayoutObject *largeobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuActiveEntryIcon];
    if (largeobj->size[0] > 0 && largeobj->size[1] > 0) {
        img_w = largeobj->size[0];
        img_h = largeobj->size[1];
    }
    if (me->icon_gfx) {
        img = me->icon_gfx;
        fmt = (me->icon_mode == IMAGE_MODE_RGB24)
                  ? (ps4 ? RenderImageFormat_Rgb24 : RenderImageFormat_Rgb24Rounded)
                  : (ps4 ? RenderImageFormat_Rgba32 : RenderImageFormat_Rgba32Rounded);
    } else if (me->type == ENTRY_TYPE_FOLDER) {
        img = folder_icon_large;
        fmt = ps4 ? RenderImageFormat_Rgba32 : RenderImageFormat_Rgba32Rounded;
    } else if (me->type == ENTRY_TYPE_THEME) {
        img = theme_icon_large;
        fmt = ps4 ? RenderImageFormat_Rgba32 : RenderImageFormat_Rgba32Rounded;
    } else {
        img = invalid_icon_large;
        fmt = ps4 ? RenderImageFormat_Rgba32 : RenderImageFormat_Rgba32Rounded;
    }

    if (ps4) {
        /* ~50% opacity while the settings bar is out (inactive) */
        const uint8_t dim = (uint8_t)(255.0f - 127.0f * s_topbar_t);
        const uint32_t tint = ((uint32_t)dim << 24) | 0x00ffffffu;
        float t = (h - 153.0f) / 161.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        const float icon_h = (w < h) ? w : h;
        const float label_h = h - icon_h;
        uint8_t bright = (uint8_t)(235.0f + 20.0f * t);
        renderRectF(x, y, w, h, makeRGBA(10 * bright / 255, 58 * bright / 255,
                                         158 * bright / 255, dim));
        if (t > 0.02f)
            renderRectF(x, y + icon_h, w, label_h, makeRGBA(7, 42, 122, dim));

        if (me->type == ENTRY_TYPE_FOLDER) {
            int preview_count = 0;
            DIR *dir = opendir(me->path);
            if (dir) {
                struct dirent *dp;
                while ((dp = readdir(dir)) && preview_count < 4) {
                    if (dp->d_name[0] == '.')
                        continue;
                    preview_count++;
                }
                closedir(dir);
            }
            const float qw = (w - 3.0f) * 0.5f;
            const float qh = (icon_h - 3.0f) * 0.5f;
            const int count = (preview_count > 0) ? preview_count : 1;
            for (int q = 0; q < count; q++) {
                const float qx = x + (q & 1) * (qw + 3.0f);
                const float qy = y + (q >> 1) * (qh + 3.0f);
                renderRectF(qx, qy, qw, qh, makeRGBA(8, 48, 134, dim));
                if (img)
                    renderImageScaledF(img, img_w, img_h, qx + 8.0f, qy + 8.0f,
                                       qw - 16.0f, qh - 16.0f, fmt, tint);
            }
        } else if (img) {
            const float inset = 0.0f;
            renderImageScaledF(img, img_w, img_h, x + inset, y + inset,
                               w - inset * 2.0f, icon_h - inset * 2.0f, fmt, tint);
        }

        if (t > 0.5f) {
            const char *action = me->type == ENTRY_TYPE_FOLDER ? "Open" : "Start";
            uint32_t aw = 0, ah = 0;
            GetTextDimensions(interuimedium20, action, &aw, &ah);
            DrawText(interuimedium20, (uint32_t)(x + (w - (float)aw) * 0.5f),
                     (uint32_t)(y + h - 14.0f),
                     MakeColor(255, 255, 255,
                               (uint8_t)((110.0f + 145.0f * t) * dim / 255.0f)), action);
            /* Disc/cartridge logo: full strip height with a few px padding (PS4 style) */
            const float disc_sz = label_h - 6.0f;
            if (disc_sz > 4.0f) {
                const uint8_t alpha = (uint8_t)((110.0f + 145.0f * t) * dim / 255.0f);
                const float dx_i = x + w - disc_sz - 8.0f;
                const float dy_i = y + h - label_h + 3.0f;
                if (me->game_platform == 2) {
                    renderImageScaledF(g_ps4_disc_icon_rgba, g_ps4_disc_icon_rgba_W,
                                       g_ps4_disc_icon_rgba_H, dx_i, dy_i, disc_sz, disc_sz,
                                       RenderImageFormat_Rgba32, makeRGBA(230, 0, 18, alpha));
                } else if (me->game_platform == 1) {
                    renderImageScaledF(g_ps4_disc_icon_rgba, g_ps4_disc_icon_rgba_W,
                                       g_ps4_disc_icon_rgba_H, dx_i, dy_i, disc_sz, disc_sz,
                                       RenderImageFormat_Rgba32, makeRGBA(24, 26, 30, alpha));
                } else {
                    renderImageScaledF(g_ps4_disc_icon_rgba, g_ps4_disc_icon_rgba_W,
                                       g_ps4_disc_icon_rgba_H, dx_i, dy_i, disc_sz, disc_sz,
                                       RenderImageFormat_Rgba32,
                                       ((uint32_t)alpha << 24) | 0x00ffffffu);
                }
            }
        }

        if (me->starred && me->type != ENTRY_TYPE_THEME)
            renderStarF(x + w - 13.0f, y + 14.0f, 7.0f, makeRGBA(255, 206, 56, dim));
        return;
    }

    /* Subtle Switch 2 elastic pop (stays tight so neighboring tiles never collide) */
    float scale = is_focused ? (1.0f + 0.026f * pop_amount) : 1.0f;
    float draw_w = w * scale;
    float draw_h = h * scale;
    float dx = x - (draw_w - w) * 0.5f;
    float dy = y - (draw_h - h) * 0.5f;
    float radius = (draw_w < draw_h ? draw_w : draw_h) * 0.072f;

    /* Soft Switch 2 elevation shadow */
    renderSoftShadowF(dx, dy + (is_focused ? 7.0f : 3.0f), draw_w, draw_h,
                      radius, is_focused ? 14.0f : 7.0f,
                      dark ? (is_focused ? 120 : 55) : (is_focused ? 65 : 30));

    /* Tile backing card (prevents transparent edges on SVG/PNG icons) */
    uint32_t card_bg = dark ? makeRGBA(38, 40, 46, 255) : makeRGBA(255, 255, 255, 255);
    renderRoundedRectF(dx, dy, draw_w, draw_h, radius, card_bg);

    if (img) {
        renderImageScaledF(img, img_w, img_h, dx, dy, draw_w, draw_h, fmt, 0xffffffffu);
    }

    if (me->starred && me->type != ENTRY_TYPE_THEME) {
        float bx = dx + draw_w - 22.0f;
        float by = dy + 22.0f;
        renderStarF(bx, by + 1.5f, 11.5f, makeRGBA(0, 0, 0, 125));
        renderStarF(bx, by, 10.5f, makeRGBA(255, 206, 56, 255));
    }

    if (is_focused) {
        float pad = 6.5f;
        renderSwitch2FocusRingF(dx - pad, dy - pad, draw_w + pad * 2.0f, draw_h + pad * 2.0f,
                                radius + pad, 4.0f, ringTimer(),
                                themeCurrent.highlightColor.abgr,
                                themeCurrent.highlightGradientEdgeColor.abgr);
    }
}

static void drawAllSoftwareEndTile(float x, float y, float w, float h,
                                   bool is_focused, float pop_amount) {
    if (s_cfg.ui_style == 1) {
        renderRectF(x, y, w, h, makeRGBA(28, 82, 190, 210));
        const float cx = x + w * 0.5f;
        const float cy = y + h * 0.5f;
        const float sq = (w < h ? w : h) * 0.12f;
        const uint32_t fg = 0xffffffffu;
        renderRectF(cx - sq * 1.5f, cy - sq * 1.5f, sq, sq, fg);
        renderRectF(cx + sq * 0.5f, cy - sq * 1.5f, sq, sq, fg);
        renderRectF(cx - sq * 1.5f, cy + sq * 0.5f, sq, sq, fg);
        renderRectF(cx + sq * 0.5f, cy + sq * 0.5f, sq, sq, fg);
        (void)is_focused;
        return;
    }
    bool dark = isDarkTheme();
    float scale = is_focused ? (1.0f + 0.068f * pop_amount) : 1.0f;
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;
    float r = ((w < h ? w : h) * 0.38f) * scale;

    uint32_t bg = dark ? makeRGBA(46, 48, 56, 255) : makeRGBA(255, 255, 255, 255);
    uint32_t fg = dark ? makeRGBA(225, 228, 236, 255) : makeRGBA(62, 66, 74, 255);

    renderSoftShadowF(cx - r, cy - r + 5.0f, r * 2.0f, r * 2.0f, r, is_focused ? 14.0f : 8.0f,
                      dark ? 95 : 45);
    renderCircleF(cx, cy, r, bg);

    float sq = r * 0.34f;
    float gap = r * 0.12f;
    float off = sq + gap * 0.5f;
    renderRoundedRectF(cx - off, cy - off, sq, sq, sq * 0.25f, fg);
    renderRoundedRectF(cx + gap * 0.5f, cy - off, sq, sq, sq * 0.25f, fg);
    renderRoundedRectF(cx - off, cy + gap * 0.5f, sq, sq, sq * 0.25f, fg);
    renderRoundedRectF(cx + gap * 0.5f, cy + gap * 0.5f, sq, sq, sq * 0.25f, fg);

    if (is_focused) {
        renderSwitch2CircleFocusRingF(cx, cy, r + 6.0f, 5.0f, ringTimer(),
                                      themeCurrent.highlightColor.abgr,
                                      themeCurrent.highlightGradientEdgeColor.abgr);
    }
}

static void drawFocusedHeaderInfo(menuEntry_s *me, float center_x, float title_y, bool is_all_software_tile) {
    if (s_cfg.ui_style == 1) {
        if (fabsf(s_focus_pos - (float)s_sel) > 0.15f)
            return;
        const char *label = is_all_software_tile ? "Library" : (me ? me->name : "");
        DrawTextTruncate(interuimedium30, (uint32_t)center_x, (uint32_t)title_y,
                         MakeColor(255, 255, 255, 255), label, 620, "...");
        return;
    }
    bool dark = isDarkTheme();
    /* Switch 2 HOME highlights the selected title in crisp cyan/teal right above the tile */
    color_t title_col = dark ? MakeColor(78, 224, 245, 255) : MakeColor(0, 168, 204, 255);
    color_t muted = makeColorMuted(themeCurrent.textColor, dark ? 150 : 145);

    if (is_all_software_tile) {
        const char *label = "All Software";
        uint32_t tw = 0, th = 0;
        GetTextDimensions(interuimedium20, label, &tw, &th);
        float tx = center_x - (float)tw * 0.5f;
        if (tx < 48.0f) tx = 48.0f;
        if (tx + (float)tw > SCREEN_W - 48.0f) tx = SCREEN_W - 48.0f - (float)tw;
        DrawText(interuimedium20, (uint32_t)tx, (uint32_t)title_y, title_col, label);
        return;
    }

    if (!me) return;

    char titleBuf[ENTRY_NAMELENGTH + 16];
    snprintf(titleBuf, sizeof(titleBuf), "%s", me->name);

    uint32_t max_w = 620;
    uint32_t tw = 0, th = 0;
    GetTextDimensions(interuimedium20, titleBuf, &tw, &th);
    if (tw > max_w) tw = max_w;

    /* Tiny Switch 2 cyan cartridge/NRO badge (or gold star if favorite) beside the title */
    bool show_star = (me->starred && me->type != ENTRY_TYPE_THEME);
    bool show_cart = (me->type == ENTRY_TYPE_FILE);
    float icon_w = (show_star ? 24.0f : 0.0f) + (show_cart ? 22.0f : 0.0f);
    float total_w = icon_w + (float)tw;
    float tx = center_x - total_w * 0.5f;
    if (tx < 48.0f) tx = 48.0f;
    if (tx + total_w > SCREEN_W - 48.0f) tx = SCREEN_W - 48.0f - total_w;

    float cur_x = tx;
    if (show_star) {
        renderStarF(cur_x + 9.0f, title_y - 7.5f, 9.0f, makeRGBA(255, 206, 56, 255));
        cur_x += 24.0f;
    }
    if (show_cart) {
        if (me->game_platform == 2) {
            /* Switch 2 game card: red */
            renderRoundedRectF(cur_x, title_y - 16.0f, 14.0f, 18.0f, 3.0f, makeRGBA(230, 0, 18, 255));
            renderRectF(cur_x + 3.5f, title_y - 4.0f, 7.0f, 2.0f, makeRGBA(255, 232, 234, 255));
        } else if (me->game_platform == 1) {
            /* Switch 1 game card: black */
            renderRoundedRectF(cur_x, title_y - 16.0f, 14.0f, 18.0f, 3.0f, makeRGBA(18, 20, 24, 255));
            renderRectF(cur_x + 3.5f, title_y - 4.0f, 7.0f, 2.0f, makeRGBA(180, 185, 196, 255));
        } else {
            uint32_t c_col = title_col.abgr | 0xff000000u;
            renderRoundedRectF(cur_x, title_y - 16.0f, 14.0f, 18.0f, 3.0f, c_col);
            renderRectF(cur_x + 3.5f, title_y - 4.0f, 7.0f, 2.0f, dark ? makeRGBA(24, 24, 26, 255) : makeRGBA(235, 235, 238, 255));
        }
        cur_x += 22.0f;
    }

    DrawTextTruncate(interuimedium20, (uint32_t)cur_x, (uint32_t)title_y, title_col, titleBuf, max_w, "...");

    /* Discreet Author · Version subtitle right underneath (respects Settings toggle) */
    if (!s_cfg.show_subtitle)
        return;
    if (me->type != ENTRY_TYPE_FOLDER) {
        char subBuf[320];
        if (me->version[0] && me->author[0])
            snprintf(subBuf, sizeof(subBuf), "%s  \u00B7  v%s", me->author, me->version);
        else if (me->author[0])
            snprintf(subBuf, sizeof(subBuf), "%s", me->author);
        else if (me->version[0])
            snprintf(subBuf, sizeof(subBuf), "v%s", me->version);
        else
            subBuf[0] = 0;

        if (subBuf[0]) {
            uint32_t sw = 0, sh = 0;
            GetTextDimensions(interuiregular14, subBuf, &sw, &sh);
            if (sw > max_w) sw = max_w;
            float sx = center_x - (float)sw * 0.5f;
            if (sx < 48.0f) sx = 48.0f;
            if (sx + (float)sw > SCREEN_W - 48.0f) sx = SCREEN_W - 48.0f - (float)sw;
            DrawTextTruncate(interuiregular14, (uint32_t)sx, (uint32_t)(title_y + 24.0f), muted, subBuf, max_w, "...");
        }
    } else {
        const char *sub = "Folder";
        uint32_t sw = 0, sh = 0;
        GetTextDimensions(interuiregular14, sub, &sw, &sh);
        float sx = center_x - (float)sw * 0.5f;
        if (sx < 48.0f) sx = 48.0f;
        if (sx + (float)sw > SCREEN_W - 48.0f) sx = SCREEN_W - 48.0f - (float)sw;
        DrawText(interuiregular14, (uint32_t)sx, (uint32_t)(title_y + 24.0f), muted, sub);
    }
}

static float ps4TileWidth(int index) {
    float d = fabsf((float)index - s_focus_pos);
    if (d > 1.0f) d = 1.0f;
    return 153.0f + 108.0f * (1.0f - d) * (1.0f - s_topbar_t);
}

static float ps4TileHeight(int index) {
    float d = fabsf((float)index - s_focus_pos);
    if (d > 1.0f) d = 1.0f;
    return 153.0f + 161.0f * (1.0f - d) * (1.0f - s_topbar_t);
}

static float ps4TileGap(void) {
    return 2.0f + 12.0f * s_topbar_t;
}

static float ps4Cumulative(int index) {
    float sum = 0.0f;
    for (int j = 0; j < index; j++)
        sum += ps4TileWidth(j) + ps4TileGap();
    return sum;
}

static void homeTileRect(int index, float *x, float *y, float *w, float *h) {
    const bool ps4 = s_cfg.ui_style == 1;
    if (!ps4) {
        *x = CAROUSEL_START_X + (float)index * CAROUSEL_STRIDE - s_scroll_x;
        *y = CAROUSEL_TILE_Y;
        *w = *h = CAROUSEL_TILE_SZ;
        return;
    }
    *y = 140.0f + (s_cfg.ui_style == 1 ? s_topbar_t * 505.0f : 0.0f);
    *w = ps4TileWidth(index);
    *h = ps4TileHeight(index);
    const int before = (int)floorf(s_focus_pos);
    const float frac = s_focus_pos - (float)before;
    const float focus_cum = ps4Cumulative(before) +
                            frac * (ps4TileWidth(before) + ps4TileGap());
    *x = 187.0f + ps4Cumulative(index) - focus_cum;
}

static void drawPS4FocusFrame(void) {
    if (s_focus != S2Focus_Apps)
        return;
    int before = (int)floorf(s_focus_pos);
    int after = before + 1;
    float frac = s_focus_pos - (float)before;
    if (before < 0) { before = 0; after = 0; frac = 0.0f; }
    float ax, ay, aw, ah, bx, by, bw, bh;
    homeTileRect(before, &ax, &ay, &aw, &ah);
    homeTileRect(after, &bx, &by, &bw, &bh);
    float x = ax + (bx - ax) * frac;
    float y = ay + (by - ay) * frac;
    float w = aw + (bw - aw) * frac;
    float h = ah + (bh - ah) * frac;
    const bool settled = fabsf(s_focus_pos - (float)s_sel) < 0.02f;
    float alpha = 1.0f;
    if (settled)
        alpha = 0.875f + 0.125f * sinf((float)menuTimer * 3.93f);
    const uint8_t a = (uint8_t)(alpha * 255.0f);
    const uint32_t glow = ((uint32_t)(a / 3) << 24) | 0x00ffc83fu;
    const uint32_t c = ((uint32_t)a << 24) | 0x00ffc83fu;
    renderRectF(x - 4.0f, y - 4.0f, w + 8.0f, 1.0f, glow);
    renderRectF(x - 4.0f, y + h + 3.0f, w + 8.0f, 1.0f, glow);
    renderRectF(x - 4.0f, y - 4.0f, 1.0f, h + 8.0f, glow);
    renderRectF(x + w + 3.0f, y - 4.0f, 1.0f, h + 8.0f, glow);
    renderRectF(x - 1.0f, y - 1.0f, w + 2.0f, 2.0f, c);
    renderRectF(x - 1.0f, y + h - 1.0f, w + 2.0f, 2.0f, c);
    renderRectF(x - 1.0f, y - 1.0f, 2.0f, h + 2.0f, c);
    renderRectF(x + w - 1.0f, y - 1.0f, 2.0f, h + 2.0f, c);
}

static void drawPS4Credits(menuEntry_s *me, float x, float y) {
    if (!me || fabsf(s_focus_pos - (float)s_sel) > 0.15f)
        return;
    char sub[384];
    sub[0] = '\0';
    if (me->author[0] && me->version[0])
        snprintf(sub, sizeof(sub), "%s  \u00B7  v%s", me->author, me->version);
    else if (me->author[0])
        snprintf(sub, sizeof(sub), "%s", me->author);
    else if (me->version[0])
        snprintf(sub, sizeof(sub), "v%s", me->version);
    if (sub[0])
        DrawTextTruncate(interuiregular14, (uint32_t)x, (uint32_t)y,
                         MakeColor(255, 255, 255, 165), sub, 620, "...");
}

static void drawCarouselView(void) {
    int total_slots = s_visible_count + (hasEndAllSoftwareTile() ? 1 : 0);

    if (s_visible_count == 0) {
        const char *msg = s_favorites_only
            ? "No favorite apps yet. Press X on an app to add it to Favorites."
            : textGetString(StrId_NoAppsFound_Msg);
        uint32_t mw = 0, mh = 0;
        GetTextDimensions(interuiregular18, msg, &mw, &mh);
        DrawText(interuiregular18, (uint32_t)((SCREEN_W - (float)mw) * 0.5f), 315,
                 themeCurrent.textColor, msg);
        return;
    }

    /* One row for both styles; PS4 positions tiles from the continuous focus. */
    const bool ps4 = s_cfg.ui_style == 1;
    if (!ps4) {
        const float content_w = CAROUSEL_START_X * 2.0f + (float)total_slots * CAROUSEL_STRIDE -
                                (CAROUSEL_STRIDE - CAROUSEL_TILE_SZ);
        float max_scroll = content_w - SCREEN_W;
        if (max_scroll < 0.0f) max_scroll = 0.0f;
        if (!s_touch_active) {
            if (fabsf(s_touch_vel_x) > 0.4f) {
                s_scroll_x -= s_touch_vel_x;
                s_touch_vel_x *= 0.88f;
                if (s_scroll_x < 0.0f) { s_scroll_x = 0.0f; s_touch_vel_x = 0.0f; }
                if (s_scroll_x > max_scroll) { s_scroll_x = max_scroll; s_touch_vel_x = 0.0f; }
            } else {
                s_touch_vel_x = 0.0f;
                float target_scroll = CAROUSEL_START_X + (float)s_sel * CAROUSEL_STRIDE +
                                      CAROUSEL_TILE_SZ * 0.5f - SCREEN_W * 0.42f;
                if (target_scroll < 0.0f) target_scroll = 0.0f;
                if (target_scroll > max_scroll) target_scroll = max_scroll;
                s_scroll_x += (target_scroll - s_scroll_x) * 0.22f;
                if (fabsf(target_scroll - s_scroll_x) < 0.25f) s_scroll_x = target_scroll;
            }
        }
    }

    /* Draw unselected tiles first so the focused tile always sits on top. */
    /* Sequence A: content row tiles enter at 4400..5000ms (translateY +16px -> 0, stagger 60ms) */
    if (ps4 && s_ps4_boot_ms < 4400.0f)
        return;
    const float slide_y = 0.0f;
    for (int i = 0; i < total_slots; i++) {
        float tx, ty, tw, th;
        homeTileRect(i, &tx, &ty, &tw, &th);
        if (tx + tw < -40.0f || tx > SCREEN_W + 40.0f)
            continue;
        if (i == s_sel && (s_cfg.ui_style == 1 || s_focus == S2Focus_Apps))
            continue;
        float boot_dy = 0.0f;
        if (ps4 && s_ps4_boot_ms < 5000.0f) {
            const float st = 4400.0f + (float)(i < 8 ? i : 8) * 60.0f;
            const float p = segProg(s_ps4_boot_ms, st, st + 360.0f);
            if (p <= 0.0f) continue;
            boot_dy = (1.0f - easeOutSoft(p)) * 16.0f;
        }
        if (i < s_visible_count)
            drawAppTile(s_visible[i], tx, ty + slide_y + boot_dy, tw, th, false, 0.0f);
        else
            drawAllSoftwareEndTile(tx, ty + slide_y + boot_dy, tw, th, false, 0.0f);
    }

    if ((s_cfg.ui_style == 1 || s_focus == S2Focus_Apps) &&
        s_sel >= 0 && s_sel < total_slots) {
        float tx, ty, tw, th;
        homeTileRect(s_sel, &tx, &ty, &tw, &th);
        float sel_boot_dy = 0.0f;
        if (ps4 && s_ps4_boot_ms < 5000.0f) {
            const float st = 4400.0f + (float)(s_sel < 8 ? s_sel : 8) * 60.0f;
            const float p = segProg(s_ps4_boot_ms, st, st + 360.0f);
            sel_boot_dy = (1.0f - easeOutSoft(p)) * 16.0f;
        }
        bool is_end_tile = (s_sel == s_visible_count);
        if (!is_end_tile) {
            drawAppTile(s_visible[s_sel], tx, ty + slide_y + sel_boot_dy, tw, th, true, s_app_pop);
            if (ps4 && s_focus == S2Focus_Apps) {
                drawFocusedHeaderInfo(s_visible[s_sel], tx + tw + 16.0f,
                                      ty + slide_y + sel_boot_dy + th - 38.0f, false);
                drawPS4Credits(s_visible[s_sel], tx + tw + 16.0f,
                               ty + slide_y + sel_boot_dy + th - 8.0f);
            }
            else if (!ps4)
                drawFocusedHeaderInfo(s_visible[s_sel], tx + tw * 0.5f,
                                      152.0f + slide_y, false);
        } else {
            drawAllSoftwareEndTile(tx, ty + slide_y + sel_boot_dy, tw, th, true, s_app_pop);
            if (ps4 && s_focus == S2Focus_Apps)
                drawFocusedHeaderInfo(NULL, tx + tw + 16.0f,
                                      ty + slide_y + sel_boot_dy + th - 8.0f, true);
            else if (!ps4)
                drawFocusedHeaderInfo(NULL, tx + tw * 0.5f,
                                      162.0f + slide_y, true);
        }
        if (ps4 && s_ps4_boot_ms >= 4700.0f)
            drawPS4FocusFrame();
    }
}

static uint32_t ps4Lerp(uint32_t a, uint32_t b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const int ar = a & 255, ag = (a >> 8) & 255, ab = (a >> 16) & 255;
    const int br = b & 255, bg = (b >> 8) & 255, bb = (b >> 16) & 255;
    return makeRGBA((uint8_t)(ar + (br - ar) * t),
                    (uint8_t)(ag + (bg - ag) * t),
                    (uint8_t)(ab + (bb - ab) * t), 255);
}

static void drawPS4Background(void) {
    const uint32_t center = makeRGBA(11, 70, 200, 255);
    const uint32_t mid = makeRGBA(10, 58, 158, 255);
    const uint32_t edge = makeRGBA(5, 38, 107, 255);
    for (int y = 0; y < 720; y += 8) {
        const float dy = fabsf((float)y + 4.0f - 288.0f) / 432.0f;
        const uint32_t c = ps4Lerp(center, mid, dy * 1.4f);
        const uint32_t v = ps4Lerp(c, edge, dy * dy * 0.95f);
        renderRectF(0.0f, (float)y, SCREEN_W, 8.0f, v);
    }
    /* Soft diagonal silk bands: layered translucent circles read as blurred folds. */
    renderCircleF(220.0f, 160.0f, 360.0f, makeRGBA(47, 111, 224, 28));
    renderCircleF(980.0f, 80.0f, 300.0f, makeRGBA(47, 111, 224, 20));
    renderCircleF(760.0f, 620.0f, 420.0f, makeRGBA(47, 111, 224, 22));
    renderCircleF(1180.0f, 500.0f, 260.0f, makeRGBA(47, 111, 224, 16));
    renderCircleF(420.0f, 700.0f, 320.0f, makeRGBA(47, 111, 224, 18));

    /* Orbis-style flowing wave ribbons, sweeping slowly across the canvas. */
    for (int w = 0; w < 2; w++) {
        const float base = 210.0f + (float)w * 240.0f;
        const float amp = 70.0f - (float)w * 26.0f;
        const float speed = 0.10f + 0.06f * (float)w;
        const float phase = (float)menuTimer * speed + (float)w * 2.4f;
        const uint8_t wa = (uint8_t)(16 + w * 6);
        const uint32_t wc = makeRGBA(72, 140, 235, wa);
        const uint32_t wg = makeRGBA(120, 190, 255, wa / 2);
        for (int x = 0; x < 1280; x += 16) {
            const float fx = (float)x * 0.0075f;
            const float y0 = base + amp * sinf(fx + phase);
            renderRectF((float)x, y0, 16.0f, 130.0f - (float)w * 30.0f, wc);
            renderRectF((float)x, y0 + 20.0f, 16.0f, 3.0f, wg);
        }
    }

    /* Floating dust, deterministic and slowly drifting. */
    for (int i = 0; i < 34; i++) {
        uint32_t h = (uint32_t)(i * 2654435761u + 0x9e3779b9u);
        const int layer = i % 3;
        const float depth = 0.5f + (float)layer * 0.5f;
        float x = (float)(h % 1280u) + s_dust_parallax * depth;
        while (x < -10.0f) x += 1300.0f;
        while (x > 1290.0f) x -= 1300.0f;
        float y = (float)((h >> 11) % 720u);
        y -= (float)menuTimer * (3.0f + depth * 8.0f);
        while (y < -10.0f) y += 740.0f;
        const float r = 1.5f + depth * 4.0f + (float)((h >> 20) % 4u);
        const float twinkle = 0.5f + 0.5f * sinf((float)menuTimer * (0.8f + 0.2f * layer) + (float)i);
        const uint8_t a = (uint8_t)((0.2f + 0.4f * twinkle) * 255.0f);
        const uint32_t c = ((uint32_t)a << 24) | 0x00ffe4cfu;
        renderCircleF(x, y, r, c);
        if (layer == 2)
            renderCircleF(x + 1.0f, y + 1.0f, r + 1.5f, ((uint32_t)(a / 3) << 24) | 0x00ffe4cfu);
    }
    /* Vignette: darken the outer 96 px in a few soft steps. */
    for (int i = 0; i < 6; i++) {
        const float inset = (float)i * 16.0f;
        const uint8_t a = (uint8_t)(8 + i * 5);
        const uint32_t c = ((uint32_t)a << 24) | 0x00280a00u;
        renderRectF(0.0f, 0.0f, SCREEN_W, inset + 16.0f, c);
        renderRectF(0.0f, 720.0f - inset - 16.0f, SCREEN_W, inset + 16.0f, c);
        renderRectF(0.0f, 0.0f, inset + 16.0f, 720.0f, c);
    }
}

static const uint8_t g_ps4_avatar28[3136] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    0,0,0,255,0,0,0,255,0,0,0,255,0,0,0,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
};
static const int g_ps4_avatar28_W=28, g_ps4_avatar28_H=28;

/* Persistent PS4 bottom bar: prompts left, profile right (Orbis spec). */
static void drawPS4BottomBar(const char *enter_label, const char *back_label) {
    const float bar_h = 104.0f; /* twice the spec height per request */
    const float y0 = SCREEN_H - bar_h;
    const float cy = y0 + bar_h * 0.5f;
    renderRectF(0.0f, y0, SCREEN_W, bar_h, makeRGBA(0, 0, 0, 115));
    renderRectF(0.0f, y0, SCREEN_W, 1.0f, makeRGBA(255, 255, 255, 46));

    /* Left cluster: Switch 2 button glyphs from the menu theme, 24px pair spacing */
    const char *glyphs[2] = { themeCurrent.buttonAText, themeCurrent.buttonBText };
    const char *labels[2] = { enter_label, back_label };
    float px = 77.0f + 12.0f;
    for (int i = 0; i < 2; i++) {
        DrawText(interuiregular18, (uint32_t)px,
                 (uint32_t)(cy + fontBaselineOffset(interuiregular18)),
                 MakeColor(255, 255, 255, 230), glyphs[i]);
        uint32_t gw = 0, gh = 0;
        GetTextDimensions(interuiregular18, glyphs[i], &gw, &gh);
        DrawText(interuiregular18, (uint32_t)(px + (float)gw + 8.0f),
                 (uint32_t)(cy + fontBaselineOffset(interuiregular18)),
                 MakeColor(255, 255, 255, 220), labels[i]);
        uint32_t lw = 0, lh = 0;
        GetTextDimensions(interuiregular18, labels[i], &lw, &lh);
        px += (float)gw + 8.0f + (float)lw + 24.0f + 24.0f;
    }

    /* Right cluster: avatar + display name */
    const char *name = s_cfg.display_name[0] ? s_cfg.display_name : "Switchstation 2";
    uint32_t nw = 0, nh = 0;
    GetTextDimensions(interuiregular18, name, &nw, &nh);
    const float name_x = 1203.0f - (float)nw;
    DrawText(interuiregular18, (uint32_t)name_x,
             (uint32_t)(cy + fontBaselineOffset(interuiregular18)),
             MakeColor(255, 255, 255, 255), name);
    renderImageScaledF(g_ps4_avatar28, g_ps4_avatar28_W, g_ps4_avatar28_H,
                       name_x - 38.0f, cy - 14.0f, 28.0f, 28.0f,
                       RenderImageFormat_Rgba32, 0xffffffffu);
}

static void drawPS4StatusBar(void) {
    /* Always visible, independent of the top bar animation */
    const color_t white = MakeColor(255, 255, 255, 255);
    const float y = 38.0f;

    DrawText(interuiregular18, 72, (uint32_t)(y + fontBaselineOffset(interuiregular18)),
             white, "Switchstation 2");

    float right_x = 1213.0f;
    char clock[16];
    time_t now = time(NULL);
    struct tm *tmNow = localtime(&now);
    strftime(clock, sizeof(clock), "%I:%M %p", tmNow);
    if (clock[0] == '0')
        memmove(clock, clock + 1, strlen(clock));
    uint32_t tw = 0, th = 0;
    GetTextDimensions(interuimedium20, clock, &tw, &th);
    right_x -= (float)tw;
    DrawText(interuimedium20, (uint32_t)right_x, (uint32_t)(y + 7), white, clock);

    uint32_t charge = 100;
    bool charging = false;
    if (powerGetDetails(&charge, &charging)) {
        if (charge > 100) charge = 100;
        right_x -= 48.0f;
        drawHorizontalBattery(right_x, y - 9.5f, charge, charging, white, true);
        right_x -= 18.0f;
    }

    bool netFlag = false, tempFlag = false;
    s32 temperature = 0;
    AssetId netId = AssetId_wifi_none_icon;
    if (statusGet(&netFlag, &netId, &tempFlag, &temperature) && netFlag) {
        assetsDataEntry *netData = NULL;
        assetsGetData(netId, &netData);
        if (netData) {
            const int nw = netData->imageSize[0];
            const int nh = netData->imageSize[1];
            right_x -= (float)nw;
            renderImageScaledF(netData->buffer, nw, nh, right_x,
                               y - (float)nh * 0.5f, (float)nw, (float)nh,
                               RenderImageFormat_Rgba32, 0xffffffffu);
            right_x -= 16.0f;
        }
    }
}

static int gridCols(void) {
    return (s_cfg.ui_style == 1) ? 3 : GRID_COLS;
}

static float gridStrideY(void) {
    return (s_cfg.ui_style == 1) ? PS4_CARD_SY : GRID_STRIDE_Y;
}

/* Orbis Library card: square cover flush left, navy info panel right */
static void drawPS4LibraryCard(menuEntry_s *me, float x, float y, bool focused) {
    const uint32_t fill = focused ? makeRGBA(18, 68, 148, 217) : makeRGBA(6, 22, 56, 184);
    renderRectF(x, y, PS4_CARD_W, PS4_CARD_H, fill);
    if (focused) {
        const uint32_t edge = makeRGBA(124, 232, 255, 255); /* #7CE8FF */
        renderRectF(x, y, PS4_CARD_W, 2.0f, edge);
        renderRectF(x, y + PS4_CARD_H - 2.0f, PS4_CARD_W, 2.0f, edge);
        renderRectF(x, y, 2.0f, PS4_CARD_H, edge);
        renderRectF(x + PS4_CARD_W - 2.0f, y, 2.0f, PS4_CARD_H, edge);
    }

    const uint8_t *img = me->icon_gfx;
    enum RenderImageFormat fmt = (me->icon_mode == IMAGE_MODE_RGB24)
                                    ? RenderImageFormat_Rgb24 : RenderImageFormat_Rgba32;
    if (!img) {
        img = (me->type == ENTRY_TYPE_FOLDER) ? folder_icon_large
            : (me->type == ENTRY_TYPE_THEME) ? theme_icon_large : invalid_icon_large;
        fmt = RenderImageFormat_Rgba32;
    }
    if (img)
        renderImageScaledF(img, 256, 256, x, y, PS4_CARD_H, PS4_CARD_H, fmt, 0xffffffffu);

    /* Info panel: title + muted subtitle. DrawTextTruncate lets the trailing
     * "..." overshoot its width limit, so keep an ellipsis margin inside the
     * card so nothing spills past the right border. */
    const float info_x = x + PS4_CARD_H + 16.0f;
    const float info_w = PS4_CARD_W - PS4_CARD_H - 32.0f;
    DrawTextTruncate(interuimedium20, (uint32_t)info_x, (uint32_t)(y + 30.0f),
                     MakeColor(255, 255, 255, 255), me->name, (int)(info_w - 30.0f), "...");
    const char *sub =
        (me->type == ENTRY_TYPE_FOLDER) ? "Folder" :
        (me->author[0] ? me->author : (me->version[0] ? me->version : NULL));
    if (sub)
        DrawTextTruncate(interuiregular14, (uint32_t)info_x, (uint32_t)(y + 58.0f),
                         MakeColor(255, 255, 255, 140), sub, (int)(info_w - 22.0f), "...");
    if (me->starred && me->type != ENTRY_TYPE_THEME)
        renderStarF(x + PS4_CARD_W - 16.0f, y + 16.0f, 8.0f, makeRGBA(255, 206, 56, 255));
}

static void drawGridView(void) {
    if (s_visible_count == 0) {
        drawCarouselView();
        return;
    }

    const bool ps4 = s_cfg.ui_style == 1;
    const int cols = gridCols();
    if (s_sel >= s_visible_count) s_sel = s_visible_count - 1;
    int sel_row = s_sel / cols;
    int total_rows = (s_visible_count + cols - 1) / cols;
    float max_scroll_y = (total_rows > 2) ? (float)(total_rows - 2) * gridStrideY() : 0.0f;
    float target_y = (sel_row > 0) ? (float)(sel_row - 1) * gridStrideY() : 0.0f;
    if (target_y > max_scroll_y) target_y = max_scroll_y;
    s_grid_scroll_y += (target_y - s_grid_scroll_y) * 0.22f;

    /* Draw unselected grid tiles */
    float slide_y = transSlideOffset();
    for (int i = 0; i < s_visible_count; i++) {
        int col = i % cols;
        int row = i / cols;
        float tx = ps4 ? PS4_LIB_X + (float)col * PS4_CARD_SX
                      : GRID_START_X + (float)col * GRID_STRIDE_X;
        float ty = ps4 ? PS4_LIB_Y + (float)row * PS4_CARD_SY - s_grid_scroll_y
                         + s_topbar_t * 478.0f
                      : GRID_START_Y + (float)row * GRID_STRIDE_Y - s_grid_scroll_y + slide_y;
        const float th = ps4 ? PS4_CARD_H : GRID_TILE_SZ;
        const float clip_b = ps4 ? SCREEN_H : GRID_CLIP_BOTTOM;
        if (ty + th < GRID_CLIP_TOP - 20.0f || ty > clip_b + 20.0f)
            continue;
        if (s_focus == S2Focus_Apps && i == s_sel)
            continue;
        if (ps4)
            drawPS4LibraryCard(s_visible[i], tx, ty, false);
        else
            drawAppTile(s_visible[i], tx, ty, GRID_TILE_SZ, GRID_TILE_SZ, false, 0.0f);
    }

    /* Draw focused grid tile on top */
    if (s_focus == S2Focus_Apps && s_sel >= 0 && s_sel < s_visible_count) {
        int col = s_sel % cols;
        int row = s_sel / cols;
        float tx = ps4 ? PS4_LIB_X + (float)col * PS4_CARD_SX
                      : GRID_START_X + (float)col * GRID_STRIDE_X;
        float ty = ps4 ? PS4_LIB_Y + (float)row * PS4_CARD_SY - s_grid_scroll_y
                         + s_topbar_t * 478.0f
                      : GRID_START_Y + (float)row * GRID_STRIDE_Y - s_grid_scroll_y + slide_y;
        const float th = ps4 ? PS4_CARD_H : GRID_TILE_SZ;
        const float clip_b = ps4 ? SCREEN_H : GRID_CLIP_BOTTOM;
        if (ty + th >= GRID_CLIP_TOP - 20.0f && ty <= clip_b + 20.0f) {
            if (ps4)
                drawPS4LibraryCard(s_visible[s_sel], tx, ty, true);
            else
                drawAppTile(s_visible[s_sel], tx, ty, GRID_TILE_SZ, GRID_TILE_SZ, true, s_app_pop);
        }
    }

    /* Clean top & bottom occlusion bands so scrolled grid tiles never overlap the header or pill.
     * PS4 keeps its transparent gradient and draws the status bar over the grid. */
    if (s_cfg.ui_style != 1) {
        uint32_t bg_solid = themeCurrent.backgroundColor.abgr | 0xff000000u;
        renderRectF(0.0f, 0.0f, SCREEN_W, GRID_CLIP_TOP, bg_solid);
        renderRectF(0.0f, GRID_CLIP_BOTTOM, SCREEN_W, SCREEN_H - GRID_CLIP_BOTTOM, bg_solid);
    }

    if (s_cfg.ui_style == 1) {
        DrawText(interuimedium30, 77, 96, MakeColor(255, 255, 255, 255), "Library");
        char countBuf[16];
        snprintf(countBuf, sizeof(countBuf), "%d", s_visible_count);
        uint32_t lw = 0, lh = 0, cw = 0, chh = 0;
        GetTextDimensions(interuimedium30, "Library", &lw, &lh);
        GetTextDimensions(interuiregular14, countBuf, &cw, &chh);
        const float bx = 77.0f + (float)lw + 14.0f;
        const float by = 96.0f - fontBaselineOffset(interuimedium30) - 13.0f;
        renderRoundedRectF(bx, by, (float)cw + 16.0f, 26.0f, 2.0f,
                           makeRGBA(255, 255, 255, 56));
        DrawText(interuiregular14, (uint32_t)(bx + 8.0f),
                 (uint32_t)(by + 13.0f + fontBaselineOffset(interuiregular14)),
                 MakeColor(255, 255, 255, 255), countBuf);
    } else if (s_focus == S2Focus_Apps && s_sel < s_visible_count) {
        /* Centered title/author bar above the Switch 2 grid. */
        drawFocusedHeaderInfo(s_visible[s_sel], SCREEN_W * 0.5f, 112.0f, false);
    }
}

static void drawPillIconSymbol(int idx, float cx, float cy, float scale, uint32_t fg, uint32_t hole_bg) {
    float s = scale * 1.15f;
    switch (idx) {
    case PILL_NETLOADER:
        /* Crisp SDF Switch 2 style Download / Netloader icon (arrow + U tray) */
        renderNetloaderIconF(cx, cy, 17.5f * scale, fg);
        break;
    case PILL_FAVORITES:
        /* Crisp SDF 5-point star centered mathematically on (cx, cy) */
        renderStarF(cx, cy, 14.5f * scale, fg);
        break;
    case PILL_ALL_SOFTWARE: {
        /* 2x2 crisp rounded squares (Switch 2 style) */
        float sq = 9.0f * s;
        float gap = 3.6f * s;
        renderRoundedRectF(cx - sq - gap * 0.5f, cy - sq - gap * 0.5f, sq, sq, 2.4f * s, fg);
        renderRoundedRectF(cx + gap * 0.5f, cy - sq - gap * 0.5f, sq, sq, 2.4f * s, fg);
        renderRoundedRectF(cx - sq - gap * 0.5f, cy + gap * 0.5f, sq, sq, 2.4f * s, fg);
        renderRoundedRectF(cx + gap * 0.5f, cy + gap * 0.5f, sq, sq, 2.4f * s, fg);
        break;
    }
    case PILL_THEMES:
        /* Layered theme cards */
        renderRoundedRectF(cx - 11.5f * s, cy - 9.0f * s, 16.0f * s, 12.5f * s, 3.2f * s, fg);
        renderRoundedRectF(cx - 4.0f * s, cy - 2.0f * s, 16.0f * s, 12.5f * s, 3.2f * s,
                           (fg & 0x00ffffffu) | 0xc8000000u);
        break;
    case PILL_SETTINGS:
        /* Crisp Switch 2 8-tooth Settings Gear */
        renderSettingsIconF(cx, cy, 16.5f * scale, fg);
        break;
    case PILL_EXIT:
        /* Clean thin Switch 2 power icon */
        renderCircleF(cx, cy + 1.5f * s, 11.5f * s, fg);
        renderCircleF(cx, cy + 1.5f * s, 8.2f * s, hole_bg);
        renderRectF(cx - 4.5f * s, cy - 11.5f * s, 9.0f * s, 10.0f * s, hole_bg);
        renderRoundedRectF(cx - 1.8f * s, cy - 12.5f * s, 3.6f * s, 12.0f * s, 1.8f * s, fg);
        break;
    }
}

static const int kPs4QuickItems[] = {
    PILL_ALL_SOFTWARE, PILL_NETLOADER, PILL_FAVORITES, PILL_THEMES,
    PILL_SETTINGS, PILL_EXIT
};
#define PS4_QUICK_COUNT 6



static const uint8_t g_ps4_lib_icon_rgba[16384] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,36,
    255,255,255,156,255,255,255,223,255,255,255,249,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,244,255,255,255,204,255,255,255,125,255,255,255,16,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,9,255,255,255,123,255,255,255,204,255,255,255,244,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,249,255,255,255,223,255,255,255,155,
    255,255,255,45,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,1,255,255,255,108,255,255,255,244,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,233,
    255,255,255,62,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,54,
    255,255,255,214,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,253,255,255,255,126,255,255,255,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,108,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,243,255,255,255,34,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,35,255,255,255,244,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,107,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,34,255,255,255,243,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,196,255,255,255,2,0,0,0,0,
    0,0,0,0,255,255,255,2,255,255,255,192,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,245,255,255,255,37,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,156,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,70,0,0,0,0,
    0,0,0,0,255,255,255,71,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,154,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,223,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,222,255,255,255,94,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,120,255,255,255,252,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,136,0,0,0,0,
    0,0,0,0,255,255,255,138,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,252,255,255,255,121,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,94,255,255,255,221,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,221,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,249,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,96,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,181,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,164,0,0,0,0,
    0,0,0,0,255,255,255,165,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,180,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,97,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,249,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,242,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,121,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,2,255,255,255,207,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,157,0,0,0,0,
    0,0,0,0,255,255,255,158,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,204,255,255,255,2,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,124,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,242,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,205,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,252,255,255,255,180,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,206,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,118,0,0,0,0,
    0,0,0,0,255,255,255,120,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,205,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,181,255,255,255,252,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,203,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,125,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,252,255,255,255,39,0,0,0,0,
    0,0,0,0,255,255,255,41,255,255,255,254,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,120,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,16,255,255,255,233,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,135,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,164,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,212,255,255,255,7,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,63,255,255,255,243,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,203,255,255,255,7,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,15,255,255,255,207,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,243,255,255,255,52,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,32,255,255,255,190,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,253,255,255,255,145,
    255,255,255,7,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,5,
    255,255,255,128,255,255,255,252,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,196,255,255,255,37,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,2,
    255,255,255,69,255,255,255,137,255,255,255,164,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,158,255,255,255,118,255,255,255,39,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,39,255,255,255,118,255,255,255,158,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,164,255,255,255,136,255,255,255,69,
    255,255,255,1,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,2,
    255,255,255,71,255,255,255,139,255,255,255,165,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,159,255,255,255,119,255,255,255,41,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,41,255,255,255,120,255,255,255,159,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,165,255,255,255,138,255,255,255,70,
    255,255,255,3,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,35,255,255,255,194,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,254,255,255,255,164,
    255,255,255,14,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,8,
    255,255,255,138,255,255,255,252,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,211,255,255,255,51,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,54,255,255,255,244,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,207,255,255,255,7,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,8,255,255,255,205,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,246,255,255,255,53,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,7,255,255,255,212,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,137,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,139,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,215,255,255,255,9,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,123,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,252,255,255,255,39,0,0,0,0,
    0,0,0,0,255,255,255,41,255,255,255,252,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,121,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,205,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,252,255,255,255,179,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,203,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,119,0,0,0,0,
    0,0,0,0,255,255,255,121,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,204,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,170,255,255,255,170,255,255,255,170,
    255,255,255,170,255,255,255,179,255,255,255,251,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,204,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,243,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,122,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,2,255,255,255,206,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,157,0,0,0,0,
    0,0,0,0,255,255,255,158,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,205,255,255,255,2,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,124,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,241,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,249,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,96,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,182,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,164,0,0,0,0,
    0,0,0,0,255,255,255,165,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,180,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,97,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,249,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,222,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,221,255,255,255,96,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,124,255,255,255,252,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,136,0,0,0,0,
    0,0,0,0,255,255,255,138,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,251,255,255,255,123,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,96,255,255,255,223,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,221,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,155,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,69,0,0,0,0,
    0,0,0,0,255,255,255,71,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,153,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,45,255,255,255,252,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,190,255,255,255,1,0,0,0,0,
    0,0,0,0,255,255,255,2,255,255,255,207,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,244,255,255,255,36,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,128,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,242,255,255,255,33,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,52,255,255,255,246,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,104,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,1,255,255,255,107,255,255,255,244,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,213,
    255,255,255,53,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,53,
    255,255,255,213,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,243,255,255,255,106,255,255,255,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,35,
    255,255,255,154,255,255,255,221,255,255,255,249,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,242,255,255,255,203,255,255,255,120,255,255,255,8,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,8,255,255,255,121,255,255,255,202,255,255,255,243,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,249,255,255,255,221,255,255,255,153,
    255,255,255,34,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const int g_ps4_lib_icon_rgba_W=64, g_ps4_lib_icon_rgba_H=64;
static const uint8_t g_ps4_dl_icon_rgba[16384] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,125,255,255,255,235,
    255,255,255,235,255,255,255,125,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,91,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,91,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,163,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,163,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,125,255,255,255,235,
    255,255,255,235,255,255,255,126,255,255,255,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,1,255,255,255,126,255,255,255,235,
    255,255,255,235,255,255,255,125,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,91,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,153,255,255,255,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,1,255,255,255,153,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,91,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,161,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,153,
    255,255,255,1,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,1,
    255,255,255,153,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,161,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,127,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,153,255,255,255,1,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,1,255,255,255,153,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,127,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,16,255,255,255,215,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,153,255,255,255,1,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,255,255,255,1,255,255,255,153,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,215,255,255,255,16,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,23,255,255,255,212,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,156,255,255,255,2,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    255,255,255,1,255,255,255,149,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,217,255,255,255,26,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,23,
    255,255,255,212,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,156,
    255,255,255,2,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,255,255,255,1,
    255,255,255,149,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,217,
    255,255,255,26,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,23,255,255,255,212,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,156,255,255,255,171,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,255,255,255,149,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,217,255,255,255,26,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,23,255,255,255,212,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,222,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,220,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,217,255,255,255,26,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,23,255,255,255,212,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,217,255,255,255,26,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,25,
    255,255,255,215,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,215,
    255,255,255,25,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,23,255,255,255,212,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,217,255,255,255,26,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,25,255,255,255,215,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,215,255,255,255,25,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,23,255,255,255,212,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,217,255,255,255,26,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,19,255,255,255,77,255,255,255,42,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,25,
    255,255,255,215,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,215,
    255,255,255,25,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,42,255,255,255,77,255,255,255,19,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,66,255,255,255,242,255,255,255,255,255,255,255,255,
    255,255,255,138,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,23,255,255,255,212,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,217,255,255,255,26,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,138,
    255,255,255,255,255,255,255,255,255,255,255,242,255,255,255,66,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,212,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,43,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,25,255,255,255,207,255,255,255,255,
    255,255,255,255,255,255,255,207,255,255,255,25,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,43,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,212,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,253,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,84,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,4,255,255,255,65,
    255,255,255,65,255,255,255,4,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,84,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,253,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,249,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,95,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,97,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,249,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,222,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,221,255,255,255,95,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,96,255,255,255,223,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,221,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,155,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,154,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,45,255,255,255,252,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,244,255,255,255,36,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,127,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,106,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,1,255,255,255,107,255,255,255,244,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,243,255,255,255,104,255,255,255,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,35,
    255,255,255,154,255,255,255,221,255,255,255,249,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,249,255,255,255,221,255,255,255,153,
    255,255,255,34,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const int g_ps4_dl_icon_rgba_W=64, g_ps4_dl_icon_rgba_H=64;
static const uint8_t g_ps4_fav_icon_rgba[16384] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,16,
    255,255,255,16,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,3,255,255,255,159,255,255,255,253,
    255,255,255,253,255,255,255,159,255,255,255,3,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,114,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,114,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,11,255,255,255,233,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,233,255,255,255,11,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,115,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,115,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,8,
    255,255,255,229,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,232,
    255,255,255,10,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,112,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,111,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,8,255,255,255,228,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,230,255,255,255,9,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,109,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,108,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,7,255,255,255,227,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,243,
    255,255,255,243,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,227,255,255,255,7,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,105,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,136,
    255,255,255,137,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,104,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,5,255,255,255,223,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,244,255,255,255,22,
    255,255,255,20,255,255,255,243,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,226,255,255,255,6,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,102,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,139,0,0,0,0,
    0,0,0,0,255,255,255,140,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,101,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,5,
    255,255,255,222,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,247,255,255,255,26,0,0,0,0,
    0,0,0,0,255,255,255,23,255,255,255,245,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,225,
    255,255,255,6,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,99,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,142,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,143,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,98,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,14,
    255,255,255,51,255,255,255,88,255,255,255,125,255,255,255,227,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,247,255,255,255,25,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,25,255,255,255,247,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,227,255,255,255,125,255,255,255,88,255,255,255,51,
    255,255,255,14,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,10,255,255,255,47,255,255,255,84,255,255,255,121,
    255,255,255,158,255,255,255,195,255,255,255,233,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,146,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,146,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,233,255,255,255,195,255,255,255,158,
    255,255,255,121,255,255,255,84,255,255,255,47,255,255,255,10,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,15,
    255,255,255,103,255,255,255,154,255,255,255,190,255,255,255,227,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,248,
    255,255,255,28,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,28,
    255,255,255,248,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,227,255,255,255,190,255,255,255,154,255,255,255,103,
    255,255,255,15,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,16,255,255,255,219,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,150,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,150,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,219,255,255,255,16,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,109,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,226,255,255,255,23,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,23,255,255,255,226,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,109,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,126,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,230,255,255,255,192,
    255,255,255,156,255,255,255,111,255,255,255,19,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,19,255,255,255,111,255,255,255,156,
    255,255,255,192,255,255,255,230,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,126,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,41,255,255,255,247,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,234,
    255,255,255,198,255,255,255,159,255,255,255,123,255,255,255,86,
    255,255,255,49,255,255,255,12,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,12,255,255,255,49,
    255,255,255,86,255,255,255,123,255,255,255,159,255,255,255,198,
    255,255,255,234,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,247,255,255,255,41,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,79,
    255,255,255,249,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,252,255,255,255,93,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,93,255,255,255,252,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,249,
    255,255,255,79,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,72,255,255,255,246,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,254,
    255,255,255,106,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,99,
    255,255,255,253,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,248,255,255,255,78,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,63,255,255,255,243,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,254,255,255,255,110,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,102,255,255,255,253,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,245,255,255,255,69,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,61,255,255,255,242,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,254,255,255,255,112,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,105,255,255,255,254,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,244,255,255,255,67,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,59,
    255,255,255,241,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,124,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,116,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,244,
    255,255,255,64,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,51,255,255,255,237,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,136,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,119,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,240,255,255,255,56,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,49,255,255,255,235,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,136,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,128,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,238,255,255,255,54,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,47,255,255,255,234,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,126,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,126,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,234,255,255,255,47,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,43,
    255,255,255,231,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,251,255,255,255,22,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,22,255,255,255,251,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,231,
    255,255,255,43,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,39,255,255,255,228,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,48,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,48,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,228,255,255,255,39,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,98,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,254,255,255,255,15,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,15,255,255,255,254,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,98,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,134,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,226,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,226,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,134,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,178,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,182,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,183,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,177,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,221,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,138,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,139,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,221,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,11,255,255,255,253,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,95,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,6,
    255,255,255,6,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,95,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,253,255,255,255,11,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,53,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,52,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,34,255,255,255,161,255,255,255,249,
    255,255,255,249,255,255,255,161,255,255,255,34,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,52,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,53,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,98,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,253,255,255,255,11,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,21,
    255,255,255,136,255,255,255,248,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,250,255,255,255,153,
    255,255,255,26,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,10,255,255,255,253,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,97,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,141,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,219,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,17,255,255,255,136,255,255,255,245,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,245,255,255,255,136,255,255,255,17,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,219,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,141,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,184,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,175,0,0,0,0,255,255,255,10,
    255,255,255,119,255,255,255,238,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,242,255,255,255,136,
    255,255,255,14,0,0,0,0,255,255,255,176,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,183,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,228,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,138,255,255,255,109,255,255,255,232,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,232,255,255,255,109,255,255,255,138,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,228,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,17,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,250,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,216,
    255,255,255,216,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,250,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,17,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,61,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,227,255,255,255,102,255,255,255,4,
    255,255,255,2,255,255,255,85,255,255,255,222,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,60,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,104,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,234,
    255,255,255,113,255,255,255,8,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,8,255,255,255,113,
    255,255,255,234,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,104,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,147,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,243,255,255,255,136,255,255,255,15,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,11,255,255,255,119,255,255,255,239,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,146,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,189,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,246,
    255,255,255,140,255,255,255,20,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,20,255,255,255,140,
    255,255,255,246,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,189,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,185,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,251,255,255,255,153,255,255,255,28,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,28,255,255,255,153,255,255,255,251,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,185,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,80,
    255,255,255,254,255,255,255,255,255,255,255,255,255,255,255,253,
    255,255,255,167,255,255,255,37,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,37,255,255,255,167,
    255,255,255,253,255,255,255,255,255,255,255,255,255,255,255,254,
    255,255,255,80,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,70,255,255,255,162,255,255,255,149,255,255,255,47,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,47,255,255,255,149,255,255,255,162,255,255,255,70,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const int g_ps4_fav_icon_rgba_W=64, g_ps4_fav_icon_rgba_H=64;
static const uint8_t g_ps4_thm_icon_rgba[16384] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,22,255,255,255,64,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,80,
    255,255,255,55,255,255,255,7,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,59,
    255,255,255,190,255,255,255,253,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,242,255,255,255,149,255,255,255,23,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,123,255,255,255,254,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,232,
    255,255,255,57,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,109,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,239,255,255,255,38,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,27,255,255,255,245,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,192,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,137,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,50,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,210,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,228,255,255,255,54,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,6,
    255,255,255,108,255,255,255,254,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,124,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,243,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,108,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,194,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,157,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,198,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,64,255,255,255,22,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,175,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,253,255,255,255,189,
    255,255,255,58,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,212,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,253,255,255,255,120,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,86,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,224,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,107,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,243,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,108,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,216,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,246,255,255,255,28,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,209,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,229,255,255,255,56,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,8,
    255,255,255,110,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,135,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,135,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,49,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,55,255,255,255,229,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,208,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,26,255,255,255,245,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,190,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,110,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,242,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,106,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,238,255,255,255,37,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,121,255,255,255,253,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,231,
    255,255,255,56,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,57,
    255,255,255,188,255,255,255,252,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,240,255,255,255,147,255,255,255,22,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,21,255,255,255,63,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,198,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,198,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,80,
    255,255,255,54,255,255,255,6,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,86,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,158,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,193,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,110,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,242,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,125,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,254,255,255,255,108,
    255,255,255,7,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,58,255,255,255,230,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,208,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,51,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,134,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,187,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,246,255,255,255,28,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,38,255,255,255,239,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,104,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,57,
    255,255,255,232,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,253,255,255,255,119,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,23,255,255,255,148,255,255,255,241,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,252,255,255,255,187,
    255,255,255,56,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,6,255,255,255,54,
    255,255,255,80,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,85,255,255,255,85,255,255,255,85,
    255,255,255,85,255,255,255,62,255,255,255,21,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const int g_ps4_thm_icon_rgba_W=64, g_ps4_thm_icon_rgba_H=64;
static const uint8_t g_ps4_set_icon_rgba[16384] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,45,255,255,255,145,255,255,255,212,255,255,255,244,
    255,255,255,244,255,255,255,212,255,255,255,144,255,255,255,44,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,7,255,255,255,151,
    255,255,255,253,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,252,
    255,255,255,148,255,255,255,6,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,7,255,255,255,190,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,189,255,255,255,6,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,151,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,148,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,45,255,255,255,253,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,252,255,255,255,43,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,145,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,203,255,255,255,103,
    255,255,255,103,255,255,255,204,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,143,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,213,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,203,255,255,255,7,0,0,0,0,
    0,0,0,0,255,255,255,7,255,255,255,205,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,211,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,11,
    255,255,255,91,255,255,255,160,255,255,255,192,255,255,255,191,
    255,255,255,159,255,255,255,91,255,255,255,10,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,242,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,103,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,105,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,242,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,69,255,255,255,162,255,255,255,224,
    255,255,255,248,255,255,255,239,255,255,255,202,255,255,255,128,
    255,255,255,23,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,95,255,255,255,232,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,230,255,255,255,94,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,6,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,80,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,75,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,14,0,0,0,0,0,0,0,0,255,255,255,22,
    255,255,255,186,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,244,255,255,255,109,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,141,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,146,255,255,255,5,0,0,0,0,255,255,255,113,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,45,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,37,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,158,255,255,255,25,255,255,255,60,255,255,255,224,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,141,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,111,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,234,255,255,255,230,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,243,255,255,255,8,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,4,255,255,255,229,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,93,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,24,
    255,255,255,244,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,141,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,116,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,230,
    255,255,255,10,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,129,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,238,255,255,255,156,255,255,255,156,
    255,255,255,238,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,227,
    255,255,255,23,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,9,
    255,255,255,199,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,175,
    255,255,255,93,255,255,255,115,255,255,255,227,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,89,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,203,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,227,255,255,255,28,0,0,0,0,0,0,0,0,
    255,255,255,28,255,255,255,203,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,237,255,255,255,45,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,20,255,255,255,204,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,248,255,255,255,112,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,28,255,255,255,238,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,158,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,239,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,114,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,6,255,255,255,135,255,255,255,242,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,253,255,255,255,166,255,255,255,24,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,4,255,255,255,112,255,255,255,219,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,248,
    255,255,255,177,255,255,255,48,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,158,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,189,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,248,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,94,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,9,
    255,255,255,79,255,255,255,108,255,255,255,118,255,255,255,73,
    255,255,255,13,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,34,255,255,255,63,255,255,255,51,255,255,255,15,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,159,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,188,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,223,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,176,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,28,255,255,255,239,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,157,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,161,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,112,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,6,255,255,255,204,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,89,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,68,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,249,255,255,255,49,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,137,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,229,
    255,255,255,10,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,185,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,177,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,10,255,255,255,243,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,92,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,21,255,255,255,224,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,248,255,255,255,15,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,81,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,145,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,59,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,53,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,15,255,255,255,87,255,255,255,131,255,255,255,159,
    255,255,255,159,255,255,255,131,255,255,255,87,255,255,255,14,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,111,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,231,255,255,255,4,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,22,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,61,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,33,255,255,255,164,
    255,255,255,249,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,249,
    255,255,255,163,255,255,255,32,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,118,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,230,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,135,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,34,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,105,255,255,255,249,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,248,255,255,255,103,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,75,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,111,255,255,255,6,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,12,255,255,255,89,255,255,255,159,255,255,255,189,
    255,255,255,221,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,219,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,139,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,137,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,14,255,255,255,253,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,244,255,255,255,212,255,255,255,144,255,255,255,44,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,96,
    255,255,255,232,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,114,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,99,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,104,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,167,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,252,
    255,255,255,148,255,255,255,6,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,142,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,218,255,255,255,6,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,33,255,255,255,249,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,229,255,255,255,181,
    255,255,255,181,255,255,255,230,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,247,255,255,255,31,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,25,255,255,255,238,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,189,255,255,255,6,0,0,0,0,
    0,0,0,0,255,255,255,112,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,221,255,255,255,32,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,164,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,228,255,255,255,79,255,255,255,1,0,0,0,0,
    0,0,0,0,255,255,255,1,255,255,255,80,255,255,255,229,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,162,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,46,255,255,255,229,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,148,0,0,0,0,
    255,255,255,24,255,255,255,244,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,250,255,255,255,154,
    255,255,255,25,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,15,255,255,255,249,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,228,
    255,255,255,28,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,30,
    255,255,255,230,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,248,255,255,255,14,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,24,
    255,255,255,142,255,255,255,243,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,252,255,255,255,43,
    255,255,255,127,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,238,255,255,255,157,
    255,255,255,127,255,255,255,84,255,255,255,28,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,87,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,78,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,81,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,85,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,7,255,255,255,46,255,255,255,80,
    255,255,255,103,255,255,255,204,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,143,
    255,255,255,202,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,226,255,255,255,27,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,132,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,229,255,255,255,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,2,255,255,255,230,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,130,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,7,255,255,255,205,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,211,
    255,255,255,237,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,115,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,158,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,182,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,183,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,157,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,105,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,242,
    255,255,255,247,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,95,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,157,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,183,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,185,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,156,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,105,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,242,
    255,255,255,221,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,177,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,132,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,230,255,255,255,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,2,255,255,255,231,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,130,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,8,255,255,255,206,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,211,
    255,255,255,161,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,157,255,255,255,49,
    255,255,255,27,255,255,255,1,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,86,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,80,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,83,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,84,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,4,255,255,255,37,255,255,255,75,
    255,255,255,105,255,255,255,206,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,141,
    255,255,255,68,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,246,255,255,255,198,255,255,255,87,
    255,255,255,3,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,14,255,255,255,249,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,229,
    255,255,255,30,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,31,
    255,255,255,231,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,248,255,255,255,13,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,10,
    255,255,255,117,255,255,255,230,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,252,255,255,255,42,
    0,0,0,0,255,255,255,185,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,195,255,255,255,18,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,163,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,230,255,255,255,82,255,255,255,2,0,0,0,0,
    0,0,0,0,255,255,255,2,255,255,255,83,255,255,255,231,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,161,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,21,255,255,255,200,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,146,0,0,0,0,
    0,0,0,0,255,255,255,22,255,255,255,223,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,211,255,255,255,9,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,32,255,255,255,248,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,231,255,255,255,183,
    255,255,255,183,255,255,255,232,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,247,255,255,255,30,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,4,255,255,255,207,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,188,255,255,255,6,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,27,255,255,255,200,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,133,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,97,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,102,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,113,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,252,
    255,255,255,146,255,255,255,6,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,2,
    255,255,255,95,255,255,255,197,255,255,255,253,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,247,255,255,255,11,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,137,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,134,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,220,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,242,255,255,255,210,255,255,255,142,255,255,255,42,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,13,255,255,255,42,
    255,255,255,59,255,255,255,162,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,74,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,102,255,255,255,247,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,247,255,255,255,99,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,36,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,156,255,255,255,13,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,4,255,255,255,231,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,117,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,31,255,255,255,162,
    255,255,255,248,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,248,
    255,255,255,161,255,255,255,30,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,63,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,21,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,5,255,255,255,233,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,109,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,14,255,255,255,86,255,255,255,130,255,255,255,158,
    255,255,255,158,255,255,255,129,255,255,255,85,255,255,255,13,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,55,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,59,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,146,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,79,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,16,255,255,255,249,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,224,255,255,255,21,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,95,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,242,255,255,255,8,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,179,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,183,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,11,
    255,255,255,231,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,134,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,50,255,255,255,249,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,66,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,92,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,203,255,255,255,6,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,112,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,160,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,161,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,237,255,255,255,27,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,177,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,221,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,190,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,156,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,17,255,255,255,51,255,255,255,67,255,255,255,35,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,11,
    255,255,255,75,255,255,255,119,255,255,255,112,255,255,255,80,
    255,255,255,9,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,96,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,247,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,190,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,156,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,52,255,255,255,179,
    255,255,255,249,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,220,255,255,255,115,255,255,255,7,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,10,255,255,255,134,255,255,255,248,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,243,255,255,255,136,255,255,255,6,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,118,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,237,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,160,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,238,255,255,255,28,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,113,255,255,255,249,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,219,255,255,255,33,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,19,255,255,255,213,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,203,255,255,255,28,
    0,0,0,0,0,0,0,0,255,255,255,28,255,255,255,229,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,199,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,90,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,228,255,255,255,117,255,255,255,95,
    255,255,255,178,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,222,
    255,255,255,25,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,3,
    255,255,255,197,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,238,
    255,255,255,158,255,255,255,159,255,255,255,239,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,125,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,11,
    255,255,255,231,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,155,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,88,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,243,
    255,255,255,22,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,94,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,250,255,255,255,28,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,198,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,230,255,255,255,230,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,106,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,141,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,224,255,255,255,59,255,255,255,23,255,255,255,134,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,84,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,1,255,255,255,245,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,160,255,255,255,3,255,255,255,5,255,255,255,146,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,140,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,108,255,255,255,243,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,182,
    255,255,255,21,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,220,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,128,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,26,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,60,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,94,255,255,255,230,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,229,255,255,255,92,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,22,
    255,255,255,126,255,255,255,201,255,255,255,237,255,255,255,247,
    255,255,255,221,255,255,255,158,255,255,255,65,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,188,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,158,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,50,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,41,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,10,255,255,255,89,255,255,255,159,
    255,255,255,189,255,255,255,189,255,255,255,158,255,255,255,88,
    255,255,255,10,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,159,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,239,255,255,255,28,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,159,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,252,
    255,255,255,13,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,89,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,228,255,255,255,118,
    255,255,255,95,255,255,255,179,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,196,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,11,255,255,255,231,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,94,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,94,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,198,255,255,255,2,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,140,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,223,255,255,255,26,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,109,
    255,255,255,243,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,182,255,255,255,21,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,23,255,255,255,125,255,255,255,201,255,255,255,235,
    255,255,255,247,255,255,255,219,255,255,255,158,255,255,255,66,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const int g_ps4_set_icon_rgba_W=64, g_ps4_set_icon_rgba_H=64;
static const uint8_t g_ps4_pow_icon_rgba[16384] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,48,255,255,255,150,
    255,255,255,150,255,255,255,48,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,48,255,255,255,247,255,255,255,255,
    255,255,255,255,255,255,255,247,255,255,255,48,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,150,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,150,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,113,255,255,255,223,255,255,255,224,
    255,255,255,117,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,1,255,255,255,122,
    255,255,255,226,255,255,255,222,255,255,255,108,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,126,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,89,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,95,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,118,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,102,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,163,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,170,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,254,
    255,255,255,93,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,55,255,255,255,250,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,133,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,140,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,247,255,255,255,48,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,15,255,255,255,222,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,224,255,255,255,20,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,24,255,255,255,227,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,215,255,255,255,11,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,151,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,235,
    255,255,255,40,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,43,
    255,255,255,237,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,140,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,50,255,255,255,252,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,251,255,255,255,59,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,68,255,255,255,253,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,250,255,255,255,41,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,189,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,122,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,132,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,178,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,53,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,211,255,255,255,4,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,5,255,255,255,215,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,254,
    255,255,255,44,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,169,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,60,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,70,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,159,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,16,255,255,255,248,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,187,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,197,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,245,255,255,255,12,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,99,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,72,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,81,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,89,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,176,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,229,
    255,255,255,3,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,5,
    255,255,255,236,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,166,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,2,255,255,255,237,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,145,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,170,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,170,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,155,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,229,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,43,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,78,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,163,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,163,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,88,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,34,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,84,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,19,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,91,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,91,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,27,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,75,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,122,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,231,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,125,255,255,255,235,
    255,255,255,235,255,255,255,125,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,240,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,111,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,148,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,196,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,206,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,138,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,160,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,179,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,189,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,151,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,171,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,169,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,178,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,163,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,165,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,175,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,185,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,155,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,153,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,189,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,198,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,143,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,133,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,219,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,228,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,123,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,97,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,251,255,255,255,6,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,12,255,255,255,254,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,87,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,59,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,57,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,66,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,50,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,10,255,255,255,249,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,120,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,130,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,245,255,255,255,6,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,198,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,202,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,211,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,187,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,128,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,38,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,46,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,118,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,36,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,148,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,158,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,254,255,255,255,32,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,203,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,245,255,255,255,24,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,30,255,255,255,249,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,195,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,92,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,159,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,170,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,83,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,5,
    255,255,255,225,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,254,255,255,255,64,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,72,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,218,
    255,255,255,3,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,97,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,227,255,255,255,20,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,24,255,255,255,232,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,88,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,2,255,255,255,200,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,189,
    255,255,255,7,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,9,
    255,255,255,196,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,192,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,49,255,255,255,249,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,169,255,255,255,3,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,5,255,255,255,175,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,246,255,255,255,43,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,114,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,177,255,255,255,9,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,11,255,255,255,180,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,102,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,1,255,255,255,174,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,201,255,255,255,32,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,36,255,255,255,205,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,164,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,9,
    255,255,255,196,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,239,
    255,255,255,93,255,255,255,1,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,2,255,255,255,99,
    255,255,255,241,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,189,
    255,255,255,7,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,17,255,255,255,206,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,196,255,255,255,60,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,65,255,255,255,201,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,202,255,255,255,14,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,14,255,255,255,189,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,196,
    255,255,255,94,255,255,255,14,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,16,255,255,255,97,
    255,255,255,201,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,183,255,255,255,12,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,255,255,255,7,255,255,255,156,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,246,255,255,255,187,255,255,255,125,
    255,255,255,77,255,255,255,40,255,255,255,17,255,255,255,3,
    255,255,255,4,255,255,255,17,255,255,255,41,255,255,255,79,
    255,255,255,127,255,255,255,189,255,255,255,248,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,148,255,255,255,5,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,89,255,255,255,241,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,238,255,255,255,82,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,25,255,255,255,162,255,255,255,254,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,254,255,255,255,162,255,255,255,24,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,58,
    255,255,255,181,255,255,255,254,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,254,255,255,255,177,
    255,255,255,54,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,255,255,255,46,255,255,255,147,255,255,255,233,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,230,255,255,255,144,255,255,255,42,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,255,255,255,3,
    255,255,255,66,255,255,255,129,255,255,255,193,255,255,255,238,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,238,255,255,255,190,255,255,255,127,255,255,255,63,
    255,255,255,3,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,20,255,255,255,51,255,255,255,68,255,255,255,82,
    255,255,255,81,255,255,255,68,255,255,255,50,255,255,255,19,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const int g_ps4_pow_icon_rgba_W=64, g_ps4_pow_icon_rgba_H=64;



static void drawPS4QuickIcon(int item, float cx, float cy, uint32_t fg, uint32_t hole) {
    (void)hole;
    const uint8_t *icon = NULL;
    int iw = 0, ih = 0;
    switch (item) {
    case PILL_ALL_SOFTWARE: icon = g_ps4_lib_icon_rgba;  iw = g_ps4_lib_icon_rgba_W;  ih = g_ps4_lib_icon_rgba_H;  break;
    case PILL_NETLOADER:    icon = g_ps4_dl_icon_rgba;   iw = g_ps4_dl_icon_rgba_W;   ih = g_ps4_dl_icon_rgba_H;   break;
    case PILL_FAVORITES:    icon = g_ps4_fav_icon_rgba;  iw = g_ps4_fav_icon_rgba_W;  ih = g_ps4_fav_icon_rgba_H;  break;
    case PILL_THEMES:       icon = g_ps4_thm_icon_rgba;   iw = g_ps4_thm_icon_rgba_W;   ih = g_ps4_thm_icon_rgba_H;   break;
    case PILL_SETTINGS:     icon = g_ps4_set_icon_rgba;  iw = g_ps4_set_icon_rgba_W;  ih = g_ps4_set_icon_rgba_H;  break;
    case PILL_EXIT:         icon = g_ps4_pow_icon_rgba;  iw = g_ps4_pow_icon_rgba_W;  ih = g_ps4_pow_icon_rgba_H;  break;
    }
    if (icon)
        renderImageScaledF(icon, iw, ih, cx - 24.0f, cy - 24.0f, 48.0f, 48.0f,
                           RenderImageFormat_Rgba32, fg);
}


static void drawPS4QuickMenu(void) {
    if (s_topbar_t < 0.05f)
        return;
    const float y = 360.0f - (1.0f - s_topbar_t) * 380.0f;
    const uint32_t hole = makeRGBA(8, 19, 48, 255);
    for (int i = 0; i < PS4_QUICK_COUNT; i++) {
        const float cx = SCREEN_W * 0.5f - (PS4_QUICK_COUNT - 1) * 0.5f * 120.0f + (float)i * 120.0f;
        const bool focused = (s_focus == S2Focus_Top && s_top_idx == i);
        if (focused) {
            renderCircleF(cx, y, 38.0f, makeRGBA(40, 90, 180, 150));
            renderCircleF(cx, y, 43.0f, 0xffffffffu);
            renderCircleF(cx, y, 40.0f, hole);
        }
        drawPS4QuickIcon(kPs4QuickItems[i], cx, y, 0xffffffffu, hole);
        static const char *kQuickLabels[PS4_QUICK_COUNT] = {
            "Library", "Netloader", "Favorites", "Themes", "Settings", "Exit",
        };
        uint32_t lw = 0, lh = 0;
        GetTextDimensions(interuiregular14, kQuickLabels[i], &lw, &lh);
        DrawText(interuiregular14, (uint32_t)(cx - (float)lw * 0.5f),
                 (uint32_t)(y + 64.0f),
                 MakeColor(255, 255, 255, focused ? 255 : 170), kQuickLabels[i]);
    }
}

static void drawBottomPillBar(void) {
    bool dark = isDarkTheme();

    /* Authentic Switch 2 floating pill dock: clean solid dark/light pill, no heavy outline,
     * with flat vibrant icons sitting directly on the pill (no individual grey circles) */
    uint32_t pill_bg = dark ? makeRGBA(45, 45, 49, 250) : makeRGBA(255, 255, 255, 252);

    renderSoftShadowF(PILL_X, PILL_Y + 5.0f, PILL_W, PILL_H, PILL_H * 0.5f, 16.0f, dark ? 105 : 38);
    renderRoundedRectF(PILL_X, PILL_Y, PILL_W, PILL_H, PILL_H * 0.5f, pill_bg);

    /* Vibrant Switch 2 icon colors directly on the pill surface */
    uint32_t icon_colors[PILL_COUNT] = {
        makeRGBA(42, 206, 232, 255),  /* Netloader: Switch 2 Cyan */
        makeRGBA(248, 178, 38, 255),  /* Favorites: Warm Gold */
        makeRGBA(238, 66, 74, 255),   /* All Software: Switch 2 Coral-Red */
        makeRGBA(146, 114, 255, 255), /* Themes: Soft Violet */
        dark ? makeRGBA(212, 214, 222, 255) : makeRGBA(82, 86, 96, 255), /* Settings: Gear */
        dark ? makeRGBA(212, 214, 222, 255) : makeRGBA(82, 86, 96, 255), /* Exit: Power */
    };
    static const char *kPillLabels[PILL_COUNT] = {
        "Netloader",
        "Favorites",
        "All Software",
        "Themes",
        "Settings",
        "Exit",
    };

    for (int i = 0; i < PILL_COUNT; i++) {
        float cx = pillButtonCenterX(i);
        float cy = PILL_CENTER_Y;
        bool focused = (s_focus == S2Focus_Pill && s_pill_idx == i);
        float scale = focused ? (1.0f + 0.10f * s_pill_pop) : 1.0f;
        float r = (PILL_BTN_RADIUS + 4.0f) * scale;

        /* Subtle highlight disc only when focused */
        if (focused) {
            uint32_t foc_bg = dark ? makeRGBA(58, 60, 68, 255) : makeRGBA(240, 244, 250, 255);
            renderCircleF(cx, cy, r, foc_bg);
            renderSwitch2CircleFocusRingF(cx, cy, r + 3.5f, 4.0f, ringTimer(),
                                          themeCurrent.highlightColor.abgr,
                                          themeCurrent.highlightGradientEdgeColor.abgr);
        }

        uint32_t hole_bg = focused
            ? (dark ? makeRGBA(58, 60, 68, 255) : makeRGBA(240, 244, 250, 255))
            : pill_bg;
        drawPillIconSymbol(i, cx, cy, scale, icon_colors[i], hole_bg);

        /* Active state indicator dot for toggleable buttons */
        bool is_toggled = (i == PILL_FAVORITES && s_favorites_only) ||
                          (i == PILL_ALL_SOFTWARE && s_view == S2View_Grid) ||
                          (i == PILL_THEMES && menuIsThemeMenuActive());
        if (is_toggled) {
            renderCircleF(cx, PILL_Y + PILL_H - 7.0f, 3.5f,
                          dark ? makeRGBA(78, 224, 245, 255) : makeRGBA(0, 178, 210, 255));
        }

        if (focused) {
            /* Authentic Switch 2: cyan label appears BELOW the pill (like "Nintendo eShop") */
            const char *lbl = kPillLabels[i];
            uint32_t lw = 0, lh = 0;
            GetTextDimensions(interuiregular18, lbl, &lw, &lh);
            float lx = cx - (float)lw * 0.5f;
            float ly = PILL_Y + PILL_H + 28.0f;
            color_t lbl_col = dark ? MakeColor(78, 224, 245, 255) : MakeColor(0, 168, 204, 255);
            DrawText(interuiregular18, (uint32_t)lx, (uint32_t)ly, lbl_col, lbl);
        }
    }
}

static void drawBottomCommandStrip(menu_s *menu) {
    bool dark = isDarkTheme();
    color_t txt = themeCurrent.textColor;
    color_t muted = makeColorMuted(txt, dark ? 145 : 140);

    /* Authentic Switch 2 HOME has NO bottom divider line.
     * Bottom-left: Switch 2 Joy-Con silhouette with P1 green LED + subtle app count */
    float jx = 50.0f, jy = 660.0f;
    uint32_t jc_col = dark ? makeRGBA(225, 227, 232, 255) : makeRGBA(62, 65, 74, 255);
    uint32_t led_on = makeRGBA(130, 235, 65, 255);
    uint32_t led_off = dark ? makeRGBA(76, 78, 86, 255) : makeRGBA(185, 188, 196, 255);
    /* 4 tiny vertical P1..P4 LED dots */
    renderRectF(jx, jy + 2.0f, 4.0f, 4.0f, led_on);
    renderRectF(jx, jy + 9.0f, 4.0f, 4.0f, led_off);
    renderRectF(jx, jy + 16.0f, 4.0f, 4.0f, led_off);
    renderRectF(jx, jy + 23.0f, 4.0f, 4.0f, led_off);
    /* Left & Right Joy-Con pair */
    renderRoundedRectF(jx + 12.0f, jy, 12.0f, 30.0f, 5.5f, jc_col);
    renderRoundedRectF(jx + 27.0f, jy, 12.0f, 30.0f, 5.5f, jc_col);
    uint32_t bg_cut = themeCurrent.backgroundColor.abgr | 0xff000000u;
    renderCircleF(jx + 18.0f, jy + 9.0f, 2.5f, bg_cut);
    renderCircleF(jx + 33.0f, jy + 19.0f, 2.5f, bg_cut);

    /* Subtle count next to controller icon */
    char infoBuf[PATH_MAX + 64];
    if (menuIsThemeMenuActive()) {
        snprintf(infoBuf, sizeof(infoBuf), "Themes (%d)", s_visible_count);
    } else if (s_favorites_only) {
        snprintf(infoBuf, sizeof(infoBuf), "Favorites (%d)", s_visible_count);
    } else {
        snprintf(infoBuf, sizeof(infoBuf), "%d apps", s_visible_count);
    }
    DrawTextTruncate(interuiregular14, 104, 681, muted, infoBuf, 400, "...");

    /* Right: clean Switch 2 button prompts */
    float rx = 1232.0f;
    const uint32_t y_txt = 682;

    /* Helper macro to draw right-aligned "[Glyph] Label" prompt */
    #define DRAW_PROMPT(glyph, label) do { \
        uint32_t lw_ = 0, lh_ = 0, gw_ = 0, gh_ = 0; \
        GetTextDimensions(interuiregular18, (label), &lw_, &lh_); \
        GetTextDimensions(interuiregular18, (glyph), &gw_, &gh_); \
        rx -= (float)lw_; \
        DrawText(interuiregular18, (uint32_t)rx, y_txt, txt, (label)); \
        rx -= ((float)gw_ + 6.0f); \
        DrawText(interuiregular18, (uint32_t)rx, y_txt, txt, (glyph)); \
        rx -= 26.0f; \
    } while (0)

    if (s_focus == S2Focus_Pill) {
        DRAW_PROMPT(themeCurrent.buttonAText, "Select");
        DRAW_PROMPT(themeCurrent.buttonBText, "Back");
    } else {
        if (s_visible_count > 0) {
            if (s_sel == s_visible_count) {
                DRAW_PROMPT(themeCurrent.buttonAText, "All Software");
            } else {
                menuEntry_s *me = s_visible[s_sel];
                const char *a_label = (me->type == ENTRY_TYPE_THEME) ? "Apply" :
                                      (me->type == ENTRY_TYPE_FOLDER) ? "Open" : "Launch";
                DRAW_PROMPT(themeCurrent.buttonAText, a_label);
                if (me->type != ENTRY_TYPE_THEME) {
                    DRAW_PROMPT(themeCurrent.buttonXText, me->starred ? "Unstar" : "Star");
                }
            }
        }
        bool show_back = menuIsThemeMenuActive() || s_view == S2View_Grid || s_favorites_only ||
                         (menu && strcmp(menu->dirname, "sdmc:/") != 0 && strcmp(menu->dirname, "/") != 0);
        if (show_back) {
            DRAW_PROMPT(themeCurrent.buttonBText, "Back");
        }
        if (!menuIsThemeMenuActive()) {
            DRAW_PROMPT(themeCurrent.buttonYText, "Netloader");
            DRAW_PROMPT(themeCurrent.buttonMText, "Themes");
        }
    }
    #undef DRAW_PROMPT
}

/* Settings rows per style: PS4 hides Theme and adds Profile Name and
 * PS4 System Sounds at the end. */
static int settingsRowCount(void) {
    return (s_cfg.ui_style == 1) ? 9 : 9;
}

static int settingsRowItem(int row) {
    if (s_cfg.ui_style != 1)
        return row; /* Switch 2: items 0..8 */
    if (row == 8)
        return SET_PROFILE_NAME;
    return (row >= SET_THEME) ? row + 1 : row;
}

static bool s_name_edit = false;
static int s_name_cursor = 0;

static const char kNameCharset[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";

static void nameEditEnter(void) {
    s_name_edit = true;
    s_name_cursor = (int)strlen(s_cfg.display_name);
    if (s_name_cursor >= 32)
        s_name_cursor = 32; /* positions 0..31; NUL lives at index 32 */
}

static void changeSetting(int idx, int delta) {
    switch (idx) {
    case SET_SOUNDS:
        s_cfg.ui_sounds = s_cfg.ui_sounds ? 0 : 1;
        switch2SfxSetEnabled(s_cfg.ui_sounds != 0);
        break;
    case SET_THEME:
        s_cfg.theme_mode = (s_cfg.theme_mode + delta + 3) % 3;
        switch2ApplyThemeSetting();
        break;
    case SET_UI_STYLE:
        s_cfg.ui_style = s_cfg.ui_style ? 0 : 1;
        s_scroll_x = 0.0f;
        s_grid_scroll_y = 0.0f;
        s_focus = S2Focus_Apps;
        s_focus_pos = (float)s_sel;
        /* Switch 2 keeps its built-in tactile ticks; PS4 uses the pack. */
        if (s_cfg.ui_style == 0) {
            switch2SfxSetBgmEnabled(false);
            switch2SfxResetSynth();
        } else if (sfxPackInstalled()) {
            sfxPackLoadActive();
            if (!s_cfg.ps4_sounds_on)
                switch2SfxResetSynth();
            switch2SfxSetBgmEnabled(s_cfg.bgm_on != 0);
        }
        break;
    case SET_RING_SPEED:
        s_cfg.ring_speed = (s_cfg.ring_speed + delta + 4) % 4;
        break;
    case SET_TRANSITIONS:
        s_cfg.transitions = (s_cfg.transitions + delta + 3) % 3;
        break;
    case SET_DEFAULT_VIEW:
        s_cfg.default_view = s_cfg.default_view ? 0 : 1;
        break;
    case SET_SORT_ORDER:
        s_cfg.sort_order = s_cfg.sort_order ? 0 : 1;
        break;
    case SET_SHOW_SUBTITLE:
        s_cfg.show_subtitle = s_cfg.show_subtitle ? 0 : 1;
        break;
    case SET_HIDE_FOLDERS:
        menuHiddenToggle();
        s_folders_dirty = true;
        break;
    case SET_PROFILE_NAME: {
        (void)delta;
#ifdef __SWITCH2__
        static char initial[32];
        snprintf(initial, sizeof(initial), "%s", s_cfg.display_name);
        SwkbdConfig kbd;
        if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
            swkbdConfigMakePresetDefault(&kbd);
            swkbdConfigSetHeaderText(&kbd, "Profile Name");
            swkbdConfigSetGuideText(&kbd, "Name shown in the bottom bar");
            swkbdConfigSetSubText(&kbd, "Save");
            swkbdConfigSetStringLenMax(&kbd, 32); /* hard 32-char limit */
            swkbdConfigSetInitialText(&kbd, initial);
            if (R_SUCCEEDED(swkbdShow(&kbd, s_cfg.display_name,
                                      sizeof(s_cfg.display_name)))) {
                s_cfg.display_name[strcspn(s_cfg.display_name, "\r\n")] = 0;
                s_cfg.display_name[32] = 0; /* belt and braces */
                if (!s_cfg.display_name[0])
                    snprintf(s_cfg.display_name, sizeof(s_cfg.display_name),
                             "Switchstation 2");
                break;
            }
        }
#endif
        if (!s_name_edit)
            nameEditEnter();
        break;
    }
    }
    switch2SettingsSave();
    switch2SfxPlay(S2Sfx_Confirm);
}

static const char *settingValueText(int idx) {
    switch (idx) {
    case SET_SOUNDS:
        return s_cfg.ui_sounds ? "On" : "Off";
    case SET_THEME:
        if (s_cfg.theme_mode == 1) return "Basic Light";
        if (s_cfg.theme_mode == 2) return "Basic Dark";
        return "Auto (System)";
    case SET_UI_STYLE:
        return s_cfg.ui_style ? "PS4" : "Switch 2";
    case SET_RING_SPEED:
        if (s_cfg.ring_speed == 0) return "Slow";
        if (s_cfg.ring_speed == 1) return "Normal";
        if (s_cfg.ring_speed == 2) return "Fast";
        return "Static";
    case SET_TRANSITIONS:
        if (s_cfg.transitions == 0) return "Slide & Fade";
        if (s_cfg.transitions == 1) return "Fade";
        return "Instant";
    case SET_DEFAULT_VIEW:
        return s_cfg.default_view ? "All Software" : "Carousel";
    case SET_SORT_ORDER:
        return s_cfg.sort_order ? "Alphabetical" : "Favorites First";
    case SET_SHOW_SUBTITLE:
        return s_cfg.show_subtitle ? "On" : "Off";
    case SET_HIDE_FOLDERS:
        return menuHiddenFoldersEnabled() ? "On" : "Off";
    case SET_PROFILE_NAME:
        return s_cfg.display_name;
    case SET_PS4_SOUNDS:
        if (sfxPackState() == SfxDl_Running)
            return "Downloading...";
        if (sfxPackState() == SfxDl_Error)
            return "Failed";
        return sfxPackInstalled() ? "Installed" : "Get";
    default:
        return "";
    }
}

#define SETTINGS_LIST_X  (s_cfg.ui_style == 1 ? 290.0f : 388.0f)
#define SETTINGS_LIST_Y  (s_cfg.ui_style == 1 ? 180.0f : 116.0f)
#define SETTINGS_LIST_W  (s_cfg.ui_style == 1 ? 700.0f : 828.0f)
#define SETTINGS_ROW_H   (s_cfg.ui_style == 1 ? 40.0f : 54.0f)

static float transEase(void) {
    float t = s_trans_t;
    if (t >= 1.0f) return 1.0f;
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv; /* cubic ease-out */
}

static float transSlideOffset(void) {
    if (s_cfg.transitions != 0 || s_trans_t >= 1.0f)
        return 0.0f;
    return (1.0f - transEase()) * 44.0f;
}

static void triggerTransition(void) {
    s_trans_t = (s_cfg.transitions == 2) ? 1.0f : 0.0f;
}

static void scanThemes(void) {
    s_theme_count = 0;
    s_themes_loaded = true;
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/config/nx-hbmenu/themes", menuGetRootBasePath());
    DIR* dp = opendir(dir);
    if (!dp)
        return;
    struct dirent* entry;
    while ((entry = readdir(dp)) && s_theme_count < MAX_THEMES) {
        if (entry->d_name[0] == '.')
            continue;
        const char* ext = strrchr(entry->d_name, '.');
        if (!ext || (strcasecmp(ext, ".cfg") != 0 &&
                     strcasecmp(ext, ".romfs") != 0 &&
                     strcasecmp(ext, ".zip") != 0))
            continue;
        snprintf(s_theme_paths[s_theme_count], PATH_MAX, "%s/%.*s", dir,
                 (int)(PATH_MAX - strlen(dir) - 2), entry->d_name);
        snprintf(s_theme_names[s_theme_count], 96, "%.95s", entry->d_name);
        s_theme_count++;
    }
    closedir(dp);
    if (s_theme_sel >= s_theme_count)
        s_theme_sel = s_theme_count > 0 ? s_theme_count - 1 : 0;
}

static void applyThemeAt(int index) {
    if (index < 0 || index >= s_theme_count)
        return;
    SetThemePathToConfig(s_theme_paths[index]);
    themeStartup(themeGlobalPreset);
    menuStartupCommon();
    switch2SfxPlay(S2Sfx_Confirm);
}

static void refreshPluginSettings(void) {
    hbPluginSettingsLoad(&s_plugin_settings);
    s_plugin_settings_loaded = true;
    if (s_plugin_sel >= s_plugin_settings.count)
        s_plugin_sel = s_plugin_settings.count > 0 ? s_plugin_settings.count - 1 : 0;
}

static void changePluginSetting(int delta) {
    if (s_plugin_sel < 0 || s_plugin_sel >= s_plugin_settings.count)
        return;
    HbPluginSettings *plugin = &s_plugin_settings.plugins[s_plugin_sel];
    if (s_plugin_option_sel < 0 || s_plugin_option_sel >= plugin->option_count)
        return;
    KaihenPluginSetting *option = &plugin->options[s_plugin_option_sel];
    if (strcmp(plugin->reg_name, "nupvfs") == 0) {
        if (strcmp(option->key, HB_PLUGIN_KEY_COPY_SAVES) == 0) {
            if (delta <= 0) return;
            char summary[320];
            bool ok = hbCopyNandSavesToSd(summary, sizeof summary);
            switch2SfxPlay(ok ? S2Sfx_Confirm : S2Sfx_Back);
            menuCreateMsgBox(660, 280, summary);
            return;
        }
        if (strcmp(option->key, HB_PLUGIN_KEY_CLEAR_CACHE) == 0) {
            if (delta <= 0) return;
            char summary[256];
            bool ok = hbClearRomfsCache(summary, sizeof summary);
            switch2SfxPlay(ok ? S2Sfx_Confirm : S2Sfx_Back);
            menuCreateMsgBox(620, 240, summary);
            return;
        }
        if (strcmp(option->key, HB_PLUGIN_KEY_MANAGE_MODS) == 0) {
            if (delta <= 0) return;
            hbModPacksLoad(&s_mod_packs);
            s_mod_pack_sel = 0;
            s_plugin_view = 2;
            switch2SfxPlay(S2Sfx_Confirm);
            return;
        }
    }
    int value = option->value + delta * (option->step ? option->step : 1);
    if (option->type == KaihenPluginSetting_Bool) {
        /* A / arrows flip a Bool instead of forcing a direction, so
         * pressing A on an already-on toggle visibly turns it off. */
        value = option->value ? 0 : 1;
    } else {
        if (value < option->min) value = option->min;
        if (value > option->max) value = option->max;
    }
    char err[96];
    if (hbPluginSettingsSet(plugin, option->key, (uint8_t)value, err, sizeof err)) {
        option->value = (uint8_t)value;
        if (strcmp(plugin->reg_name, "nupvfs") == 0 && strcmp(option->key, "games") == 0)
            s_folders_dirty = true;
        switch2SfxPlay(S2Sfx_Confirm);
    } else {
        switch2SfxPlay(S2Sfx_Back);
        char text[256];
        snprintf(text, sizeof text, "Plugin set failed\n%s", err[0] ? err : "unknown");
        menuCreateMsgBox(560, 260, text);
    }
}

static const char *pluginOptionValue(const KaihenPluginSetting *option, char *buf,
                                     size_t buf_size) {
    if (strcmp(option->key, HB_PLUGIN_KEY_COPY_SAVES) == 0 ||
        strcmp(option->key, HB_PLUGIN_KEY_CLEAR_CACHE) == 0)
        return "Run";
    if (strcmp(option->key, HB_PLUGIN_KEY_MANAGE_MODS) == 0)
        return "Open";
    if (option->type == KaihenPluginSetting_Bool)
        return option->value ? "On" : "Off";
    snprintf(buf, buf_size, "%u", option->value);
    return buf;
}

static void drawPS4ListOutline(float x, float y, float w, float h) {
    /* Orbis focus: full-row glowing selection box, brighter translucent fill */
    renderRectF(x, y, w, h, makeRGBA(18, 68, 148, 217));
    const uint32_t edge = makeRGBA(124, 232, 255, 255); /* #7CE8FF */
    renderRectF(x, y, w, 2.0f, edge);
    renderRectF(x, y + h - 2.0f, w, 2.0f, edge);
    renderRectF(x, y, 2.0f, h, edge);
    renderRectF(x + w - 2.0f, y, 2.0f, h, edge);
}

static void drawPS4RowDivider(float x, float y, float w) {
    renderRectF(x, y, w, 1.0f, makeRGBA(255, 255, 255, 46));
}

static void drawSettingsView(void) {
    bool dark = isDarkTheme();
    color_t txt = themeCurrent.textColor;
    color_t muted = makeColorMuted(txt, dark ? 150 : 145);
    color_t accent = dark ? MakeColor(78, 224, 245, 255) : MakeColor(0, 168, 204, 255);
    uint32_t sep_col = dark ? makeRGBA(62, 64, 72, 255) : makeRGBA(212, 215, 222, 255);

    const bool ps4 = s_cfg.ui_style == 1;
    if (ps4) {
        DrawText(interuimedium30, 72, 64, MakeColor(255, 255, 255, 255), "Settings");
        drawPS4RowDivider(72.0f, 104.0f, SCREEN_W - 144.0f);
        static const char *kTabs[] = { "General", "Plugins", "Themes" };
        for (int tab = 0; tab < 3; tab++) {
            const float tx = 290.0f + (float)tab * 150.0f;
            const bool active = (s_settings_tab == tab);
            DrawText(interuimedium20, (uint32_t)tx, 136,
                     active ? MakeColor(255, 255, 255, 255) : MakeColor(255, 255, 255, 120),
                     kTabs[tab]);
            if (active)
                renderRectF(tx, 158.0f, 90.0f, 2.0f, makeRGBA(140, 210, 255, 230));
        }
    } else {
        /* Top Settings header bar (authentic Switch 2 System Settings style) */
        const float bar_cy = 50.0f;
        renderSettingsIconF(74.0f, bar_cy, 18.0f, txt.abgr | 0xff000000u);
        DrawText(interuimedium20, 108, (uint32_t)(bar_cy + fontBaselineOffset(interuimedium20)), txt, "Settings");

        /* Right status: Clock + Horizontal Battery */
        float right_x = 1232.0f;
        uint32_t batteryCharge = 100;
        bool isCharging = false;
        if (powerGetDetails(&batteryCharge, &isCharging)) {
            if (batteryCharge > 100) batteryCharge = 100;
            right_x -= 40.0f;
            drawHorizontalBattery(right_x, bar_cy - 9.5f, batteryCharge, isCharging, txt, dark);
            right_x -= 22.0f;
        }
        time_t unixTime = time(NULL);
        struct tm *tmNow = localtime(&unixTime);
        char timeStr[16];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", tmNow->tm_hour, tmNow->tm_min);
        uint32_t cw = 0, ch = 0;
        GetTextDimensions(interuimedium20, timeStr, &cw, &ch);
        right_x -= (float)cw;
        DrawText(interuimedium20, (uint32_t)right_x, (uint32_t)(bar_cy + fontBaselineOffset(interuimedium20)), txt, timeStr);

        /* Header & sidebar hairline dividers */
        renderRectF(42.0f, 92.0f, SCREEN_W - 84.0f, 1.0f, sep_col);
        renderRectF(344.0f, 114.0f, 1.0f, 514.0f, sep_col);

        /* Left sidebar sections */
        static const char *kTabs[] = { "General", "Plugins", "Themes" };
        uint32_t cat_bg = dark ? makeRGBA(40, 42, 48, 255) : makeRGBA(246, 248, 252, 255);
        for (int tab = 0; tab < 3; tab++) {
            const float cat_y = 126.0f + (float)tab * 70.0f;
            const bool active = (s_settings_tab == tab);
            if (active) {
                renderRoundedRectF(56.0f, cat_y, 260.0f, 56.0f, 10.0f, cat_bg);
                renderRoundedRectF(66.0f, cat_y + 12.0f, 4.5f, 32.0f, 2.25f,
                                   accent.abgr | 0xff000000u);
            }
            DrawText(interuimedium20, 86,
                     (uint32_t)(cat_y + 28.0f + fontBaselineOffset(interuimedium20)),
                     active ? accent : muted, kTabs[tab]);
        }

        DrawText(interuiregular14, 68, 550, muted, "Switch 2 HOME UI");
        DrawText(interuiregular14, 68, 572, muted, "by ImDella");
        DrawText(interuiregular14, 68, 594, muted, "Base: ouncebrew / switchbrew");
        DrawText(interuiregular14, 68, 616, muted, "Saved automatically to SD");
    }

    float y_off = transSlideOffset();

    /* Right settings list */
    if (s_settings_tab == SETTINGS_TAB_THEMES) {
        if (!s_themes_loaded)
            scanThemes();
        if (ps4 && sfxPackInstalled()) {
            /* Installed PS4 sound pack: toggle row pinned at the top */
            const float ry = SETTINGS_LIST_Y;
            const float rcy = ry + SETTINGS_ROW_H * 0.5f;
            const bool focused = (s_theme_sel == 0);
            drawPS4RowDivider(SETTINGS_LIST_X, ry + SETTINGS_ROW_H, SETTINGS_LIST_W);
            if (focused)
                drawPS4ListOutline(SETTINGS_LIST_X, ry + 2.0f, SETTINGS_LIST_W, SETTINGS_ROW_H - 4.0f);
            DrawText(interuiregular18, (uint32_t)(SETTINGS_LIST_X + 28.0f),
                     (uint32_t)(rcy + fontBaselineOffset(interuiregular18)),
                     focused ? txt : makeColorMuted(txt, dark ? 220 : 210),
                     "PS4 Orbis OS");
            const char *st = s_cfg.ps4_sounds_on ? "On" : "Off";
            uint32_t vw = 0, vh = 0;
            GetTextDimensions(interuiregular18, st, &vw, &vh);
            DrawText(interuiregular18,
                     (uint32_t)(SETTINGS_LIST_X + SETTINGS_LIST_W - 26.0f - (float)vw),
                     (uint32_t)(rcy + fontBaselineOffset(interuiregular18)),
                     s_cfg.ps4_sounds_on ? accent : muted, st);
        }
        if (s_theme_count == 0) {
            if (!(ps4 && sfxPackInstalled()))
                DrawText(interuiregular18, (uint32_t)(SETTINGS_LIST_X + 28.0f),
                         SETTINGS_LIST_Y + 24.0f, muted, "No themes installed.");
        } else {
            char current[PATH_MAX] = {0};
            GetThemePathFromConfig(current, sizeof(current));
            const int row_base = (ps4 && sfxPackInstalled()) ? 1 : 0;
            const int row_max = (ps4 && sfxPackInstalled()) ? 8 : 9;
            for (int i = 0; i < s_theme_count && i < row_max; i++) {
                float ry = SETTINGS_LIST_Y + (float)(i + row_base) * SETTINGS_ROW_H + y_off;
                float rcy = ry + SETTINGS_ROW_H * 0.5f;
                bool focused = (i == s_theme_sel - row_base);
                const float label_x = SETTINGS_LIST_X + 28.0f;
                drawPS4RowDivider(SETTINGS_LIST_X, ry + SETTINGS_ROW_H, SETTINGS_LIST_W);
                if (ps4) {
                    if (focused)
                        drawPS4ListOutline(SETTINGS_LIST_X, ry + 2.0f, SETTINGS_LIST_W, SETTINGS_ROW_H - 4.0f);
                } else if (focused) {
                    uint32_t row_bg = dark ? makeRGBA(42, 44, 52, 255) : makeRGBA(255, 255, 255, 255);
                    renderRoundedRectF(SETTINGS_LIST_X, ry + 4.0f, SETTINGS_LIST_W,
                                       SETTINGS_ROW_H - 8.0f, 12.0f, row_bg);
                }
                DrawTextTruncate(interuiregular18, (uint32_t)label_x,
                                 (uint32_t)(rcy + fontBaselineOffset(interuiregular18)),
                                 focused ? txt : makeColorMuted(txt, dark ? 220 : 210),
                                 s_theme_names[i], 560, "...");
                const size_t plen_i = strlen(s_theme_paths[i]);
                const bool zip_i = plen_i >= 4 &&
                    strcasecmp(s_theme_paths[i] + plen_i - 4, ".zip") == 0;
                if (zip_i && ps4) {
                    uint32_t vw = 0, vh = 0;
                    GetTextDimensions(interuiregular18, "Install", &vw, &vh);
                    DrawText(interuiregular18,
                             (uint32_t)(SETTINGS_LIST_X + SETTINGS_LIST_W - 26.0f - (float)vw),
                             (uint32_t)(rcy + fontBaselineOffset(interuiregular18)),
                             focused ? accent : muted, "Install");
                } else if (strcmp(current, s_theme_paths[i]) == 0) {
                    uint32_t vw = 0, vh = 0;
                    GetTextDimensions(interuiregular18, "Current", &vw, &vh);
                    DrawText(interuiregular18,
                             (uint32_t)(SETTINGS_LIST_X + SETTINGS_LIST_W - 26.0f - (float)vw),
                             (uint32_t)(rcy + fontBaselineOffset(interuiregular18)),
                             focused ? accent : muted, "Current");
                }
            }
        }
        if (s_theme_status[0])
            DrawTextTruncate(interuiregular14, (uint32_t)SETTINGS_LIST_X,
                             (uint32_t)(SETTINGS_LIST_Y + 9.0f * SETTINGS_ROW_H + 18.0f),
                             MakeColor(255, 255, 255, 200), s_theme_status,
                             (int)SETTINGS_LIST_W, "...");
    } else if (s_settings_tab == SETTINGS_TAB_PLUGINS) {
        if (!s_plugin_settings_loaded)
            refreshPluginSettings();
        if (s_plugin_view == 0) {
            if (s_plugin_settings.count == 0) {
                const char *msg = s_plugin_settings.hen_available
                    ? "No plugin settings exposed."
                    : "kaiHEN is not available.";
                DrawText(interuiregular18, (uint32_t)(SETTINGS_LIST_X + 28.0f),
                         SETTINGS_LIST_Y + 24.0f, muted, msg);
            } else {
                for (int i = 0; i < s_plugin_settings.count; i++) {
                    float ry = SETTINGS_LIST_Y + (float)i * SETTINGS_ROW_H + y_off;
                    float rcy = ry + SETTINGS_ROW_H * 0.5f;
                    bool focused = (i == s_plugin_sel);
                    const float label_x = SETTINGS_LIST_X + 28.0f;
                    drawPS4RowDivider(SETTINGS_LIST_X, ry + SETTINGS_ROW_H, SETTINGS_LIST_W);
                    if (ps4) {
                        if (focused)
                            drawPS4ListOutline(SETTINGS_LIST_X, ry + 2.0f, SETTINGS_LIST_W, SETTINGS_ROW_H - 4.0f);
                    } else if (focused) {
                        uint32_t row_bg = dark ? makeRGBA(42, 44, 52, 255) : makeRGBA(255, 255, 255, 255);
                        renderRoundedRectF(SETTINGS_LIST_X, ry + 4.0f, SETTINGS_LIST_W,
                                           SETTINGS_ROW_H - 8.0f, 12.0f, row_bg);
                        renderRoundedRectF(SETTINGS_LIST_X + 10.0f, ry + 18.0f, 4.5f,
                                           SETTINGS_ROW_H - 36.0f, 2.25f,
                                           accent.abgr | 0xff000000u);
                    }
                    DrawTextTruncate(interuiregular18, (uint32_t)label_x,
                                     (uint32_t)(rcy + fontBaselineOffset(interuiregular18)),
                                     focused ? txt : makeColorMuted(txt, dark ? 220 : 210),
                                     s_plugin_settings.plugins[i].name, 560, "...");
                    char countBuf[32];
                    snprintf(countBuf, sizeof(countBuf), "%d options",
                             s_plugin_settings.plugins[i].option_count);
                    uint32_t vw = 0, vh = 0;
                    GetTextDimensions(interuiregular18, countBuf, &vw, &vh);
                    DrawText(interuiregular18,
                             (uint32_t)(SETTINGS_LIST_X + SETTINGS_LIST_W - 26.0f - (float)vw),
                             (uint32_t)(rcy + fontBaselineOffset(interuiregular18)),
                             focused ? accent : muted, countBuf);
                }
            }
        } else if (s_plugin_view == 2) {
            if (s_mod_packs.count == 0) {
                DrawText(interuiregular18, (uint32_t)(SETTINGS_LIST_X + 28.0f),
                         (uint32_t)(SETTINGS_LIST_Y + 42.0f + y_off), muted,
                         "No mod packs in /kaihen/mods/<tid>/packs/<name>");
            } else {
                const int max_vis = 8;
                int start = s_mod_pack_sel >= max_vis ? (s_mod_pack_sel - max_vis + 1) : 0;
                int end = start + max_vis;
                if (end > s_mod_packs.count) end = s_mod_packs.count;
                for (int i = start; i < end; i++) {
                    const HbModPackEntry *pack = &s_mod_packs.packs[i];
                    float ry = SETTINGS_LIST_Y + (float)(i - start) * SETTINGS_ROW_H + y_off;
                    float rcy = ry + SETTINGS_ROW_H * 0.5f;
                    bool focused = (i == s_mod_pack_sel);
                    const float label_x = SETTINGS_LIST_X + 28.0f;
                    if (ps4) {
                        if (focused)
                            drawPS4ListOutline(SETTINGS_LIST_X, ry + 2.0f, SETTINGS_LIST_W, SETTINGS_ROW_H - 4.0f);
                    } else if (focused) {
                        uint32_t row_bg = dark ? makeRGBA(42, 44, 52, 255) : makeRGBA(255, 255, 255, 255);
                        renderRoundedRectF(SETTINGS_LIST_X, ry + 4.0f, SETTINGS_LIST_W,
                                           SETTINGS_ROW_H - 8.0f, 12.0f, row_bg);
                        renderRoundedRectF(SETTINGS_LIST_X + 10.0f, ry + 18.0f, 4.5f,
                                           SETTINGS_ROW_H - 36.0f, 2.25f,
                                           accent.abgr | 0xff000000u);
                    }
                    DrawTextTruncate(interuiregular18, (uint32_t)label_x,
                                     (uint32_t)(rcy + fontBaselineOffset(interuiregular18)),
                                     focused ? txt : makeColorMuted(txt, dark ? 220 : 210),
                                     pack->label, 560, "...");
                    const char *vstr = pack->enabled ? "On" : "Off";
                    char valBuf[32];
                    snprintf(valBuf, sizeof(valBuf), focused ? "\u25C2  %s  \u25B8" : "%s", vstr);
                    uint32_t vw = 0, vh = 0;
                    GetTextDimensions(interuiregular18, valBuf, &vw, &vh);
                    DrawText(interuiregular18,
                             (uint32_t)(SETTINGS_LIST_X + SETTINGS_LIST_W - 26.0f - (float)vw),
                             (uint32_t)(rcy + fontBaselineOffset(interuiregular18)),
                             focused ? accent : muted, valBuf);
                }
            }
        } else if (s_plugin_sel < s_plugin_settings.count) {
            const HbPluginSettings *plugin = &s_plugin_settings.plugins[s_plugin_sel];
            for (int i = 0; i < plugin->option_count; i++) {
                const KaihenPluginSetting *option = &plugin->options[i];
                float ry = SETTINGS_LIST_Y + (float)i * SETTINGS_ROW_H + y_off;
                float rcy = ry + SETTINGS_ROW_H * 0.5f;
                bool focused = (i == s_plugin_option_sel);
                const float label_x = SETTINGS_LIST_X + 28.0f;
                if (ps4) {
                    if (focused)
                        drawPS4ListOutline(SETTINGS_LIST_X, ry + 2.0f, SETTINGS_LIST_W, SETTINGS_ROW_H - 4.0f);
                } else if (focused) {
                    uint32_t row_bg = dark ? makeRGBA(42, 44, 52, 255) : makeRGBA(255, 255, 255, 255);
                    renderRoundedRectF(SETTINGS_LIST_X, ry + 4.0f, SETTINGS_LIST_W,
                                       SETTINGS_ROW_H - 8.0f, 12.0f, row_bg);
                    renderRoundedRectF(SETTINGS_LIST_X + 10.0f, ry + 18.0f, 4.5f,
                                       SETTINGS_ROW_H - 36.0f, 2.25f,
                                       accent.abgr | 0xff000000u);
                }
                DrawTextTruncate(interuiregular18, (uint32_t)label_x,
                                 (uint32_t)(rcy + fontBaselineOffset(interuiregular18)),
                                 focused ? txt : makeColorMuted(txt, dark ? 220 : 210),
                                 option->label, 560, "...");
                char valueBuf[32];
                const char *value = pluginOptionValue(option, valueBuf, sizeof(valueBuf));
                char valBuf[64];
                if (strcmp(option->key, HB_PLUGIN_KEY_COPY_SAVES) == 0 ||
                    strcmp(option->key, HB_PLUGIN_KEY_CLEAR_CACHE) == 0 ||
                    strcmp(option->key, HB_PLUGIN_KEY_MANAGE_MODS) == 0)
                    snprintf(valBuf, sizeof(valBuf), focused ? "[A] %s" : "%s", value);
                else if (focused)
                    snprintf(valBuf, sizeof(valBuf), "\u25C2  %s  \u25B8", value);
                else
                    snprintf(valBuf, sizeof(valBuf), "%s", value);
                uint32_t vw = 0, vh = 0;
                GetTextDimensions(interuiregular18, valBuf, &vw, &vh);
                DrawText(interuiregular18,
                         (uint32_t)(SETTINGS_LIST_X + SETTINGS_LIST_W - 26.0f - (float)vw),
                         (uint32_t)(rcy + fontBaselineOffset(interuiregular18)),
                         focused ? accent : muted, valBuf);
            }
        }
    } else {
    static const char *kLabels[SET_COUNT] = {
        "UI Sounds",
        "Theme",
        "UI Style",
        "Focus Border Speed",
        "Transitions",
        "Default View",
        "Sort Order",
        "Show Author & Version",
        "Hide Folders",
        "Profile Name",
        "PS4 System Sounds",
        "Background Music",
    };

    for (int i = 0; i < settingsRowCount(); i++) {
        const int item = settingsRowItem(i);
        float ry = SETTINGS_LIST_Y + (float)i * SETTINGS_ROW_H + y_off;
        float rcy = ry + SETTINGS_ROW_H * 0.5f;
        bool focused = (i == s_settings_sel);

        if (!ps4 && i > 0)
            renderRectF(SETTINGS_LIST_X + 12.0f, ry, SETTINGS_LIST_W - 24.0f, 1.0f, sep_col);

        float label_x = SETTINGS_LIST_X + 28.0f;
        if (ps4) {
            drawPS4RowDivider(SETTINGS_LIST_X, ry + SETTINGS_ROW_H, SETTINGS_LIST_W);
            if (focused)
                drawPS4ListOutline(SETTINGS_LIST_X, ry + 2.0f, SETTINGS_LIST_W, SETTINGS_ROW_H - 4.0f);
        } else if (focused) {
            uint32_t row_bg = dark ? makeRGBA(42, 44, 52, 255) : makeRGBA(255, 255, 255, 255);
            renderRoundedRectF(SETTINGS_LIST_X, ry + 4.0f, SETTINGS_LIST_W, SETTINGS_ROW_H - 8.0f, 12.0f, row_bg);
            renderRoundedRectF(SETTINGS_LIST_X + 10.0f, ry + 18.0f, 4.5f, SETTINGS_ROW_H - 36.0f, 2.25f, accent.abgr | 0xff000000u);
        }

        /* Setting name on left */
        DrawText(interuiregular18, (uint32_t)label_x,
                 (uint32_t)(rcy + fontBaselineOffset(interuiregular18)),
                 focused || ps4 ? txt : makeColorMuted(txt, dark ? 220 : 210), kLabels[item]);

        if (item == SET_PS4_SOUNDS && sfxPackState() == SfxDl_Running) {
            const float bw = 140.0f;
            const float bx = SETTINGS_LIST_X + SETTINGS_LIST_W - 26.0f - bw;
            renderRectF(bx, rcy - 3.0f, bw, 6.0f, makeRGBA(255, 255, 255, 60));
            renderRectF(bx, rcy - 3.0f, bw * (float)sfxPackProgress() / 100.0f, 6.0f,
                        makeRGBA(124, 232, 255, 255));
            char pct[16];
            snprintf(pct, sizeof(pct), "%u%%", sfxPackProgress());
            uint32_t pw = 0, ph = 0;
            GetTextDimensions(interuiregular14, pct, &pw, &ph);
            DrawText(interuiregular14, (uint32_t)(bx - (float)pw - 10.0f),
                     (uint32_t)(rcy + fontBaselineOffset(interuiregular14)),
                     MakeColor(255, 255, 255, 220), pct);
            continue;
        }
        if (item == SET_PS4_SOUNDS && sfxPackState() == SfxDl_Error) {
            DrawTextTruncate(interuiregular14,
                             (uint32_t)(SETTINGS_LIST_X + SETTINGS_LIST_W - 26.0f - 360.0f),
                             (uint32_t)(rcy + fontBaselineOffset(interuiregular14)),
                             MakeColor(255, 255, 255, 190), sfxPackStatusText(), 360, "...");
            continue;
        }
        /* Value on right ("< Value >" when focused, clean value otherwise) */
        const char *val = settingValueText(item);
        char valBuf[64];
        if (item == SET_PROFILE_NAME && s_name_edit)
            snprintf(valBuf, sizeof(valBuf), "%.*s_", s_name_cursor, s_cfg.display_name);
        else if (focused)
            snprintf(valBuf, sizeof(valBuf), "\u25C2  %s  \u25B8", val);
        else
            snprintf(valBuf, sizeof(valBuf), "%s", val);

        uint32_t vw = 0, vh = 0;
        GetTextDimensions(interuiregular18, valBuf, &vw, &vh);
        color_t val_col = focused ? accent : muted;
        DrawText(interuiregular18, (uint32_t)(SETTINGS_LIST_X + SETTINGS_LIST_W - 26.0f - (float)vw),
                 (uint32_t)(rcy + fontBaselineOffset(interuiregular18)), val_col, valBuf);
    }
    }

    if (ps4) {
        drawPS4BottomBar(s_settings_tab == SETTINGS_TAB_PLUGINS && s_plugin_view == 0
                             ? "Open" : "Change", "Back");
        return;
    }

    /* Footer bar */
    renderRectF(42.0f, 644.0f, SCREEN_W - 84.0f, 1.0f, sep_col);
    float rx = 1232.0f;
    const uint32_t y_txt = 682;
    #define DRAW_SET_PROMPT(glyph, label) do { \
        uint32_t lw_ = 0, lh_ = 0, gw_ = 0, gh_ = 0; \
        GetTextDimensions(interuiregular18, (label), &lw_, &lh_); \
        GetTextDimensions(interuiregular18, (glyph), &gw_, &gh_); \
        rx -= (float)lw_; \
        DrawText(interuiregular18, (uint32_t)rx, y_txt, txt, (label)); \
        rx -= ((float)gw_ + 6.0f); \
        DrawText(interuiregular18, (uint32_t)rx, y_txt, txt, (glyph)); \
        rx -= 28.0f; \
    } while (0)

    DrawText(interuiregular18, 56, y_txt, muted, "L/R  Section");
    DRAW_SET_PROMPT(themeCurrent.buttonAText,
                    s_settings_tab == SETTINGS_TAB_PLUGINS && s_plugin_view == 0
                        ? "Open" : "Change");
    if (s_settings_tab == SETTINGS_TAB_PLUGINS)
        DRAW_SET_PROMPT(themeCurrent.buttonYText, "Refresh");
    DRAW_SET_PROMPT(themeCurrent.buttonBText, "Back");
    #undef DRAW_SET_PROMPT
}

static void updateAnimations(void) {
    sfxPackPoll();
    /* Trigger elastic pop spring whenever focused item changes */
    if (s_sel != s_last_sel || (s_focus == S2Focus_Apps && s_last_focus != S2Focus_Apps)) {
        s_app_pop = 0.0f;
        s_app_pop_vel = 0.28f;
        s_ps4_panel_t = 0.0f;
        s_last_sel = s_sel;
    }
    if (s_pill_idx != s_last_pill || (s_focus == S2Focus_Pill && s_last_focus != S2Focus_Pill)) {
        s_pill_pop = 0.0f;
        s_pill_pop_vel = 0.30f;
        s_last_pill = s_pill_idx;
    }
    s_last_focus = s_focus;

    /* Damped spring integration (60fps) -> snappy Switch 2 bounce */
    s_app_pop_vel += (1.0f - s_app_pop) * 0.24f;
    s_app_pop_vel *= 0.62f;
    s_app_pop += s_app_pop_vel;

    s_pill_pop_vel += (1.0f - s_pill_pop) * 0.26f;
    s_pill_pop_vel *= 0.60f;
    s_pill_pop += s_pill_pop_vel;
    /* View transition animation (~240ms cubic ease-out at 60fps) */
    if (s_view != s_last_view) {
        s_last_view = s_view;
        triggerTransition();
    }
    if (s_trans_t < 1.0f) {
        s_trans_t += 0.068f;
        if (s_trans_t > 1.0f) s_trans_t = 1.0f;
    }
    if (s_ps4_boot_ms < 5000.0f) {
        s_ps4_boot_ms += 16.6667f;
        if (!s_ps4_boot_chime_played && s_ps4_boot_ms >= 3200.0f) {
            s_ps4_boot_chime_played = true;
            if (s_cfg.ps4_sounds_on)
                switch2SfxPlay(S2Sfx_Startup);
        }
        if (s_ps4_boot_ms > 5000.0f) s_ps4_boot_ms = 5000.0f;
    }
    if (s_ps4_launch_active) {
        s_ps4_launch_ms += 16.6667f;
        if (s_ps4_launch_ms >= 1500.0f)
            s_ps4_launch_ms = 1500.0f;
    }
    /* One continuous focus value; the row is a pure function of it. */
    const float target = (float)s_sel;
    s_focus_pos += (target - s_focus_pos) * (1.0f - expf(-19.8f / 60.0f));
    if (fabsf(target - s_focus_pos) < 0.0005f)
        s_focus_pos = target;
    {
        const float delta = s_focus_pos - s_last_focus_pos;
        s_dust_parallax -= delta * 2.2f;
        if (s_dust_parallax > 48.0f) s_dust_parallax = 48.0f;
        if (s_dust_parallax < -48.0f) s_dust_parallax = -48.0f;
        s_last_focus_pos = s_focus_pos;
    }
    {
        const float top_target = (s_cfg.ui_style == 1 && s_focus == S2Focus_Top &&
                                  (s_view == S2View_Carousel || s_view == S2View_Grid))
                                     ? 1.0f : 0.0f;
        s_topbar_t += (top_target - s_topbar_t) * 0.18f;
        if (fabsf(top_target - s_topbar_t) < 0.002f) s_topbar_t = top_target;
    }
    if (fabsf(s_focus_pos - (float)s_sel) < 0.15f) {
        if (s_ps4_panel_t < 1.0f) {
            s_ps4_panel_t += 0.10f;
            if (s_ps4_panel_t > 1.0f) s_ps4_panel_t = 1.0f;
        }
    } else {
        s_ps4_panel_t = 0.0f;
    }
}

/* --- Public Entry Points -------------------------------------------------- */

static void drawPS4BootOverlay(void) {
    if (s_cfg.ui_style != 1 || s_ps4_boot_ms >= 5000.0f)
        return;
    const float ms = s_ps4_boot_ms;

    /* 0..3500ms: solid #000000; 3500..5000ms: home background fades in (0->1, EASE_SOFT) */
    const float bg_op = easeSoft(segProg(ms, 3500.0f, 5000.0f));
    const uint8_t black_a = (uint8_t)(255.0f * (1.0f - bg_op));
    if (black_a > 0)
        renderRectF(0.0f, 0.0f, SCREEN_W, SCREEN_H, makeRGBA(0, 0, 0, black_a));

    /* Logo visible 1000..4200ms:
     *   1000..2500 opacity 0->1 (EASE_OUT_SOFT)
     *   1000..3500 scale 0.95->1.00 (EASE_OUT_SOFT)
     *   1000..3000 soft white glow 0->24px @ 40% white (EASE_SOFT)
     *   1800..3200 shimmer sweep left->right @ 20% white (EASE_SOFT)
     *   3500..4200 opacity 1->0 (EASE_IN_SOFT) */
    if (ms >= 1000.0f && ms <= 4200.0f) {
        float logo_op = easeOutSoft(segProg(ms, 1000.0f, 2500.0f));
        if (ms > 3500.0f)
            logo_op *= (1.0f - easeInSoft(segProg(ms, 3500.0f, 4200.0f)));
        const float sc = 0.95f + 0.05f * easeOutSoft(segProg(ms, 1000.0f, 3500.0f));
        const float glow_p = easeSoft(segProg(ms, 1000.0f, 3000.0f));
        const float cx = SCREEN_W * 0.5f;
        const float cy = SCREEN_H * 0.5f;

        /* Crisp centered white logo text ("Switchstation 2") */
        const char *mark = "Switchstation 2";
        uint32_t mw = 0, mh = 0;
        GetTextDimensions(interuimedium30, mark, &mw, &mh);
        const float lx = cx - (float)mw * 0.5f;
        const float ly = cy + (float)mh * 0.25f;
        const uint8_t la = (uint8_t)(255.0f * logo_op);
        DrawText(interuimedium30, (uint32_t)lx, (uint32_t)ly, MakeColor(255, 255, 255, la), mark);
        (void)glow_p;

        /* 1800..3200ms: linear light sweep left->right across the mark (20% white) */
        if (ms >= 1800.0f && ms <= 3200.0f) {
            const float sp = easeSoft(segProg(ms, 1800.0f, 3200.0f));
            const float sx = (lx - 30.0f) + sp * ((float)mw + 60.0f);
            const uint8_t sa = (uint8_t)(51.0f * logo_op * sinf(sp * 3.14159265f));
            renderRectF(sx, cy - 22.0f * sc, 28.0f, 44.0f * sc, makeRGBA(255, 255, 255, sa));
        }
    }
}

static void drawPS4LaunchOverlay(void) {
    if (!s_ps4_launch_active || !s_ps4_launch_entry)
        return;
    const float ms = s_ps4_launch_ms;

    /* 250..600ms: UI chrome dims out (EASE_IN_SOFT); 400..1200ms: bg dims 1->0.2 (EASE_SOFT) */
    const float chrome_hide = easeInSoft(segProg(ms, 250.0f, 600.0f));
    const float bg_dim = easeSoft(segProg(ms, 400.0f, 1200.0f));
    const float cover_a = chrome_hide * 0.72f + bg_dim * 0.28f;
    if (cover_a > 0.005f) {
        renderRectF(0.0f, 0.0f, SCREEN_W, SCREEN_H,
                    makeRGBA(5, 7, 15, (uint8_t)(240.0f * (cover_a > 1.0f ? 1.0f : cover_a))));
    }

    /* 0..100ms: press-in 1.12->1.09; 100..250ms: rebound 1.09->1.12;
     * 250..1200ms: scale to cover viewport & move to center (EASE_SOFT) */
    float sc_mul = 1.0f;
    if (ms < 100.0f)
        sc_mul = 1.0f - (0.03f / 1.12f) * easeSoft(segProg(ms, 0.0f, 100.0f));
    else if (ms < 250.0f)
        sc_mul = (1.09f / 1.12f) + (0.03f / 1.12f) * easeSoft(segProg(ms, 100.0f, 250.0f));

    const float z = easeSoft(segProg(ms, 250.0f, 1200.0f));
    const float start_w = s_ps4_launch_sw * sc_mul;
    const float start_h = s_ps4_launch_sh * sc_mul;
    const float start_x = s_ps4_launch_sx + (s_ps4_launch_sw - start_w) * 0.5f;
    const float start_y = s_ps4_launch_sy + (s_ps4_launch_sh - start_h) * 0.5f;

    const float target_sz = SCREEN_H; /* fit-to-screen centered cover */
    const float target_x = (SCREEN_W - target_sz) * 0.5f;
    const float target_y = 0.0f;

    const float ix = start_x + (target_x - start_x) * z;
    const float iy = start_y + (target_y - start_y) * z;
    const float iw = start_w + (target_sz - start_w) * z;
    const float ih = start_h + (target_sz - start_h) * z;

    /* 800..1500ms: cover art opacity 1 -> 0 (EASE_SOFT) */
    const float art_op = 1.0f - easeSoft(segProg(ms, 800.0f, 1500.0f));
    const uint8_t ia = (uint8_t)(255.0f * (art_op < 0.0f ? 0.0f : art_op));
    if (ia > 1) {
        const uint8_t *img = NULL;
        int img_w = 0, img_h = 0;
        enum RenderImageFormat fmt = RenderImageFormat_Rgb24;
        if (s_ps4_launch_entry->icon_gfx) {
            img = s_ps4_launch_entry->icon_gfx;
            img_w = 256; img_h = 256;
            fmt = (s_ps4_launch_entry->icon_mode == IMAGE_MODE_RGBA32)
                      ? RenderImageFormat_Rgba32 : RenderImageFormat_Rgb24;
        }
        if (img)
            renderImageScaledF(img, img_w, img_h, ix, iy, iw, ih, fmt,
                               ((uint32_t)ia << 24) | 0x00ffffffu);
        else
            renderRectF(ix, iy, iw, ih, makeRGBA(18, 52, 120, ia));
    }

    /* 1000..1500ms: #05070f loading backdrop opacity 0 -> 1 (EASE_OUT_SOFT) */
    const float load_bg = easeOutSoft(segProg(ms, 1000.0f, 1500.0f));
    if (load_bg > 0.01f) {
        renderRectF(0.0f, 0.0f, SCREEN_W, SCREEN_H,
                    makeRGBA(5, 7, 15, (uint8_t)(255.0f * load_bg)));
    }

    /* 1200..1500ms: bottom-right 10-dot ring (48px from edges, 36px diameter, 360deg/1.2s) */
    const float ind_op = easeOutSoft(segProg(ms, 1200.0f, 1500.0f));
    if (ind_op > 0.01f) {
        const float rcx = SCREEN_W - 48.0f - 18.0f;
        const float rcy = SCREEN_H - 48.0f - 18.0f;
        const float ring_r = 15.0f;
        const float base_ang = (ms / 1200.0f) * 6.2831853f;
        const int dots = 10;
        for (int d = 0; d < dots; d++) {
            const float ang = base_ang - (float)d * (6.2831853f / (float)dots);
            const float dx = rcx + cosf(ang) * ring_r;
            const float dy = rcy + sinf(ang) * ring_r;
            const float tail = 1.0f - (float)d / (float)dots;
            const uint8_t da = (uint8_t)(255.0f * ind_op * (0.18f + 0.82f * tail));
            renderCircleF(dx, dy, 2.6f, makeRGBA(255, 255, 255, da));
        }
    }
}

static void triggerEntryLaunchConfirmed(void *arg) {
    menuEntry_s *me = (menuEntry_s *)arg;
    if (!me) return;
    if (s_cfg.ui_style == 1) {
        switch2SfxPlay(S2Sfx_Launch);
        s_ps4_launch_active = true;
        s_ps4_launch_ms = 0.0f;
        s_ps4_launch_entry = me;
        if (s_view == S2View_Grid) {
            s_ps4_launch_sw = s_ps4_launch_sh = 256.0f;
            s_ps4_launch_sx = (SCREEN_W - 256.0f) * 0.5f;
            s_ps4_launch_sy = (SCREEN_H - 256.0f) * 0.5f;
        } else {
            homeTileRect(s_sel, &s_ps4_launch_sx, &s_ps4_launch_sy, &s_ps4_launch_sw, &s_ps4_launch_sh);
            if (s_ps4_launch_sh > s_ps4_launch_sw)
                s_ps4_launch_sh = s_ps4_launch_sw;
        }
        return;
    }
    launchMenuEntry(me);
}

static void triggerEntryLaunch(menuEntry_s *me) {
    if (!me) return;
    const bool is_launchable = (me->type != ENTRY_TYPE_FOLDER && me->type != ENTRY_TYPE_THEME);
    if (is_launchable && s_cfg.ui_style == 1) {
        if (launchMenuEntryNeedsPlatformWarning(me)) {
            switch2SfxPlay(S2Sfx_DialogOpen);
            menuCreateConfirmMsgBox(780, 300, textGetString(StrId_Switch1Warning),
                                    triggerEntryLaunchConfirmed, me);
            return;
        }
        triggerEntryLaunchConfirmed(me);
        return;
    }
    switch2SfxPlay(is_launchable ? S2Sfx_Launch : S2Sfx_Confirm);
    launchMenuEntryTask(me);
    syncSelectionFromMenu(menuGetCurrent());
}

static void drawTransitionVeil(void) {
    if (s_cfg.transitions == 2 || s_trans_t >= 1.0f)
        return;
    float alpha_f = (1.0f - transEase()) * 215.0f;
    uint8_t a = (uint8_t)alpha_f;
    if (a > 0) {
        uint32_t bg = (themeCurrent.backgroundColor.abgr & 0x00ffffffu) | ((uint32_t)a << 24);
        renderRectF(0.0f, 0.0f, SCREEN_W, SCREEN_H, bg);
    }
}

void switch2MenuDraw(void) {
    menu_s *menu = menuGetCurrent();
    rebuildVisibleList(menu);
    updateAnimations();

    if (s_cfg.ui_style == 1) {
        drawPS4Background();
    } else {
        renderRectF(0.0f, 0.0f, SCREEN_W, SCREEN_H, themeCurrent.backgroundColor.abgr | 0xff000000u);
        ThemeLayoutObject *bgObj = &themeCurrent.layoutObjects[ThemeLayoutId_BackgroundImage];
        assetsDataEntry *bgData = NULL;
        assetsGetData(AssetId_background_image, &bgData);
        if (bgObj->visible && bgData && bgData->buffer) {
            enum RenderImageFormat fmt = (bgData->imageMode == IMAGE_MODE_RGB24) ? RenderImageFormat_Rgb24 : RenderImageFormat_Rgba32;
            renderImageScaledF(bgData->buffer, bgData->imageSize[0], bgData->imageSize[1],
                               0.0f, 0.0f, SCREEN_W, SCREEN_H, fmt, 0xffffffffu);
        }
    }

    if (s_view == S2View_Settings) {
        drawSettingsView();
        drawTransitionVeil();
        return;
    }

    if (!menuIsNetloaderActive()) {
        if (s_view == S2View_Grid && !menuIsThemeMenuActive())
            drawGridView();
        else
            drawCarouselView();

        if (s_cfg.ui_style == 1 && !menuIsThemeMenuActive()) {
            drawPS4StatusBar();
            drawPS4QuickMenu();
            drawPS4BottomBar("Enter", "Back");
        } else {
            drawTopBar(menu);
            drawBottomPillBar();
            drawBottomCommandStrip(menu);
        }
    } else if (s_cfg.ui_style == 1) {
        drawPS4StatusBar();
    } else {
        drawTopBar(menu);
    }

    drawTransitionVeil();
    drawPS4BootOverlay();
    drawPS4LaunchOverlay();
}

bool switch2MenuUpdate(u64 down) {
    menu_s *menu = menuGetCurrent();
    if (s_cfg.ui_style != 1 && s_focus == S2Focus_Top)
        s_focus = S2Focus_Apps;
    if (s_cfg.ui_style == 1 && s_focus == S2Focus_Pill)
        s_focus = S2Focus_Apps;
    rebuildVisibleList(menu);

    if (s_exit_requested) {
        s_exit_requested = false;
        return true;
    }

    /* PS4 game start animation: hold input until 1500ms Sequence B finishes, then launch */
    if (s_ps4_launch_active) {
        if (s_ps4_launch_ms >= 1500.0f && s_ps4_launch_entry) {
            menuEntry_s *target = s_ps4_launch_entry;
            s_ps4_launch_active = false;
            s_ps4_launch_ms = 0.0f;
            s_ps4_launch_entry = NULL;
            launchMenuEntry(target);
            syncSelectionFromMenu(menuGetCurrent());
        }
        return false;
    }

    /* Allow skipping PS4 startup overlay with A or B */
    if (s_cfg.ui_style == 1 && s_ps4_boot_ms < 5000.0f && (down & (HidNpadButton_A | HidNpadButton_B))) {
        s_ps4_boot_ms = 5000.0f;
        if (!s_ps4_boot_chime_played) {
            s_ps4_boot_chime_played = true;
            if (s_cfg.ps4_sounds_on)
                switch2SfxPlay(S2Sfx_Startup);
        }
        return false;
    }

    /* Confirmation / message box handling MUST run before Settings so dialogs
     * opened from Settings (Copy Saves, Clear Cache, errors) consume A/B. */
    if (menuIsConfirmMsgBoxOpen()) {
        if (down & HidNpadButton_A) { switch2SfxPlay(S2Sfx_Confirm); menuHandleAButton(); }
        else if (down & HidNpadButton_B) { switch2SfxPlay(S2Sfx_Back); launchMenuBackTask(); }
        return false;
    }
    if (menuIsMsgBoxOpen() && !menuIsNetloaderActive()) {
        if (down & (HidNpadButton_A | HidNpadButton_B)) { switch2SfxPlay(S2Sfx_Back); menuCloseMsgBox(); }
        return false;
    }

    /* Full-screen Switch 2 Settings navigation */
    if (s_view == S2View_Settings) {
        if (down & HidNpadButton_Plus) {
            /* Plus closes Settings back to the menu (never return true, which exits without nextLoad) */
            switch2SfxPlay(S2Sfx_Back);
            switch2SettingsSave();
            s_name_edit = false;
            s_plugin_view = 0;
            s_view = s_prev_view;
            s_focus = s_prev_focus;
            s_top_idx = s_prev_top_idx;
            return false;
        }
        if (down & (HidNpadButton_L | HidNpadButton_R)) {
            const int step = (down & HidNpadButton_R) ? 1 : 2;
            s_settings_tab = (s_settings_tab + step) % 3;
            s_plugin_view = 0;
            s_plugin_sel = 0;
            s_plugin_option_sel = 0;
            if (s_settings_tab == SETTINGS_TAB_PLUGINS)
                refreshPluginSettings();
            if (s_settings_tab == SETTINGS_TAB_THEMES) {
                s_theme_sel = 0;
                scanThemes();
            }
            switch2SfxPlay(S2Sfx_Move);
            return false;
        }
        if (down & HidNpadButton_B) {
            switch2SfxPlay(S2Sfx_Back); /* cancel.wav on every menu exit */
            if (s_settings_tab == SETTINGS_TAB_PLUGINS && s_plugin_view == 2) {
                s_plugin_view = 1;
                return false;
            }
            if (s_settings_tab == SETTINGS_TAB_PLUGINS && s_plugin_view == 1) {
                s_plugin_view = 0;
                s_plugin_option_sel = 0;
                return false;
            }
            s_view = s_prev_view;
            s_name_edit = false;
            if (s_cfg.ui_style == 1) {
                s_focus = s_prev_focus;
                s_top_idx = s_prev_top_idx;
                s_topbar_t = (s_prev_focus == S2Focus_Top) ? 1.0f : 0.0f;
            } else {
                s_focus = S2Focus_Pill;
            }
            if (s_folders_dirty) {
                menuScan(s_settings_return_dir[0] ? s_settings_return_dir : menuGetRootPath());
                s_folders_dirty = false;
                syncSelectionFromMenu(menuGetCurrent());
            }
            return false;
        }

        if (s_settings_tab == SETTINGS_TAB_THEMES) {
            const bool pack_row = s_cfg.ui_style == 1 && sfxPackInstalled();
            const int max_sel = pack_row ? s_theme_count : (s_theme_count - 1);
            const int theme_idx = pack_row ? s_theme_sel - 1 : s_theme_sel;
            if (down & HidNpadButton_AnyUp) {
                if (s_theme_sel > 0) { s_theme_sel--; switch2SfxPlay(S2Sfx_Move); }
            } else if (down & HidNpadButton_AnyDown) {
                if (s_theme_sel < max_sel) { s_theme_sel++; switch2SfxPlay(S2Sfx_Move); }
            } else if (down & HidNpadButton_A && pack_row && s_theme_sel == 0) {
                /* Toggle the installed PS4 sound pack on/off */
                if (s_cfg.ps4_sounds_on) {
                    s_cfg.ps4_sounds_on = 0;
                    switch2SfxResetSynth(); /* music keeps playing per bgm_on */
                    snprintf(s_theme_status, sizeof(s_theme_status), "PS4 sounds off");
                    switch2SfxPlay(S2Sfx_Back);
                } else {
                    s_cfg.ps4_sounds_on = 1;
                    sfxPackLoadActive();
                    snprintf(s_theme_status, sizeof(s_theme_status), "PS4 sounds on");
                    switch2SfxPlay(S2Sfx_Notification);
                }
                switch2SettingsSave();
                return false;
            } else if (down & HidNpadButton_A) {
                const char *tpath = (theme_idx >= 0 && theme_idx < s_theme_count)
                                        ? s_theme_paths[theme_idx] : NULL;
                if (s_cfg.ui_style == 1 && tpath) {
                    const size_t plen = strlen(tpath);
                    const bool is_zip = plen >= 4 &&
                        strcasecmp(tpath + plen - 4, ".zip") == 0;
                    if (is_zip && !sfxPackZipLooksLikeTheme(tpath)) {
                        char err[128] = "";
                        const int n = sfxPackInstallZip(tpath, err, sizeof(err));
                        if (n > 0) {
                            remove(tpath);
                            scanThemes();
                            s_theme_sel = 0;
                            s_cfg.ps4_sounds_on = 1;
                            switch2SettingsSave();
                            sfxPackLoadActive();
                            snprintf(s_theme_status, sizeof(s_theme_status),
                                     "Installed PS4 sounds (%d cues) - toggle in Themes", n);
                            switch2SfxPlay(S2Sfx_Notification);
                        } else {
                            snprintf(s_theme_status, sizeof(s_theme_status),
                                     "Install failed: %s", err);
                            switch2SfxPlay(S2Sfx_Back);
                        }
                        return false;
                    }
                }
                applyThemeAt(theme_idx);
            }
            return false;
        }

        if (s_settings_tab == SETTINGS_TAB_PLUGINS && (down & HidNpadButton_Y)) {
            refreshPluginSettings();
            switch2SfxPlay(S2Sfx_Confirm);
            return false;
        }

        if (s_settings_tab == SETTINGS_TAB_PLUGINS) {
            if (s_plugin_view == 0) {
                if (down & HidNpadButton_AnyUp) {
                    if (s_plugin_sel > 0) {
                        s_plugin_sel--;
                        switch2SfxPlay(S2Sfx_Move);
                    }
                } else if (down & HidNpadButton_AnyDown) {
                    if (s_plugin_sel + 1 < s_plugin_settings.count) {
                        s_plugin_sel++;
                        switch2SfxPlay(S2Sfx_Move);
                    }
                } else if (down & HidNpadButton_A) {
                    if (s_plugin_sel < s_plugin_settings.count) {
                        s_plugin_view = 1;
                        s_plugin_option_sel = 0;
                        switch2SfxPlay(S2Sfx_Confirm);
                    }
                }
                return false;
            }

            if (s_plugin_view == 2) {
                if (down & HidNpadButton_AnyUp) {
                    if (s_mod_pack_sel > 0) {
                        s_mod_pack_sel--;
                        switch2SfxPlay(S2Sfx_Move);
                    }
                } else if (down & HidNpadButton_AnyDown) {
                    if (s_mod_pack_sel + 1 < s_mod_packs.count) {
                        s_mod_pack_sel++;
                        switch2SfxPlay(S2Sfx_Move);
                    }
                } else if (down & (HidNpadButton_A | HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) {
                    if (s_mod_pack_sel >= 0 && s_mod_pack_sel < s_mod_packs.count) {
                        hbModPackToggle(&s_mod_packs.packs[s_mod_pack_sel]);
                        switch2SfxPlay(S2Sfx_Confirm);
                    }
                }
                return false;
            }

            const int option_count = (s_plugin_sel < s_plugin_settings.count)
                                         ? s_plugin_settings.plugins[s_plugin_sel].option_count : 0;
            if (down & HidNpadButton_AnyUp) {
                if (s_plugin_option_sel > 0) {
                    s_plugin_option_sel--;
                    switch2SfxPlay(S2Sfx_Move);
                }
            } else if (down & HidNpadButton_AnyDown) {
                if (s_plugin_option_sel + 1 < option_count) {
                    s_plugin_option_sel++;
                    switch2SfxPlay(S2Sfx_Move);
                }
            } else if (down & (HidNpadButton_AnyRight | HidNpadButton_A)) {
                changePluginSetting(+1);
            } else if (down & HidNpadButton_AnyLeft) {
                changePluginSetting(-1);
            }
            return false;
        }

        if (s_name_edit) {
            /* Profile name editor: up/down cycle the char, left/right move, X deletes */
            const int nchars = (int)strlen(kNameCharset);
            if (down & (HidNpadButton_A | HidNpadButton_B)) {
                s_name_edit = false;
                switch2SfxPlay(S2Sfx_Back);
                switch2SettingsSave();
            } else if (down & HidNpadButton_AnyLeft) {
                if (s_name_cursor > 0) s_name_cursor--;
            } else if (down & HidNpadButton_AnyRight) {
                if (s_name_cursor < (int)strlen(s_cfg.display_name) &&
                    s_name_cursor < 32)
                    s_name_cursor++;
            } else if (down & HidNpadButton_AnyUp || down & HidNpadButton_AnyDown) {
                const int step = (down & HidNpadButton_AnyUp) ? 1 : -1;
                char *c = &s_cfg.display_name[s_name_cursor];
                if (!c[0] && s_name_cursor < 32) {
                    c[0] = kNameCharset[0];
                    c[1] = 0;
                }
                if (c[0]) {
                    const char *pos = strchr(kNameCharset, *c);
                    int idx = pos ? (int)(pos - kNameCharset) : 0;
                    idx = (idx + step + nchars) % nchars;
                    *c = kNameCharset[idx];
                }
                switch2SfxPlay(S2Sfx_Move);
            } else if (down & HidNpadButton_X) {
                memmove(&s_cfg.display_name[s_name_cursor],
                        &s_cfg.display_name[s_name_cursor + 1],
                        strlen(&s_cfg.display_name[s_name_cursor + 1]) + 1);
            }
            return false;
        }
        if (down & HidNpadButton_AnyUp) {
            if (s_settings_sel > 0) {
                s_settings_sel--;
                switch2SfxPlay(S2Sfx_Move);
            }
        } else if (down & HidNpadButton_AnyDown) {
            if (s_settings_sel < settingsRowCount() - 1) {
                s_settings_sel++;
                switch2SfxPlay(S2Sfx_Move);
            }
        } else if (down & (HidNpadButton_AnyRight | HidNpadButton_A)) {
            changeSetting(settingsRowItem(s_settings_sel), +1);
        } else if (down & HidNpadButton_AnyLeft) {
            changeSetting(settingsRowItem(s_settings_sel), -1);
        }
        return false;
    }

    /* While Netloader is active, only allow B (cancel) */
    if (menuIsNetloaderActive()) {
        if (down & HidNpadButton_B) {
            switch2SfxPlay(S2Sfx_Back);
            launchMenuBackTask();
        }
        return false;
    }

    if (down & HidNpadButton_Y) {
        if (!menuIsThemeMenuActive()) {
            switch2SfxPlay(S2Sfx_DialogOpen);
            launchMenuNetloaderTask();
        }
        return false;
    }
    if (down & HidNpadButton_Minus) {
        if (s_cfg.ui_style == 1) {
            switch2SfxPlay(S2Sfx_EnterMenu);
            menu_s *home = menuGetCurrent();
            snprintf(s_settings_return_dir, sizeof(s_settings_return_dir), "%s",
                     home ? home->dirname : menuGetRootPath());
            s_prev_focus = s_focus;
            s_prev_top_idx = s_top_idx;
            s_prev_view = (s_view == S2View_Grid) ? S2View_Grid : S2View_Carousel;
            s_view = S2View_Settings;
            s_settings_tab = SETTINGS_TAB_THEMES;
            s_theme_sel = 0;
            if (!s_themes_loaded)
                scanThemes();
            s_focus = S2Focus_Apps;
        } else {
            triggerPillAction(PILL_THEMES);
        }
        return false;
    }

    /* Top bar (PS4 style): Library and Settings. */
    if (s_focus == S2Focus_Top) {
        if (down & HidNpadButton_AnyLeft) {
            if (s_top_idx > 0) { s_top_idx--; switch2SfxPlay(S2Sfx_Move); }
        } else if (down & HidNpadButton_AnyRight) {
            if (s_top_idx < PS4_QUICK_COUNT - 1) { s_top_idx++; switch2SfxPlay(S2Sfx_Move); }
        } else if (down & HidNpadButton_AnyDown) {
            s_focus = S2Focus_Apps;
            switch2SfxPlay(S2Sfx_DockExit);
        } else if (down & HidNpadButton_A) {
            const int item = kPs4QuickItems[s_top_idx];
            if (item == PILL_SETTINGS || item == PILL_THEMES) {
                switch2SfxPlay(S2Sfx_EnterMenu);
                menu_s *home = menuGetCurrent();
                snprintf(s_settings_return_dir, sizeof(s_settings_return_dir), "%s",
                         home ? home->dirname : menuGetRootPath());
                s_prev_focus = s_focus;
                s_prev_top_idx = s_top_idx;
                s_prev_view = (s_view == S2View_Grid) ? S2View_Grid : S2View_Carousel;
                s_view = S2View_Settings;
                s_settings_sel = 0;
                s_settings_tab = (item == PILL_THEMES) ? SETTINGS_TAB_THEMES : SETTINGS_TAB_GENERAL;
                s_plugin_view = 0;
                if (item == PILL_THEMES) { s_theme_sel = 0; scanThemes(); }
            } else {
                switch2SfxPlay(S2Sfx_Confirm);
                triggerPillAction(item);
            }
        } else if (down & HidNpadButton_B) {
            s_focus = S2Focus_Apps;
        }
        return false;
    }

    /* Pill bar focus navigation */
    if (s_focus == S2Focus_Pill) {
        if (down & HidNpadButton_AnyLeft) {
            if (s_pill_idx > 0) { s_pill_idx--; switch2SfxPlay(S2Sfx_Move); }
        } else if (down & HidNpadButton_AnyRight) {
            if (s_pill_idx < PILL_COUNT - 1) { s_pill_idx++; switch2SfxPlay(S2Sfx_Move); }
        } else if (down & (HidNpadButton_AnyUp | HidNpadButton_B)) {
            if (s_visible_count > 0) {
                s_focus = S2Focus_Apps;
                switch2SfxPlay(S2Sfx_Move);
            }
        } else if (down & HidNpadButton_A) {
            triggerPillAction(s_pill_idx);
        }
        return false;
    }

    /* App list focus navigation */
    if (down & HidNpadButton_B) {
        switch2SfxPlay(S2Sfx_Back);
        if (s_view == S2View_Grid) {
            s_view = S2View_Carousel;
        } else if (s_favorites_only && !menuIsThemeMenuActive()) {
            s_favorites_only = false;
            syncSelectionFromMenu(menu);
        } else {
            launchMenuBackTask();
            s_sel = 0;
            s_scroll_x = 0.0f;
            s_grid_scroll_y = 0.0f;
            syncSelectionFromMenu(menuGetCurrent());
        }
        return false;
    }

    if (down & HidNpadButton_A) {
        if (s_visible_count > 0) {
            if (s_sel == s_visible_count && hasEndAllSoftwareTile()) {
                switch2SfxPlay(S2Sfx_Confirm);
                s_view = S2View_Grid;
                s_sel = 0;
                s_grid_scroll_y = 0.0f;
                menu->curEntry = s_visible_count > 0 ? s_visible_indices[0] : 0;
            } else if (s_sel < s_visible_count) {
                menu->curEntry = s_visible_indices[s_sel];
                triggerEntryLaunch(s_visible[s_sel]);
            }
        }
        return false;
    }

    if (down & HidNpadButton_X) {
        if (s_visible_count > 0 && s_sel < s_visible_count && !menuIsThemeMenuActive()) {
            switch2SfxPlay(S2Sfx_Star);
            menuEntry_s *target = s_visible[s_sel];
            toggleStarState(target);
            syncSelectionFromMenu(menuGetCurrent());
        }
        return false;
    }

    int total_slots = s_visible_count + (hasEndAllSoftwareTile() ? 1 : 0);
    int prev_sel = s_sel;
    S2FocusZone prev_focus = s_focus;

    if (s_view == S2View_Carousel) {
        if (down & HidNpadButton_AnyLeft) {
            if (s_sel > 0) s_sel--;
        } else if (down & HidNpadButton_AnyRight) {
            if (s_sel < total_slots - 1) s_sel++;
        } else if (down & HidNpadButton_ZL) {
            s_sel -= 4;
            if (s_sel < 0) s_sel = 0;
        } else if (down & HidNpadButton_ZR) {
            s_sel += 4;
            if (s_sel >= total_slots) s_sel = total_slots > 0 ? total_slots - 1 : 0;
        } else if (down & HidNpadButton_AnyUp) {
            if (s_cfg.ui_style == 1) {
                s_focus = S2Focus_Top;
                switch2SfxPlay(S2Sfx_DockEnter);
            }
        } else if (down & HidNpadButton_AnyDown) {
            if (s_cfg.ui_style != 1)
                s_focus = S2Focus_Pill;
        }
    } else {
        /* 5-column All Software Grid navigation */
        if (down & HidNpadButton_AnyLeft) {
            if (s_sel > 0) s_sel--;
        } else if (down & HidNpadButton_AnyRight) {
            if (s_sel < s_visible_count - 1) s_sel++;
        } else if (down & HidNpadButton_AnyUp) {
            if (s_sel >= gridCols()) s_sel -= gridCols();
            else if (s_cfg.ui_style == 1) {
                s_focus = S2Focus_Top;
                switch2SfxPlay(S2Sfx_DockEnter);
            }
        } else if (down & HidNpadButton_AnyDown) {
            if (s_sel + gridCols() < s_visible_count)
                s_sel += gridCols();
            else
                s_focus = S2Focus_Pill;
        } else if (down & HidNpadButton_ZL) {
            s_sel -= gridCols() * 2;
            if (s_sel < 0) s_sel = 0;
        } else if (down & HidNpadButton_ZR) {
            s_sel += gridCols() * 2;
            if (s_sel >= s_visible_count) s_sel = s_visible_count > 0 ? s_visible_count - 1 : 0;
        }
    }

    if (s_focus != prev_focus)
        switch2SfxPlay(S2Sfx_Move); /* zone change tick; dock cues play at their own sites */
    else if (s_sel != prev_sel)
        switch2SfxPlay(S2Sfx_Move);
    else
        boundaryEdgeTick(down);

    if (s_visible_count > 0 && s_sel < s_visible_count)
        menu->curEntry = s_visible_indices[s_sel];

    return false;
}

void switch2HandlePointer(int x, int y, bool left_click, bool right_click,
                          bool moved) {
    if (menuIsConfirmMsgBoxOpen()) {
        if (left_click) { switch2SfxPlay(S2Sfx_Confirm); menuHandleAButton(); }
        else if (right_click) { switch2SfxPlay(S2Sfx_Back); launchMenuBackTask(); }
        return;
    }
    if (menuIsMsgBoxOpen() && !menuIsNetloaderActive()) {
        if (left_click || right_click) { switch2SfxPlay(S2Sfx_Back); menuCloseMsgBox(); }
        return;
    }

    if (right_click) {
        switch2SfxPlay(S2Sfx_Back);
        launchMenuBackTask();
        return;
    }

    if (s_view == S2View_Settings) {
        if (left_click) {
            if (s_cfg.ui_style == 1 && y >= 120 && y <= 170 &&
                x >= 290 && x <= 380) {
                s_settings_tab = SETTINGS_TAB_GENERAL;
                s_plugin_view = 0;
            } else if (s_cfg.ui_style == 1 && y >= 120 && y <= 170 &&
                       x > 380 && x <= 530) {
                s_settings_tab = SETTINGS_TAB_PLUGINS;
                s_plugin_view = 0;
                refreshPluginSettings();
            } else if (s_cfg.ui_style == 1 && y >= 120 && y <= 170 &&
                       x > 530 && x <= 680) {
                s_settings_tab = SETTINGS_TAB_THEMES;
                s_theme_sel = 0;
                scanThemes();
            } else if (s_cfg.ui_style != 1 && x >= 56 && x <= 316 && y >= 126 && y <= 182) {
                s_settings_tab = SETTINGS_TAB_GENERAL;
                s_plugin_view = 0;
            } else if (s_cfg.ui_style != 1 && x >= 56 && x <= 316 && y >= 196 && y <= 252) {
                s_settings_tab = SETTINGS_TAB_PLUGINS;
                s_plugin_view = 0;
                refreshPluginSettings();
            } else if (s_cfg.ui_style != 1 && x >= 56 && x <= 316 && y >= 266 && y <= 322) {
                s_settings_tab = SETTINGS_TAB_THEMES;
                s_theme_sel = 0;
                scanThemes();
            } else if (x >= (int)SETTINGS_LIST_X &&
                       x <= (int)(SETTINGS_LIST_X + SETTINGS_LIST_W)) {
                if (s_settings_tab == SETTINGS_TAB_GENERAL) {
                    const int row = (int)(((float)y - SETTINGS_LIST_Y) / SETTINGS_ROW_H);
                    if (row >= 0 && row < SET_COUNT) {
                        s_settings_sel = row;
                        changeSetting(row, +1);
                    }
                } else if (s_plugin_view == 0) {
                    const int row = (int)(((float)y - SETTINGS_LIST_Y) / SETTINGS_ROW_H);
                    if (row >= 0 && row < s_plugin_settings.count) {
                        s_plugin_sel = row;
                        s_plugin_view = 1;
                        s_plugin_option_sel = 0;
                    }
                } else if (s_plugin_view == 2) {
                    const int row = (int)(((float)y - SETTINGS_LIST_Y) / SETTINGS_ROW_H);
                    const int start = s_mod_pack_sel >= 8 ? (s_mod_pack_sel - 7) : 0;
                    const int idx = start + row;
                    if (row >= 0 && idx < s_mod_packs.count) {
                        s_mod_pack_sel = idx;
                        hbModPackToggle(&s_mod_packs.packs[idx]);
                        switch2SfxPlay(S2Sfx_Confirm);
                    }
                } else if (s_plugin_sel < s_plugin_settings.count) {
                    const int row = (int)(((float)y - SETTINGS_LIST_Y) / SETTINGS_ROW_H);
                    if (row >= 0 && row < s_plugin_settings.plugins[s_plugin_sel].option_count) {
                        s_plugin_option_sel = row;
                        changePluginSetting(+1);
                    }
                }
            }
        }
        return;
    }

    if (menuIsMsgBoxOpen()) {
        if (left_click)
            menuHandleAButton();
        return;
    }

    menu_s *menu = menuGetCurrent();
    rebuildVisibleList(menu);
    if (s_focus == S2Focus_Pill && y >= (int)(PILL_Y - 10) &&
        y <= (int)(PILL_Y + PILL_H + 10) &&
        x >= (int)(PILL_X - 10) && x <= (int)(PILL_X + PILL_W + 10)) {
        for (int i = 0; i < PILL_COUNT; i++) {
            const float dx = (float)x - pillButtonCenterX(i);
            const float dy = (float)y - PILL_CENTER_Y;
            if (dx * dx + dy * dy <= (PILL_BTN_RADIUS + 10.0f) * (PILL_BTN_RADIUS + 10.0f)) {
                s_pill_idx = i;
                if (left_click)
                    triggerPillAction(i);
                return;
            }
        }
    }

    if (s_view == S2View_Carousel) {
        const int slots = s_visible_count + (hasEndAllSoftwareTile() ? 1 : 0);
        for (int i = 0; i < slots; i++) {
            float tx, ty, tw, th;
            homeTileRect(i, &tx, &ty, &tw, &th);
            if (x >= (int)tx && x <= (int)(tx + tw) &&
                y >= (int)ty && y <= (int)(ty + th)) {
                const bool was_selected = (s_focus == S2Focus_Apps && s_sel == i);
                s_focus = S2Focus_Apps;
                s_sel = i;
                if (i < s_visible_count)
                    menu->curEntry = s_visible_indices[i];
                if (left_click && was_selected) {
                    switch2SfxPlay(S2Sfx_Confirm);
                    if (i == s_visible_count) {
                        s_view = S2View_Grid;
                        s_sel = s_visible_count > 0 ? s_visible_count - 1 : 0;
                    } else {
                        triggerEntryLaunch(s_visible[i]);
                    }
                }
                return;
            }
        }
    }

    if (s_view == S2View_Grid && y >= (int)GRID_CLIP_TOP && y <= (int)GRID_CLIP_BOTTOM) {
        const bool ps4g = s_cfg.ui_style == 1;
        const float gw = ps4g ? PS4_CARD_W : GRID_TILE_SZ;
        const float gh = ps4g ? PS4_CARD_H : GRID_TILE_SZ;
        for (int i = 0; i < s_visible_count; i++) {
            const int col = i % gridCols();
            const int row = i / gridCols();
            const float tx = ps4g ? PS4_LIB_X + (float)col * PS4_CARD_SX
                                   : GRID_START_X + (float)col * GRID_STRIDE_X;
            const float ty = ps4g ? PS4_LIB_Y + (float)row * PS4_CARD_SY - s_grid_scroll_y
                                  : GRID_START_Y + (float)row * GRID_STRIDE_Y - s_grid_scroll_y;
            if (x >= (int)tx && x <= (int)(tx + gw) &&
                y >= (int)ty && y <= (int)(ty + gh)) {
                const bool was_selected = (s_focus == S2Focus_Apps && s_sel == i);
                s_focus = S2Focus_Apps;
                s_sel = i;
                menu->curEntry = s_visible_indices[i];
                if (left_click && was_selected) {
                    triggerEntryLaunch(s_visible[i]);
                }
                return;
            }
        }
    }

    (void)moved;
}

void switch2HandleTouch(menu_s *menu) {
    HidTouchScreenState touch = {0};
    hidGetTouchScreenStates(&touch, 1);

    if (touch.count >= 1) {
        int tx = (int)touch.touches[0].x;
        int ty = (int)touch.touches[0].y;
        if (!s_touch_active) {
            s_touch_active = true;
            s_touch_is_tap = true;
            s_touch_start_x = s_touch_prev_x = tx;
            s_touch_start_y = s_touch_prev_y = ty;
            s_touch_vel_x = 0.0f;
        } else {
            int dx = tx - s_touch_prev_x;
            int dy = ty - s_touch_prev_y;
            if (abs(tx - s_touch_start_x) > 18 || abs(ty - s_touch_start_y) > 18)
                s_touch_is_tap = false;
            if (!s_touch_is_tap && !menuIsMsgBoxOpen() && s_view == S2View_Carousel) {
                s_scroll_x -= (float)dx;
                s_touch_vel_x = (float)dx;
                if (s_scroll_x < 0.0f) s_scroll_x = 0.0f;
            }
            (void)dy;
            s_touch_prev_x = tx;
            s_touch_prev_y = ty;
        }
    } else if (s_touch_active) {
        int x1 = s_touch_start_x;
        int y1 = s_touch_start_y;
        bool was_tap = s_touch_is_tap;
        s_touch_active = false;

        if (!was_tap || menuIsNetloaderActive())
            return;

        if (menuIsConfirmMsgBoxOpen()) {
            switch2SfxPlay(S2Sfx_Confirm);
            menuHandleAButton();
            return;
        }
        if (menuIsMsgBoxOpen()) {
            switch2SfxPlay(S2Sfx_Back);
            menuCloseMsgBox();
            return;
        }

        /* Settings view tap handling */
        if (s_view == S2View_Settings) {
            if (s_cfg.ui_style == 1) {
                if (y1 >= 120 && y1 <= 170 && x1 >= 290 && x1 <= 380) {
                    s_settings_tab = SETTINGS_TAB_GENERAL;
                    s_plugin_view = 0;
                    return;
                }
                if (y1 >= 120 && y1 <= 170 && x1 > 380 && x1 <= 530) {
                    s_settings_tab = SETTINGS_TAB_PLUGINS;
                    s_plugin_view = 0;
                    refreshPluginSettings();
                    return;
                }
                if (y1 >= 120 && y1 <= 170 && x1 > 530 && x1 <= 680) {
                    s_settings_tab = SETTINGS_TAB_THEMES;
                    s_theme_sel = 0;
                    scanThemes();
                    return;
                }
            } else if (x1 >= 56 && x1 <= 316 && y1 >= 126 && y1 <= 182) {
                s_settings_tab = SETTINGS_TAB_GENERAL;
                s_plugin_view = 0;
                return;
            } else if (x1 >= 56 && x1 <= 316 && y1 >= 196 && y1 <= 252) {
                s_settings_tab = SETTINGS_TAB_PLUGINS;
                s_plugin_view = 0;
                refreshPluginSettings();
                return;
            } else if (x1 >= 56 && x1 <= 316 && y1 >= 266 && y1 <= 322) {
                s_settings_tab = SETTINGS_TAB_THEMES;
                s_theme_sel = 0;
                scanThemes();
                return;
            }
            if (s_settings_tab == SETTINGS_TAB_GENERAL &&
                x1 >= (int)SETTINGS_LIST_X && x1 <= (int)(SETTINGS_LIST_X + SETTINGS_LIST_W)) {
                for (int i = 0; i < settingsRowCount(); i++) {
                    float ry = SETTINGS_LIST_Y + (float)i * SETTINGS_ROW_H;
                    if (y1 >= (int)ry && y1 <= (int)(ry + SETTINGS_ROW_H)) {
                        s_settings_sel = i;
                        changeSetting(settingsRowItem(i), +1);
                        return;
                    }
                }
            }
            if (s_settings_tab == SETTINGS_TAB_PLUGINS &&
                x1 >= (int)SETTINGS_LIST_X && x1 <= (int)(SETTINGS_LIST_X + SETTINGS_LIST_W)) {
                if (s_plugin_view == 0) {
                    for (int i = 0; i < s_plugin_settings.count; i++) {
                        float ry = SETTINGS_LIST_Y + (float)i * SETTINGS_ROW_H;
                        if (y1 >= (int)ry && y1 <= (int)(ry + SETTINGS_ROW_H)) {
                            s_plugin_sel = i;
                            s_plugin_view = 1;
                            s_plugin_option_sel = 0;
                            return;
                        }
                    }
                } else if (s_plugin_view == 2) {
                    const int start = s_mod_pack_sel >= 8 ? (s_mod_pack_sel - 7) : 0;
                    for (int i = start; i < s_mod_packs.count && (i - start) < 8; i++) {
                        float ry = SETTINGS_LIST_Y + (float)(i - start) * SETTINGS_ROW_H;
                        if (y1 >= (int)ry && y1 <= (int)(ry + SETTINGS_ROW_H)) {
                            s_mod_pack_sel = i;
                            hbModPackToggle(&s_mod_packs.packs[i]);
                            switch2SfxPlay(S2Sfx_Confirm);
                            return;
                        }
                    }
                } else if (s_plugin_sel < s_plugin_settings.count) {
                    int option_count = s_plugin_settings.plugins[s_plugin_sel].option_count;
                    for (int i = 0; i < option_count; i++) {
                        float ry = SETTINGS_LIST_Y + (float)i * SETTINGS_ROW_H;
                        if (y1 >= (int)ry && y1 <= (int)(ry + SETTINGS_ROW_H)) {
                            s_plugin_option_sel = i;
                            changePluginSetting(+1);
                            return;
                        }
                    }
                }
            }
            if (y1 >= 644 || (x1 < 344 && y1 < 92)) {
                switch2SfxPlay(S2Sfx_Back);
                s_view = s_prev_view;
                if (s_cfg.ui_style == 1) {
                    s_focus = s_prev_focus;
                    s_top_idx = s_prev_top_idx;
                    s_topbar_t = (s_prev_focus == S2Focus_Top) ? 1.0f : 0.0f;
                } else {
                    s_focus = S2Focus_Pill;
                }
            }
            return;
        }

        /* Message box tap */
        if (menuIsMsgBoxOpen()) {
            ThemeLayoutObject *sep = &themeCurrent.layoutObjects[ThemeLayoutId_MsgBoxSeparator];
            MessageBox mb = menuGetCurrentMsgBox();
            int sx = 1280 / 2 - mb.width / 2;
            int sy = (720 / 2 - mb.height / 2) + mb.height + sep->posStart[1];
            int ex = sx + mb.width;
            int ey = (720 / 2 - mb.height / 2) + mb.height;
            if (x1 > sx && x1 < ex && y1 > sy && y1 < ey) {
                if (menuIsConfirmMsgBoxOpen() && x1 < sx + (int)mb.width / 2)
                    menuConfirmMsgBox();
                else
                    menuCloseMsgBox();
            }
            return;
        }

        /* Bottom Pill Bar tap */
        if (y1 >= (int)(PILL_Y - 10) && y1 <= (int)(PILL_Y + PILL_H + 10) &&
            x1 >= (int)(PILL_X - 10) && x1 <= (int)(PILL_X + PILL_W + 10)) {
            for (int i = 0; i < PILL_COUNT; i++) {
                float cx = pillButtonCenterX(i);
                float dx = (float)x1 - cx;
                float dy = (float)y1 - PILL_CENTER_Y;
                if (dx * dx + dy * dy <= (PILL_BTN_RADIUS + 10.0f) * (PILL_BTN_RADIUS + 10.0f)) {
                    s_pill_idx = i;
                    s_focus = S2Focus_Pill;
                    triggerPillAction(i);
                    return;
                }
            }
        }

        /* Carousel / Grid tile tap */
        rebuildVisibleList(menu);
        if (s_view == S2View_Carousel) {
            int total_slots = s_visible_count + (hasEndAllSoftwareTile() ? 1 : 0);
            for (int i = 0; i < total_slots; i++) {
                float tx, ty, tw, th;
                homeTileRect(i, &tx, &ty, &tw, &th);
                if (x1 >= (int)tx && x1 <= (int)(tx + tw) &&
                    y1 >= (int)ty && y1 <= (int)(ty + th)) {
                    if (s_focus == S2Focus_Apps && s_sel == i) {
                        if (i == s_visible_count) {
                            s_view = S2View_Grid;
                            s_sel = s_visible_count - 1;
                        } else {
                            menu->curEntry = s_visible_indices[i];
                            triggerEntryLaunch(s_visible[i]);
                        }
                    } else {
                        s_focus = S2Focus_Apps;
                        s_sel = i;
                        if (i < s_visible_count)
                            menu->curEntry = s_visible_indices[i];
                    }
                    return;
                }
            }
        } else {
            if (y1 >= (int)GRID_CLIP_TOP && y1 <= (int)GRID_CLIP_BOTTOM) {
                const bool ps4g = s_cfg.ui_style == 1;
                const float gw = ps4g ? PS4_CARD_W : GRID_TILE_SZ;
                const float gh = ps4g ? PS4_CARD_H : GRID_TILE_SZ;
                for (int i = 0; i < s_visible_count; i++) {
                    int col = i % gridCols();
                    int row = i / gridCols();
                    float tx = ps4g ? PS4_LIB_X + (float)col * PS4_CARD_SX
                                    : GRID_START_X + (float)col * GRID_STRIDE_X;
                    float ty = ps4g ? PS4_LIB_Y + (float)row * PS4_CARD_SY - s_grid_scroll_y
                                    : GRID_START_Y + (float)row * GRID_STRIDE_Y - s_grid_scroll_y;
                    if (x1 >= (int)tx && x1 <= (int)(tx + gw) &&
                        y1 >= (int)ty && y1 <= (int)(ty + gh)) {
                        if (s_focus == S2Focus_Apps && s_sel == i) {
                            menu->curEntry = s_visible_indices[i];
                            triggerEntryLaunch(s_visible[i]);
                        } else {
                            s_focus = S2Focus_Apps;
                            s_sel = i;
                            menu->curEntry = s_visible_indices[i];
                        }
                        return;
                    }
                }
            }
        }
    }
}
