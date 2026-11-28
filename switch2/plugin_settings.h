/* SPDX-License-Identifier: ISC
 * kaiHEN plugin settings exposed by the Settings menu.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <kaihen/plugin.h>
#include <kaihen/plugin_settings.h>

#define HB_PLUGIN_MAX 16
#define HB_PLUGIN_OPTION_MAX 9
#define HB_PLUGIN_KEY_COPY_SAVES  "__copy_nand_saves"
#define HB_PLUGIN_KEY_CLEAR_CACHE "__clear_romfs_cache"
#define HB_PLUGIN_KEY_MANAGE_MODS "__manage_game_mods"
#define HB_MOD_PACK_MAX 64

typedef struct {
    u64 tid;
    char label[128];
    char pack_dir[256];
    bool enabled;
} HbModPackEntry;

typedef struct {
    HbModPackEntry packs[HB_MOD_PACK_MAX];
    int count;
} HbModPackList;

typedef struct {
    char name[HEN_PLUGIN_NAME_SIZE];      /* display name (GetInfo, may be a label) */
    char reg_name[HEN_PLUGIN_NAME_SIZE];  /* name HEN registered the plugin under */
    KaihenPluginSetting options[HB_PLUGIN_OPTION_MAX];
    int option_count;
} HbPluginSettings;

typedef struct {
    HbPluginSettings plugins[HB_PLUGIN_MAX];
    int count;
    bool hen_available;
} HbPluginSettingsList;

/// Reads every published plugin's settings table through HEN. Returns the
/// number of plugins that exposed at least one option.
int hbPluginSettingsLoad(HbPluginSettingsList* out);

/// Applies one value through the plugin's service. Returns true on success.
/// On failure, writes the failing stage and Result into err (if non-NULL).
bool hbPluginSettingsSet(const HbPluginSettings* plugin, const char* key,
                         uint8_t value, char *err, size_t err_size);

/// Queries a single Bool option from a registered plugin (e.g. "nupvfs", "games").
/// Falls back to /kaihen/<reg_name>.cfg if HEN is unavailable, else default_val.
bool hbPluginGetBool(const char* reg_name, const char* key, bool default_val);

/// Read-only copies matching console NAND saves to sdmc:/kaihen/saves/<tid>/.
/// Never writes or commits to NAND. Returns true if enumeration succeeded.
bool hbCopyNandSavesToSd(char* summary, size_t summary_size);

/// Clears all cached RomFS metadata slots in nupvfs.nso (NupVfsCmd_ClearCache).
bool hbClearRomfsCache(char* summary, size_t summary_size);

/// Scans sdmc:/kaihen/mods/<tid>/packs/<name> for toggleable mod packs.
int hbModPacksLoad(HbModPackList* out);

/// Toggles a mod pack by creating/removing <pack_dir>/.disabled and clears nupvfs cache.
bool hbModPackToggle(HbModPackEntry* pack);
