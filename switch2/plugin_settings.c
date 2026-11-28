/* SPDX-License-Identifier: ISC
 * kaiHEN plugin settings exposed by the Settings menu.
 */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <kaihen.h>

#include "plugin_settings.h"

static Result openHen(Service* hen) {
    Service ro;
    Result rc = kaihenGetRoSession(&ro);
    if (R_FAILED(rc))
        return rc;
    return kaihenOpenHomebrewSession(&ro, hen);
}

static void copyName(char* dst, size_t dst_size, const char* src) {
    if (!src || !src[0]) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

int hbPluginSettingsLoad(HbPluginSettingsList* out) {
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));

    Service hen;
    Result rc = openHen(&hen);
    if (R_FAILED(rc))
        return 0;
    out->hen_available = true;

    u32 count = 0;
    rc = henGetPluginCount(&hen, &count);
    if (R_FAILED(rc)) {
        serviceClose(&hen);
        return 0;
    }
    if (count > HB_PLUGIN_MAX)
        count = HB_PLUGIN_MAX;

    for (u32 i = 0; i < count; i++) {
        HenPluginName plugin_name;
        memset(&plugin_name, 0, sizeof(plugin_name));
        if (R_FAILED(henGetPluginName(&hen, i, &plugin_name)))
            continue;
        plugin_name.name[sizeof(plugin_name.name) - 1] = '\0';
        if (!plugin_name.name[0])
            continue;

        Service plugin;
        if (R_FAILED(henOpenPlugin(&hen, &plugin_name, &plugin)))
            continue;

        KaihenPluginSettingsInfo info;
        memset(&info, 0, sizeof(info));
        rc = kaihenPluginSettingsGetInfo(&plugin, &info);
        if (R_FAILED(rc) || info.magic != KAIHEN_PLUGIN_SETTINGS_MAGIC ||
            info.version > KAIHEN_PLUGIN_SETTINGS_VERSION || info.count == 0) {
            serviceClose(&plugin);
            continue;
        }

        HbPluginSettings* entry = &out->plugins[out->count];
        copyName(entry->name, sizeof(entry->name),
                 info.name[0] ? info.name : plugin_name.name);
        /* HEN opens plugins by the name the bootstrap registered (the
         * file name), not by the display name from GetInfo. */
        copyName(entry->reg_name, sizeof(entry->reg_name), plugin_name.name);
        u32 option_count = info.count;
        if (option_count > HB_PLUGIN_OPTION_MAX)
            option_count = HB_PLUGIN_OPTION_MAX;
        for (u32 option = 0; option < option_count; option++) {
            KaihenPluginSetting setting;
            memset(&setting, 0, sizeof(setting));
            if (R_FAILED(kaihenPluginSettingsGetSetting(&plugin, option, &setting)))
                continue;
            setting.key[sizeof(setting.key) - 1] = '\0';
            setting.label[sizeof(setting.label) - 1] = '\0';
            if (!setting.key[0] || !setting.label[0])
                continue;
            entry->options[entry->option_count++] = setting;
        }
        serviceClose(&plugin);

        if (strcmp(entry->reg_name, "nupvfs") == 0) {
            static const struct { const char* key; const char* label; } actions[] = {
                { HB_PLUGIN_KEY_COPY_SAVES,  "Copy NAND Saves -> SD" },
                { HB_PLUGIN_KEY_CLEAR_CACHE, "Clear RomFS Cache" },
                { HB_PLUGIN_KEY_MANAGE_MODS, "Manage Game Mods" },
            };
            for (size_t a = 0; a < sizeof(actions) / sizeof(actions[0]) &&
                               entry->option_count < HB_PLUGIN_OPTION_MAX; a++) {
                KaihenPluginSetting action;
                memset(&action, 0, sizeof(action));
                copyName(action.key, sizeof(action.key), actions[a].key);
                copyName(action.label, sizeof(action.label), actions[a].label);
                action.type = KaihenPluginSetting_Bool;
                entry->options[entry->option_count++] = action;
            }
        }

        if (entry->option_count > 0)
            out->count++;
        else
            memset(entry, 0, sizeof(*entry));
    }

    serviceClose(&hen);
    return out->count;
}

bool hbPluginSettingsSet(const HbPluginSettings* plugin, const char* key,
                         uint8_t value, char *err, size_t err_size) {
    if (err && err_size)
        err[0] = '\0';
    if (!plugin || !key || !key[0]) {
        if (err && err_size)
            snprintf(err, err_size, "bad input");
        return false;
    }

    Service hen;
    Result rc = openHen(&hen);
    if (R_FAILED(rc)) {
        if (err && err_size)
            snprintf(err, err_size, "open HEN: 0x%x", rc);
        return false;
    }

    HenPluginName plugin_name;
    memset(&plugin_name, 0, sizeof(plugin_name));
    copyName(plugin_name.name, sizeof(plugin_name.name), plugin->reg_name);

    Service service;
    bool applied = false;
    rc = henOpenPlugin(&hen, &plugin_name, &service);
    if (R_FAILED(rc)) {
        if (err && err_size)
            snprintf(err, err_size, "open plugin '%s': 0x%x", plugin->reg_name, rc);
        serviceClose(&hen);
        return false;
    }
    KaihenPluginSettingRequest request;
    memset(&request, 0, sizeof(request));
    copyName(request.key, sizeof(request.key), key);
    request.value = value;
    rc = kaihenPluginSettingsSetSetting(&service, &request);
    applied = R_SUCCEEDED(rc);
    if (!applied && err && err_size)
        snprintf(err, err_size, "set '%s'=%u: 0x%x", key, (unsigned)value, rc);
    serviceClose(&service);
    serviceClose(&hen);
    return applied;
}

static bool readCfgBoolFallback(const char* reg_name, const char* key, bool* out_val) {
    char cfg_path[128];
    snprintf(cfg_path, sizeof(cfg_path), "sdmc:/kaihen/%s.cfg", reg_name);
    FILE* f = fopen(cfg_path, "r");
    if (!f)
        return false;
    char line[256];
    const size_t klen = strlen(key);
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            *out_val = (line[klen + 1] != '0' && line[klen + 1] != '\0' &&
                        line[klen + 1] != '\n' && line[klen + 1] != '\r');
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

bool hbPluginGetBool(const char* reg_name, const char* key, bool default_val) {
    if (!reg_name || !reg_name[0] || !key || !key[0])
        return default_val;

    Service hen;
    if (R_SUCCEEDED(openHen(&hen))) {
        HenPluginName pname;
        memset(&pname, 0, sizeof(pname));
        copyName(pname.name, sizeof(pname.name), reg_name);
        Service plugin;
        if (R_SUCCEEDED(henOpenPlugin(&hen, &pname, &plugin))) {
            KaihenPluginSettingsInfo info;
            memset(&info, 0, sizeof(info));
            if (R_SUCCEEDED(kaihenPluginSettingsGetInfo(&plugin, &info)) &&
                info.magic == KAIHEN_PLUGIN_SETTINGS_MAGIC &&
                info.version <= KAIHEN_PLUGIN_SETTINGS_VERSION) {
                u32 count = info.count > HB_PLUGIN_OPTION_MAX ? HB_PLUGIN_OPTION_MAX : info.count;
                for (u32 i = 0; i < count; i++) {
                    KaihenPluginSetting setting;
                    memset(&setting, 0, sizeof(setting));
                    if (R_SUCCEEDED(kaihenPluginSettingsGetSetting(&plugin, i, &setting))) {
                        setting.key[sizeof(setting.key) - 1] = '\0';
                        if (strcmp(setting.key, key) == 0) {
                            serviceClose(&plugin);
                            serviceClose(&hen);
                            return setting.value != 0;
                        }
                    }
                }
            }
            serviceClose(&plugin);
        }
        serviceClose(&hen);
    }

    bool cfg_val = default_val;
    if (readCfgBoolFallback(reg_name, key, &cfg_val))
        return cfg_val;
    return default_val;
}

static void mkdirRecursiveSd(const char* path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';
    for (char* p = tmp + 6; *p; p++) { // skip "sdmc:/"
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static bool readNpdmProgramId(const char* npdm_path, u64* out_tid) {
    FILE* f = fopen(npdm_path, "rb");
    if (!f) return false;
    u8 hdr[0x80];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fclose(f);
        return false;
    }
    if (memcmp(hdr, "META", 4) != 0) {
        fclose(f);
        return false;
    }
    u32 aci0_off = 0, aci0_sz = 0;
    memcpy(&aci0_off, hdr + 0x70, 4);
    memcpy(&aci0_sz, hdr + 0x74, 4);
    if (aci0_sz < 0x18 || fseek(f, (long)aci0_off, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    u8 aci0[0x18];
    if (fread(aci0, 1, sizeof(aci0), f) != sizeof(aci0) || memcmp(aci0, "ACI0", 4) != 0) {
        fclose(f);
        return false;
    }
    u64 tid = 0;
    memcpy(&tid, aci0 + 0x10, 8);
    fclose(f);
    if (!tid) return false;
    *out_tid = tid;
    return true;
}

static void addUniqueTid(u64* tids, size_t* count, size_t max_tids, u64 tid) {
    if (!tid || *count >= max_tids) return;
    for (size_t i = 0; i < *count; i++) {
        if (tids[i] == tid) return;
    }
    tids[(*count)++] = tid;
}

static size_t scanBackupTids(u64* tids, size_t max_tids) {
    static const char* const roots[] = {
        "sdmc:/backups",
        "sdmc:/games",
        "sdmc:/switch/games",
        "sdmc:/nup",
        "sdmc:/roms",
    };
    static const char* const npdm_rel[] = {
        "exefs/main.npdm",
        "game/exefs/main.npdm",
        "program1/exefs/main.npdm",
        "game/program1/exefs/main.npdm",
    };
    size_t count = 0;
    for (size_t r = 0; r < sizeof(roots) / sizeof(roots[0]); r++) {
        DIR* d = opendir(roots[r]);
        if (!d) continue;
        struct dirent* ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            for (size_t p = 0; p < sizeof(npdm_rel) / sizeof(npdm_rel[0]); p++) {
                char path[512];
                snprintf(path, sizeof(path), "%s/%s/%s", roots[r], ent->d_name, npdm_rel[p]);
                u64 tid = 0;
                if (readNpdmProgramId(path, &tid))
                    addUniqueTid(tids, &count, max_tids, tid);
            }
        }
        closedir(d);
    }
    return count;
}

typedef struct {
    u8* buf;
    size_t buf_sz;
    u32 files_copied;
    u32 errors;
    u64 bytes_copied;
} SaveCopyStats;

static void copyFsDirToSd(FsFileSystem* src_fs, const char* src_dir,
                          const char* dst_dir, SaveCopyStats* st, int depth) {
    if (depth > 16) return;
    mkdirRecursiveSd(dst_dir);

    FsDir dir;
    if (R_FAILED(fsFsOpenDirectory(src_fs, src_dir,
                                   FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
        st->errors++;
        return;
    }

    FsDirectoryEntry entries[32];
    s64 read_cnt = 0;
    while (R_SUCCEEDED(fsDirRead(&dir, &read_cnt, 32, entries)) && read_cnt > 0) {
        for (s64 i = 0; i < read_cnt; i++) {
            const char* name = entries[i].name;
            if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            char child_src[FS_MAX_PATH];
            char child_dst[512];
            if (strcmp(src_dir, "/") == 0)
                snprintf(child_src, sizeof(child_src), "/%s", name);
            else
                snprintf(child_src, sizeof(child_src), "%s/%s", src_dir, name);
            snprintf(child_dst, sizeof(child_dst), "%s/%s", dst_dir, name);

            if (entries[i].type == FsDirEntryType_Dir) {
                copyFsDirToSd(src_fs, child_src, child_dst, st, depth + 1);
            } else if (entries[i].type == FsDirEntryType_File) {
                FsFile f;
                if (R_FAILED(fsFsOpenFile(src_fs, child_src, FsOpenMode_Read, &f))) {
                    st->errors++;
                    continue;
                }
                s64 fsz = 0;
                if (R_FAILED(fsFileGetSize(&f, &fsz)) || fsz < 0) {
                    fsFileClose(&f);
                    st->errors++;
                    continue;
                }
                FILE* out = fopen(child_dst, "wb");
                if (!out) {
                    fsFileClose(&f);
                    st->errors++;
                    continue;
                }
                s64 off = 0;
                bool ok = true;
                while (off < fsz) {
                    u64 chunk = (u64)(fsz - off);
                    if (chunk > st->buf_sz) chunk = st->buf_sz;
                    u64 got = 0;
                    if (R_FAILED(fsFileRead(&f, off, st->buf, chunk, FsReadOption_None, &got)) ||
                        got == 0 || fwrite(st->buf, 1, (size_t)got, out) != (size_t)got) {
                        ok = false;
                        break;
                    }
                    off += (s64)got;
                    st->bytes_copied += got;
                }
                fclose(out);
                fsFileClose(&f);
                if (ok) st->files_copied++;
                else st->errors++;
            }
        }
    }
    fsDirClose(&dir);
}

bool hbCopyNandSavesToSd(char* summary, size_t summary_size) {
    u64 backup_tids[128];
    const size_t backup_count = scanBackupTids(backup_tids, 128);
    if (backup_count == 0) {
        snprintf(summary, summary_size,
                 "Copy NAND Saves -> SD\nNo game backups found in /backups.");
        return false;
    }

    AccountUid active_uid = {0};
    bool has_active_uid = false;
    if (R_SUCCEEDED(accountInitialize(AccountServiceType_Application))) {
        if (R_SUCCEEDED(accountGetPreselectedUser(&active_uid)) && accountUidIsValid(&active_uid))
            has_active_uid = true;
        accountExit();
    }

    FsSaveDataInfoReader reader;
    Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
    if (R_FAILED(rc)) {
        snprintf(summary, summary_size,
                 "Copy NAND Saves -> SD failed\nfsOpenSaveDataInfoReader: 0x%x", rc);
        return false;
    }

    SaveCopyStats st;
    memset(&st, 0, sizeof(st));
    st.buf_sz = 128 * 1024;
    st.buf = (u8*)malloc(st.buf_sz);
    if (!st.buf) {
        fsSaveDataInfoReaderClose(&reader);
        snprintf(summary, summary_size, "Copy NAND Saves -> SD failed\nOut of memory");
        return false;
    }

    u32 titles_copied = 0;
    FsSaveDataInfo infos[64];
    s64 total = 0;
    while (R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, infos, 64, &total)) && total > 0) {
        for (s64 i = 0; i < total; i++) {
            const FsSaveDataInfo* info = &infos[i];
            if (!info->application_id) continue;
            if (info->save_data_type != FsSaveDataType_Account &&
                info->save_data_type != FsSaveDataType_Device)
                continue;
            if (info->save_data_type == FsSaveDataType_Account && has_active_uid &&
                memcmp(&info->uid, &active_uid, sizeof(AccountUid)) != 0)
                continue;

            const u64 info_base = info->application_id & 0x00fffffffffff000ULL;
            u64 matched_tids[4];
            size_t matched_cnt = 0;
            for (size_t b = 0; b < backup_count && matched_cnt < 4; b++) {
                if (backup_tids[b] == info->application_id ||
                    (backup_tids[b] & 0x00fffffffffff000ULL) == info_base) {
                    matched_tids[matched_cnt++] = backup_tids[b];
                }
            }
            if (matched_cnt == 0) continue;

            FsSaveDataAttribute attr;
            memset(&attr, 0, sizeof(attr));
            attr.application_id = info->application_id;
            attr.uid = info->uid;
            attr.save_data_type = info->save_data_type;
            attr.save_data_rank = info->save_data_rank;
            attr.save_data_index = info->save_data_index;

            FsFileSystem save_fs;
            // Strictly read-only NAND open (FS cmd 53); never writes or commits to NAND.
            if (R_FAILED(fsOpenReadOnlySaveDataFileSystem(&save_fs, FsSaveDataSpaceId_User, &attr))) {
                st.errors++;
                continue;
            }

            const char* sub = (info->save_data_type == FsSaveDataType_Device) ? "device" : "user";
            for (size_t m = 0; m < matched_cnt; m++) {
                char dst[256];
                snprintf(dst, sizeof(dst), "sdmc:/kaihen/saves/%016llx/%s",
                         (unsigned long long)matched_tids[m], sub);
                copyFsDirToSd(&save_fs, "/", dst, &st, 0);
                titles_copied++;
            }
            fsFsClose(&save_fs);
        }
    }

    fsSaveDataInfoReaderClose(&reader);
    free(st.buf);

    snprintf(summary, summary_size,
             "NAND Saves -> SD (/kaihen/saves/)\n"
             "Copied: %u target(s), %u file(s), %.2f MiB\n"
             "Backups scanned: %zu  Errors: %u",
             titles_copied, st.files_copied,
             (double)st.bytes_copied / (1024.0 * 1024.0),
             backup_count, st.errors);
    return true;
}

bool hbClearRomfsCache(char* summary, size_t summary_size) {
    Service hen;
    Result rc = openHen(&hen);
    if (R_FAILED(rc)) {
        if (summary && summary_size)
            snprintf(summary, summary_size, "Clear RomFS Cache failed\nopen HEN: 0x%x", rc);
        return false;
    }
    HenPluginName pname;
    memset(&pname, 0, sizeof(pname));
    copyName(pname.name, sizeof(pname.name), "nupvfs");
    Service plugin;
    rc = henOpenPlugin(&hen, &pname, &plugin);
    if (R_FAILED(rc)) {
        serviceClose(&hen);
        if (summary && summary_size)
            snprintf(summary, summary_size, "Clear RomFS Cache failed\nopen nupvfs: 0x%x", rc);
        return false;
    }
    // NupVfsCmd_ClearCache = 4
    rc = serviceDispatch(&plugin, 4);
    serviceClose(&plugin);
    serviceClose(&hen);
    if (R_FAILED(rc)) {
        if (summary && summary_size)
            snprintf(summary, summary_size, "Clear RomFS Cache failed\ncmd 4: 0x%x", rc);
        return false;
    }
    if (summary && summary_size)
        snprintf(summary, summary_size, "Cleared nupvfs RomFS cache.\nNext launch rescans all mods.");
    return true;
}

static bool fileExistsSimple(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int hbModPacksLoad(HbModPackList* out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    DIR* md = opendir("sdmc:/kaihen/mods");
    if (!md) return 0;
    struct dirent* tid_ent;
    while ((tid_ent = readdir(md)) != NULL && out->count < HB_MOD_PACK_MAX) {
        if (tid_ent->d_name[0] == '.') continue;
        char* endp = NULL;
        u64 tid = strtoull(tid_ent->d_name, &endp, 16);
        if (!tid || !endp || *endp != '\0') continue;

        char packs_dir[320];
        snprintf(packs_dir, sizeof(packs_dir), "sdmc:/kaihen/mods/%s/packs", tid_ent->d_name);
        DIR* pd = opendir(packs_dir);
        if (!pd) continue;
        struct dirent* pent;
        while ((pent = readdir(pd)) != NULL && out->count < HB_MOD_PACK_MAX) {
            const char* nm = pent->d_name;
            if (!nm[0] || nm[0] == '.' || nm[0] == '_') continue;
            char full[256];
            snprintf(full, sizeof(full), "%s/%s", packs_dir, nm);
            struct stat st;
            if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

            char dis[300];
            snprintf(dis, sizeof(dis), "%s/.disabled", full);
            HbModPackEntry* e = &out->packs[out->count++];
            e->tid = tid;
            snprintf(e->pack_dir, sizeof(e->pack_dir), "%s", full);
            snprintf(e->label, sizeof(e->label), "[%04llx] %s",
                     (unsigned long long)(tid & 0xffffULL), nm);
            e->enabled = !fileExistsSimple(dis);
        }
        closedir(pd);
    }
    closedir(md);
    return out->count;
}

bool hbModPackToggle(HbModPackEntry* pack) {
    if (!pack || !pack->pack_dir[0]) return false;
    char dis[300];
    snprintf(dis, sizeof(dis), "%s/.disabled", pack->pack_dir);
    if (pack->enabled) {
        FILE* f = fopen(dis, "wb");
        if (!f) return false;
        fclose(f);
        pack->enabled = false;
    } else {
        remove(dis);
        pack->enabled = true;
    }
    hbClearRomfsCache(NULL, 0);
    return true;
}
