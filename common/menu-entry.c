#include "common.h"
#include <physfs.h>

void menuEntryInit(menuEntry_s* me, MenuEntryType type) {
    memset(me, 0, sizeof(*me));
    me->type = type;
}

void menuEntryFree(menuEntry_s* me, bool skip_icongfx) {
    me->icon_size = 0;
    if (me->icon) {
        free(me->icon);
        me->icon = NULL;
    }

    if (!skip_icongfx) {
        if (me->icon_gfx) {
            FreeImageBuffer(me->icon_gfx);
            me->icon_gfx = NULL;
        }

        if (me->icon_gfx_small) {
            FreeImageBuffer(me->icon_gfx_small);
            me->icon_gfx_small = NULL;
        }

        me->icon_mode = IMAGE_MODE_RGB24;
    }

    if (me->nacp) {
        free(me->nacp);
        me->nacp = NULL;
    }
}

bool fileExists(const char* path) {
    struct stat st;
    return stat(path, &st)==0 && S_ISREG(st.st_mode);
}

bool fsobjExists(const char* path) {
    struct stat st;
    return stat(path, &st)==0;
}

static bool menuEntryLoadEmbeddedIcon(menuEntry_s* me) {
    NroHeader header;
    NroAssetHeader asset_header;

    FILE* f = fopen(me->path, "rb");
    if (!f) return false;

    fseek(f, sizeof(NroStart), SEEK_SET);
    if (fread(&header, sizeof(header), 1, f) != 1)
    {
        fclose(f);
        return false;
    }

    fseek(f, header.size, SEEK_SET);

    if (fread(&asset_header, sizeof(asset_header), 1, f) != 1
        || asset_header.magic != NROASSETHEADER_MAGIC
        || asset_header.version > NROASSETHEADER_VERSION
        || asset_header.icon.offset == 0
        || asset_header.icon.size == 0)
    {
        fclose(f);
        return false;
    }

    me->icon_size = asset_header.icon.size;
    me->icon = (uint8_t*)malloc(me->icon_size);
    if (me->icon == NULL) {
        fclose(f);
        return false;
    }
    memset(me->icon, 0, me->icon_size);

    fseek(f, header.size + asset_header.icon.offset, SEEK_SET);
    bool ok = fread(me->icon, me->icon_size, 1, f) == 1;
    fclose(f);
    return ok;
}

static bool menuEntryLoadExternalIcon(menuEntry_s* me, const char* path, bool data_source) {
    struct stat st;

    if (data_source) {
        return assetsPhysfsReadFile(path, &me->icon, &me->icon_size, false);
    }

    if(stat(path, &st)==-1) return false;

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    me->icon_size = st.st_size;
    me->icon = (uint8_t*)malloc(me->icon_size);
    if (me->icon == NULL) {
        fclose(f);
        return false;
    }
    memset(me->icon, 0, me->icon_size);

    bool ok = fread(me->icon, me->icon_size, 1, f) == 1;
    fclose(f);
    return ok;
}

static bool menuEntryImportIconGfx(menuEntry_s* me, uint8_t* icon_gfx, uint8_t* icon_gfx_small, ImageMode mode) {
    size_t tmpsize;
    size_t bytes_per_pixel = mode == IMAGE_MODE_RGBA32 ? 4 : 3;

    if (icon_gfx == NULL || icon_gfx_small == NULL) return false;

    ThemeLayoutObject *layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuActiveEntryIcon];
    ThemeLayoutObject *layoutobj2 = &themeCurrent.layoutObjects[ThemeLayoutId_MenuListIcon];

    tmpsize = layoutobj->size[0]*layoutobj->size[1]*bytes_per_pixel;
    me->icon_gfx = (uint8_t*)malloc(tmpsize);
    if (me->icon_gfx) memcpy(me->icon_gfx, icon_gfx, tmpsize);

    if (me->icon_gfx) {
        tmpsize = layoutobj2->size[0]*layoutobj2->size[1]*bytes_per_pixel;
        me->icon_gfx_small = (uint8_t*)malloc(tmpsize);
        if (me->icon_gfx_small) memcpy(me->icon_gfx_small, icon_gfx_small, tmpsize);

        if (me->icon_gfx_small == NULL) {
            free(me->icon_gfx);
            me->icon_gfx = NULL;
        }
    }

    if (me->icon_gfx && me->icon_gfx_small) me->icon_mode = mode;

    return me->icon_gfx && me->icon_gfx_small;
}

static bool menuEntryLoadEmbeddedNacp(menuEntry_s* me) {
    NroHeader header;
    NroAssetHeader asset_header;

    FILE* f = fopen(me->path, "rb");
    if (!f) return false;

    fseek(f, sizeof(NroStart), SEEK_SET);
    if (fread(&header, sizeof(header), 1, f) != 1)
    {
        fclose(f);
        return false;
    }

    fseek(f, header.size, SEEK_SET);

    if (fread(&asset_header, sizeof(asset_header), 1, f) != 1
        || asset_header.magic != NROASSETHEADER_MAGIC
        || asset_header.version > NROASSETHEADER_VERSION
        || asset_header.nacp.offset == 0
        || asset_header.nacp.size == 0)
    {
        fclose(f);
        return false;
    }

    if (asset_header.nacp.size < sizeof(NacpStruct))
    {
        fclose(f);
        return false;
    }

    me->nacp = (NacpStruct*)malloc(sizeof(NacpStruct));
    if (me->nacp == NULL) {
        fclose(f);
        return false;
    }

    fseek(f, header.size + asset_header.nacp.offset, SEEK_SET);
    bool ok = fread(me->nacp, sizeof(NacpStruct), 1, f) == 1;

    fclose(f);
    return ok;
}

static bool menuEntryLoadExternalNacp(menuEntry_s* me, const char* path) {
    struct stat st;

    if(stat(path, &st)==-1) return false;

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    me->nacp = (NacpStruct*)malloc(sizeof(NacpStruct));
    if (me->nacp == NULL) {
        fclose(f);
        return false;
    }
    memset(me->nacp, 0, sizeof(NacpStruct));

    bool ok = fread(me->nacp, sizeof(NacpStruct), 1, f) == 1;
    fclose(f);
    return ok;
}

/*static void fixSpaceNewLine(char* buf) {
    char *outp = buf, *inp = buf;
    char lastc = 0;
    do
    {
        char c = *inp++;
        if (c == ' ' && lastc == ' ')
            outp[-1] = '\n';
        else
            *outp++ = c;
        lastc = c;
    } while (lastc);
}*/

static bool menuEntryFindDirIcon(const char* dir_path, char* out, size_t out_sz) {
    static const char* const subdirs[] = {
        "",
        "/control",
        "/control/romfs",
        "/game",
        "/game/control",
        "/game/control/romfs",
        "/meta",
    };
    static const char* const names[] = {
        "icon_AmericanEnglish.dat",
        "icon_BritishEnglish.dat",
        "icon_Japanese.dat",
        "icon.jpg",
        "icon.png",
    };
    for (size_t s = 0; s < sizeof(subdirs) / sizeof(subdirs[0]); s++) {
        for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
            snprintf(out, out_sz, "%s%s/%s", dir_path, subdirs[s], names[n]);
            if (fileExists(out))
                return true;
        }
    }
    for (size_t s = 0; s < sizeof(subdirs) / sizeof(subdirs[0]); s++) {
        char scan_dir[PATH_MAX + 32];
        snprintf(scan_dir, sizeof(scan_dir), "%s%s", dir_path, subdirs[s]);
        DIR* d = opendir(scan_dir);
        if (!d)
            continue;
        struct dirent* de;
        bool found = false;
        while ((de = readdir(d)) != NULL) {
            if (strncmp(de->d_name, "icon_", 5) == 0) {
                snprintf(out, out_sz, "%s/%s", scan_dir, de->d_name);
                if (fileExists(out)) {
                    found = true;
                    break;
                }
            }
        }
        closedir(d);
        if (found)
            return true;
    }
    // Also check one level of non-exefs/romfs child directories (e.g. <game>/control_<tid>/)
    DIR* root_d = opendir(dir_path);
    if (root_d) {
        struct dirent* de;
        while ((de = readdir(root_d)) != NULL) {
            if (de->d_name[0] == '.' ||
                strcmp(de->d_name, "exefs") == 0 ||
                strcmp(de->d_name, "romfs") == 0 ||
                strcmp(de->d_name, "romfs_update") == 0 ||
                strcmp(de->d_name, "dlc") == 0 ||
                strcmp(de->d_name, "aoc") == 0)
                continue;
            char sub[PATH_MAX + 32];
            snprintf(sub, sizeof(sub), "%s/%s", dir_path, de->d_name);
            struct stat st;
            if (stat(sub, &st) != 0 || !S_ISDIR(st.st_mode))
                continue;
            for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
                snprintf(out, out_sz, "%s/%s", sub, names[n]);
                if (fileExists(out)) {
                    closedir(root_d);
                    return true;
                }
            }
            DIR* sub_d = opendir(sub);
            if (sub_d) {
                struct dirent* sde;
                while ((sde = readdir(sub_d)) != NULL) {
                    if (strncmp(sde->d_name, "icon_", 5) == 0) {
                        snprintf(out, out_sz, "%s/%s", sub, sde->d_name);
                        if (fileExists(out)) {
                            closedir(sub_d);
                            closedir(root_d);
                            return true;
                        }
                    }
                }
                closedir(sub_d);
            }
        }
        closedir(root_d);
    }
    return false;
}

static bool menuEntryFindDirNacp(const char* dir_path, char* out, size_t out_sz) {
    static const char* const fixed[] = {
        "control.nacp",
        "control/control.nacp",
        "control/romfs/control.nacp",
        "game/control.nacp",
        "game/control/control.nacp",
        "game/control/romfs/control.nacp",
        "meta/control.nacp",
    };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
        snprintf(out, out_sz, "%s/%s", dir_path, fixed[i]);
        if (fileExists(out))
            return true;
    }
    return false;
}

static uint8_t detectGamePlatformFromDir(const char* game_dir) {
    static const char* const rels[] = { "exefs/main.npdm", "game/exefs/main.npdm" };
    for (size_t i = 0; i < sizeof(rels) / sizeof(rels[0]); i++) {
        char path[PATH_MAX + 32];
        snprintf(path, sizeof(path), "%s/%s", game_dir, rels[i]);
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        uint8_t hdr[0x80];
        if (fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr) && memcmp(hdr, "META", 4) == 0) {
            uint32_t aci0_off = 0, aci0_sz = 0;
            memcpy(&aci0_off, hdr + 0x70, 4);
            memcpy(&aci0_sz, hdr + 0x74, 4);
            if (aci0_sz >= 0x18 && fseek(f, (long)aci0_off, SEEK_SET) == 0) {
                uint8_t aci0[0x18];
                if (fread(aci0, 1, sizeof(aci0), f) == sizeof(aci0) && memcmp(aci0, "ACI0", 4) == 0) {
                    uint64_t tid = 0;
                    memcpy(&tid, aci0 + 0x10, 8);
                    fclose(f);
                    return ((tid >> 56) == 0x04) ? 2 : 1;
                }
            }
        }
        fclose(f);
    }
    return 1;
}

bool menuEntryLoadGameShortcut(menuEntry_s* me, const char* name, const char* loader_nro,
                               const char* game_dir, const char* icon_override, bool trace_mode) {
    if (!me || !loader_nro || !game_dir || !fileExists(loader_nro))
        return false;

    const char* suffix = NULL;
    const char* explicit_name = name;
    if (name && name[0] == '+') {
        suffix = name + 1;
        explicit_name = NULL;
    }

    const char* fallback_base = strrchr(game_dir, '/');
    fallback_base = (fallback_base && fallback_base[1]) ? (fallback_base + 1) : game_dir;

    snprintf(me->path, sizeof(me->path), "%s", loader_nro);
    me->game_platform = detectGamePlatformFromDir(game_dir);
    strncpy(me->name, (explicit_name && explicit_name[0]) ? explicit_name : fallback_base, sizeof(me->name) - 1);
    strncpy(me->author, textGetString(StrId_DefaultPublisher), sizeof(me->author) - 1);
    strncpy(me->version, "1.0.0", sizeof(me->version) - 1);

    // Search game_dir, parent (e.g. game/), and grandparent (<App>/) for control.nacp & icon
    char bases[3][PATH_MAX + 8];
    int base_count = 0;
    snprintf(bases[base_count++], sizeof(bases[0]), "%s", game_dir);
    for (int up = 0; up < 2; up++) {
        char tmp[PATH_MAX + 8];
        snprintf(tmp, sizeof(tmp), "%s", bases[base_count - 1]);
        size_t len = strlen(tmp);
        while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';
        char* slash = strrchr(tmp, '/');
        if (!slash || slash <= tmp + 5)
            break;
        *slash = '\0';
        snprintf(bases[base_count++], sizeof(bases[0]), "%s", tmp);
    }

    bool iconLoaded = false;
    char probe[PATH_MAX + 64];
    if (icon_override && icon_override[0] && fileExists(icon_override))
        iconLoaded = menuEntryLoadExternalIcon(me, icon_override, false);
    for (int i = 0; i < base_count && !iconLoaded; i++) {
        if (menuEntryFindDirIcon(bases[i], probe, sizeof(probe)))
            iconLoaded = menuEntryLoadExternalIcon(me, probe, false);
    }
    if (!iconLoaded)
        iconLoaded = menuEntryLoadEmbeddedIcon(me);
    if (iconLoaded)
        menuEntryParseIcon(me);

    launchMenuEntryLoadABIRevision(me);

    bool nacpLoaded = false;
    for (int i = 0; i < base_count && !nacpLoaded; i++) {
        if (menuEntryFindDirNacp(bases[i], probe, sizeof(probe)))
            nacpLoaded = menuEntryLoadExternalNacp(me, probe);
    }
    if (nacpLoaded)
        menuEntryParseNacp(me);
    // Keep explicit shortcut display name if provided; otherwise append optional subprogram suffix
    if (explicit_name && explicit_name[0]) {
        memset(me->name, 0, sizeof(me->name));
        strncpy(me->name, explicit_name, sizeof(me->name) - 1);
    } else if (suffix && suffix[0]) {
        const size_t cur = strlen(me->name);
        const size_t slen = strlen(suffix);
        if (cur + slen < sizeof(me->name))
            memcpy(me->name + cur, suffix, slen + 1);
    }

    argData_s* ad = &me->args;
    ad->dst = (char*)&ad->buf[1];
    launchAddArg(ad, me->path);
    launchAddArg(ad, game_dir);
    if (trace_mode)
        launchAddArg(ad, "--trace");
    return true;
}

bool menuEntryLoad(menuEntry_s* me, const char* name, bool shortcut, bool check_exists) {
    int i=0, tmplen;
    menu_s *menu_fileassoc = menuFileassocGetCurrent();
    menuEntry_s* fileassoc_me = NULL;
    char *strptr = NULL;
    char tempbuf[PATH_MAX+16];
    //bool isOldAppFolder = false;

    if (check_exists && !fsobjExists(me->path)) return false;

    tempbuf[PATH_MAX] = 0;
    strncpy(me->name, name, sizeof(me->name)-1);

    if (me->type == ENTRY_TYPE_FOLDER)
    {
        //Check for <dirpath>/<dirname>.nro
        snprintf(tempbuf, sizeof(tempbuf)-1, "%.*s/%.*s.nro", (int)sizeof(tempbuf)/2, me->path, (int)sizeof(tempbuf)/2-7, name);
        bool found = fileExists(tempbuf);
        bool fileassoc_flag = 0;

        //Use the first .nro found in the directory, if there's only 1 NRO in the directory. Only used for paths starting with "sdmc:/switch/".
        tmplen = strlen(menuGetRootPath());
        if (!found && strncmp(me->path, menuGetRootPath(), tmplen)==0 && me->path[tmplen]=='/') {
            DIR* dir;
            struct dirent* dp;
            u32 nro_count=0;

            dir = opendir(me->path);
            if (dir) {
                while ((dp = readdir(dir))) {
                    if (dp->d_name[0]=='.')//Check this here so that it's consistent with menuScan().
                        continue;

                    const char* ext = getExtension(dp->d_name);
                    if (strcasecmp(ext, ".nro")==0) {
                        nro_count++;
                        if (nro_count>1) {
                            found = 0;
                            break;
                        }

                        snprintf(tempbuf, sizeof(tempbuf)-1, "%.*s/%.*s", (int)sizeof(tempbuf)/2, me->path, (int)sizeof(tempbuf)/2-7, dp->d_name);
                        found = fileExists(tempbuf);
                    }
                }
                closedir(dir);
            }

            if (!found && menu_fileassoc->nEntries > 0) {
                fileassoc_flag = 1;
                dir = opendir(me->path);
                if (dir) {
                    while ((dp = readdir(dir))) {
                        if (dp->d_name[0]=='.')//Check this here so that it's consistent with menuScan().
                            continue;

                        for (fileassoc_me = menu_fileassoc->firstEntry, i = 0; fileassoc_me; fileassoc_me = fileassoc_me->next, i++) {
                            if (!fileassoc_me->fileassoc_type) continue; //Only handle fileassoc entries for filenames, not file_extensions.

                            if (strcmp(dp->d_name, fileassoc_me->fileassoc_str)) continue;

                            snprintf(tempbuf, sizeof(tempbuf)-1, "%.*s/%.*s", (int)sizeof(tempbuf)/2, me->path, (int)sizeof(tempbuf)/2-7, dp->d_name);
                            found = fileExists(tempbuf);
                            if (found) break;
                        }
                        if (found) break;
                    }
                   closedir(dir);
                }
            }
        }

        if (found)
        {
            //isOldAppFolder = true;
            shortcut = false;
            me->type = fileassoc_flag ? ENTRY_TYPE_FILE_OTHER : ENTRY_TYPE_FILE;
            strcpy(me->path, tempbuf);
        }
    }

    if (me->type == ENTRY_TYPE_FILE)
    {
        strncpy(me->author, textGetString(StrId_DefaultPublisher), sizeof(me->author)-1);
        strncpy(me->version, "1.0.0", sizeof(me->version)-1);

        //shortcut_s sc;

        /*if (shortcut)
        {
            if (R_FAILED(shortcutCreate(&sc, me->path)))
                return false;
            if (!fileExists(sc.executable))
            {
                shortcutFree(&sc);
                return false;
            }
            strcpy(me->path, "sdmc:");
            strcat(me->path, sc.executable);
        }*/

        bool iconLoaded = false;

        // Load the icon
        /*if (shortcut)
        {
            FILE* f = sc.icon ? fopen(sc.icon, "rb") : NULL;
            if (f)
            {
                iconLoaded = fread(&me->smdh, sizeof(smdh_s), 1, f) == 1;
                fclose(f);
            }
        }*/

        if (!iconLoaded) do
        {
            // Attempt loading external icon
            /*strcpy(tempbuf, me->path);
            char* ext = getExtension(tempbuf);

            strcpy(ext, ".jpg");
            iconLoaded = menuEntryLoadExternalIcon(me, tempbuf, false);
            if (iconLoaded) break;

            if (isOldAppFolder)
            {
                char* slash = getSlash(tempbuf);

                strcpy(slash, "/icon.jpg");
                iconLoaded = menuEntryLoadExternalIcon(me, tempbuf, false);
                if (iconLoaded) break;
            }*/

            // Attempt loading the embedded icon
            if (!shortcut)
                iconLoaded = menuEntryLoadEmbeddedIcon(me);
        } while (0);

        if (iconLoaded)
        {
            menuEntryParseIcon(me);
        }

        launchMenuEntryLoadABIRevision(me);

        bool nacpLoaded = false;

        nacpLoaded = menuEntryLoadEmbeddedNacp(me);

        if (nacpLoaded)
        {
            menuEntryParseNacp(me);

            // Fix description for some applications using multiple spaces to indicate newline
            //fixSpaceNewLine(me->description);
        }

        // Metadata overrides for shortcuts
        /*if (shortcut)
        {
            if (sc.name) strncpy(me->name, sc.name, ENTRY_NAMELENGTH);
            if (sc.description) strncpy(me->description, sc.description, ENTRY_DESCLENGTH);
            if (sc.author) strncpy(me->author, sc.author, ENTRY_AUTHORLENGTH);
        }*/

        // Load the descriptor
        /*if (shortcut && sc.descriptor && fileExists(sc.descriptor))
            descriptorLoad(&me->descriptor, sc.descriptor);
        else
        {
            strcpy(tempbuf, me->path);
            strcpy(getExtension(tempbuf), ".xml");
            bool found = fileExists(tempbuf);
            if (!found && isOldAppFolder)
            {
                strcpy(tempbuf, me->path);
                strcpy(getSlash(tempbuf), "/descriptor.xml");
                found = fileExists(tempbuf);
            }
            if (found)
                descriptorLoad(&me->descriptor, tempbuf);
        }*/

        // Initialize the argument data
        argData_s* ad = &me->args;
        ad->dst = (char*)&ad->buf[1];
        launchAddArg(ad, me->path);

        // Load the argument(s) from the shortcut
        /*if (shortcut && sc.arg && *sc.arg)
            launchAddArgsFromString(ad, sc.arg);*/

        /*if (shortcut)
            shortcutFree(&sc);*/
    }

    if (me->type == ENTRY_TYPE_THEME) {
        config_t cfg = {0};
        config_init(&cfg);
        config_setting_t *themeInfo;
        const char *name,
                   *author = textGetString(StrId_DefaultPublisher),
                   *version = "1.0.0";

        const char* cfg_path = me->path;
        const char* theme_archive_path = NULL;
        const char* ext = getExtension(me->path);
        bool good_cfg = false;
        bool is_archive = false;
        #ifdef __SWITCH__
        bool is_romfs = false;
        if (strcasecmp(ext, ".romfs")==0) {
            if (R_FAILED(romfsMountFromFsdev(me->path, 0, "themetmp")))
                return false;
            is_romfs = true;
            cfg_path = "themetmp:/theme.cfg";
            theme_archive_path = "themetmp:/";
        }
        #endif

        if (strcasecmp(ext, ".romfs")!=0 && strcasecmp(ext, ".cfg")!=0) {
            theme_archive_path = me->path;
        }
        if (theme_archive_path) {
            if (!PHYSFS_mount(theme_archive_path, "themetmp", 0)) cfg_path = NULL;
            else {
                is_archive = true;
                cfg_path = "themetmp/theme.cfg";
            }
        }

        if (cfg_path) {
            if (!is_archive) good_cfg = config_read_file(&cfg, cfg_path);
            else {
                u8 *cfg_buf = NULL;
                good_cfg = assetsPhysfsReadFile(cfg_path, &cfg_buf, NULL, true);
                if (good_cfg) good_cfg = config_read_string(&cfg, (char*)cfg_buf);
                free(cfg_buf);
            }
        }

        if (good_cfg) {
            themeInfo = config_lookup(&cfg, "themeInfo");
            if (themeInfo != NULL) {
                if(config_setting_lookup_string(themeInfo, "name", &name))
                    strncpy(me->name, name, sizeof(me->name)-1);
                config_setting_lookup_string(themeInfo, "author", &author);
                config_setting_lookup_string(themeInfo, "version", &version);
            }
        }

        strncpy(me->author, author, sizeof(me->author)-1);
        strncpy(me->version, version, sizeof(me->version)-1);
        config_destroy(&cfg);

        if (good_cfg && is_archive) {
            bool iconLoaded = false;

            iconLoaded = menuEntryLoadExternalIcon(me, "themetmp/icon.jpg", true);

            if (iconLoaded) menuEntryParseIcon(me);
        }

        if (is_archive) PHYSFS_unmount(theme_archive_path);

        #ifdef __SWITCH__
        if (is_romfs) romfsUnmount("themetmp");
        #endif
    }

    if (me->type == ENTRY_TYPE_FILE_OTHER)
    {
        if (menu_fileassoc->nEntries == 0) return false;

        for (fileassoc_me = menu_fileassoc->firstEntry, i = 0; fileassoc_me; fileassoc_me = fileassoc_me->next, i++) {
            //For fileassoc_type==0 compare the extension, otherwise compare the filename.
            if (!fileassoc_me->fileassoc_type) {
                strptr = getExtension(me->path);
            }
            if (fileassoc_me->fileassoc_type) {
                strptr = getSlash(me->path);
                if (strptr[0] == '/') strptr++;
            }

            if (strcmp(strptr, fileassoc_me->fileassoc_str)) continue;

            //At this point a match was found.

            me->type = ENTRY_TYPE_FILE;

            //Attempt to load the icon from {me->path filepath with extension .jpg}, then on failure use the icon data from fileassoc_me.
            memset(tempbuf, 0, sizeof(tempbuf));
            strncpy(tempbuf, me->path, sizeof(tempbuf));
            tempbuf[sizeof(tempbuf)-1] = 0;
            strptr = getExtension(tempbuf);
            strncpy(strptr, ".jpg", sizeof(tempbuf)-1 - ((ptrdiff_t)strptr - (ptrdiff_t)tempbuf));

            bool iconLoaded = false;

            iconLoaded = menuEntryLoadExternalIcon(me, tempbuf, false);

            if (iconLoaded) menuEntryParseIcon(me);

            if (iconLoaded && !(me->icon_gfx && me->icon_gfx_small)) iconLoaded = 0;

            if (!iconLoaded && fileassoc_me->icon_gfx && fileassoc_me->icon_gfx_small)
                iconLoaded = menuEntryImportIconGfx(me, fileassoc_me->icon_gfx, fileassoc_me->icon_gfx_small, fileassoc_me->icon_mode);

            //Attempt to load the nacp from {me->path filepath with extension .nacp}, then on failure use the config from fileassoc_me.
            memset(tempbuf, 0, sizeof(tempbuf));
            strncpy(tempbuf, me->path, sizeof(tempbuf));
            tempbuf[sizeof(tempbuf)-1] = 0;
            strptr = getExtension(tempbuf);
            strncpy(strptr, ".nacp", sizeof(tempbuf)-1 - ((ptrdiff_t)strptr - (ptrdiff_t)tempbuf));

            bool nacpLoaded = menuEntryLoadExternalNacp(me, tempbuf);

            if (nacpLoaded) menuEntryParseNacp(me);
            else {
                strncpy(me->author, fileassoc_me->author, sizeof(me->author));
                me->author[sizeof(me->author)-1] = 0;

                strncpy(me->version, fileassoc_me->version, sizeof(me->version));
                me->version[sizeof(me->version)-1] = 0;
            }

            // Initialize the argument data
            argData_s* ad = &me->args;
            argData_s* ad_assoc = &fileassoc_me->args;
            char *arg_src = (char*)&ad_assoc->buf[1];
            bool ftoken_found=0;
            ad->dst = (char*)&ad->buf[1];

            for (u32 argi=0; argi<ad_assoc->buf[0]; argi++, arg_src+= strlen(arg_src)+1) {
                if (argi) {
                    strptr = strchr(arg_src, '%');
                    if (strptr && strptr[0] && strptr[1] && (strptr == arg_src || strptr[-1] != '\\')) {
                        if (strptr[1] == 'f') {
                            memset(tempbuf, 0, sizeof(tempbuf));
                            snprintf(tempbuf, sizeof(tempbuf)-1, "%.*s%s%s", (int)((uintptr_t)strptr-(uintptr_t)arg_src), arg_src, me->path, &strptr[2]);
                            launchAddArg(ad, tempbuf);
                            ftoken_found = 1;
                            continue;
                        }
                    }
                }

                launchAddArg(ad, arg_src);
            }
            if (!ftoken_found) launchAddArg(ad, me->path);

            strncpy(me->path, fileassoc_me->path, sizeof(me->path));
            me->path[sizeof(me->path)-1] = 0;

            me->abi_revision = fileassoc_me->abi_revision;

            return true;
        }
        return false;
    }

    //check for .filename.star in same path
    strptr = getSlash(me->path);
    if (strptr[0] == '/') strptr++;
    int strptrLen = strlen(strptr);
    snprintf(me->starpath, sizeof(me->starpath)-1, "%.*s.%.*s.star", (int)(strlen(me->path) - strptrLen), me->path, (int)strptrLen, strptr);
    me->starred = fileExists(me->starpath);

    return true;
}

void menuEntryFileassocLoad(const char* filepath) {
    bool success=0, iconLoaded=0;
    menuEntry_s* me = NULL;

    config_setting_t *fileassoc = NULL, *targets = NULL, *target = NULL, *app_args = NULL, *target_args = NULL;
    config_t cfg = {0};
    int targets_len=0, args_len=0, i;
    const char *strptr = NULL;

    char app_path[PATH_MAX+8];
    char main_icon_path[PATH_MAX+1];
    char target_icon_path[PATH_MAX+1];
    char target_file_extension[PATH_MAX+1];
    char target_filename[PATH_MAX+1];

    char app_author[ENTRY_AUTHORLENGTH+2];
    char app_version[ENTRY_VERLENGTH+2];

    uint8_t *app_icon_gfx = NULL;
    uint8_t *app_icon_gfx_small = NULL;
    ImageMode app_icon_mode = IMAGE_MODE_RGB24;

    config_init(&cfg);

    memset(app_path, 0, sizeof(app_path));
    memset(main_icon_path, 0, sizeof(main_icon_path));
    memset(app_author, 0, sizeof(app_author));
    memset(app_version, 0, sizeof(app_version));

    if (!fileExists(filepath)) return;

    if (config_read_file(&cfg, filepath)) {
        fileassoc = config_lookup(&cfg, "fileassoc");

        if (fileassoc != NULL) {
            if (config_setting_lookup_string(fileassoc, "app_path", &strptr))
                snprintf(app_path, sizeof(app_path)-1, "%s%s", menuGetRootBasePath(), strptr);
            if (config_setting_lookup_string(fileassoc, "icon_path", &strptr))
                snprintf(main_icon_path, sizeof(main_icon_path)-1, "%s%s", menuGetRootBasePath(), strptr);
            app_args = config_setting_lookup(fileassoc, "app_args");
            targets = config_setting_lookup(fileassoc, "targets");

            if (app_path[0] && targets) {
                targets_len = config_setting_length(targets);

                if (targets_len > 0) {
                    //Load the author/version and icon data with the NRO app path.
                    me = menuCreateEntry(ENTRY_TYPE_FILE);
                    success = 0;
                    if (me) {
                        strncpy(me->path, app_path, sizeof(me->path)-1);
                        me->path[sizeof(me->path)-1] = 0;

                        strptr = getSlash(app_path);
                        if(strptr[0] == '/') strptr++;

                        if (menuEntryLoad(me, strptr, 0, true)) {
                            strncpy(app_author, me->author, sizeof(app_author));
                            app_author[sizeof(app_author)-1] = 0;
                            strncpy(app_version, me->version, sizeof(app_version));
                            app_version[sizeof(app_version)-1] = 0;
                            app_icon_gfx = me->icon_gfx;
                            app_icon_gfx_small = me->icon_gfx_small;
                            app_icon_mode = me->icon_mode;
                            success = 1;
                        }

                        menuDeleteEntry(me, success);
                        me = NULL;
                    }

                    //Process the targets list.
                    if (success) {
                        for (i=0; i<targets_len; i++) {
                            target = config_setting_get_elem(targets, i);
                            if (target == NULL) continue;

                            memset(target_icon_path, 0, sizeof(target_icon_path));
                            memset(target_file_extension, 0, sizeof(target_file_extension));
                            memset(target_filename, 0, sizeof(target_filename));

                            if (config_setting_lookup_string(target, "icon_path", &strptr))
                                snprintf(target_icon_path, sizeof(target_icon_path)-1, "%s%s", menuGetRootBasePath(), strptr);
                            if (config_setting_lookup_string(target, "file_extension", &strptr))
                                strncpy(target_file_extension, strptr, sizeof(target_file_extension)-1);
                            if (config_setting_lookup_string(target, "filename", &strptr))
                                strncpy(target_filename, strptr, sizeof(target_filename)-1);
                            target_args = config_setting_lookup(target, "app_args");

                            //string_is_set for target_file_extension and target_filename must differ: only 1 can be set, not both set or both not set.
                            if ((target_file_extension[0]!=0) == (target_filename[0]!=0)) continue;

                            me = menuCreateEntry(ENTRY_TYPE_FILEASSOC);
                            iconLoaded = 0;

                            if (me) {
                                strncpy(me->path, app_path, sizeof(me->path));
                                me->path[sizeof(me->path)-1] = 0;
                                strncpy(me->author, app_author, sizeof(me->author));
                                me->author[sizeof(me->author)-1] = 0;
                                strncpy(me->version, app_version, sizeof(me->version));
                                me->version[sizeof(me->version)-1] = 0;

                                if (target_file_extension[0]) {
                                    me->fileassoc_type = 0;
                                    strncpy(me->fileassoc_str, target_file_extension, sizeof(me->fileassoc_str));
                                } else if (target_filename[0]) {
                                    me->fileassoc_type = 1;
                                    strncpy(me->fileassoc_str, target_filename, sizeof(me->fileassoc_str));
                                }
                                me->fileassoc_str[sizeof(me->fileassoc_str)-1] = 0;

                                if (target_icon_path[0]) iconLoaded = menuEntryLoadExternalIcon(me, target_icon_path, false);
                                if (!iconLoaded && main_icon_path[0]) iconLoaded = menuEntryLoadExternalIcon(me, main_icon_path, false);

                                if (iconLoaded) {
                                    menuEntryParseIcon(me);
                                } else {
                                    iconLoaded = menuEntryImportIconGfx(me, app_icon_gfx, app_icon_gfx_small, app_icon_mode);
                                }

                                argData_s* ad = &me->args;
                                ad->dst = (char*)&ad->buf[1];
                                launchAddArg(ad, me->path);

                                config_setting_t *config_args = target_args ? target_args : app_args;
                                if (config_args) {
                                    args_len = config_setting_length(config_args);
                                    for (int argi=0; argi<args_len; argi++) {
                                        strptr = config_setting_get_string_elem(config_args, argi);
                                        if (strptr==NULL) continue;

                                        launchAddArg(ad, strptr);
                                    }
                                }

                                launchMenuEntryLoadABIRevision(me);
                            }

                            if (me) menuFileassocAddEntry(me);
                        }
                    }
                }
            }
        }
    }

    if (success) {
        FreeImageBuffer(app_icon_gfx);
        FreeImageBuffer(app_icon_gfx_small);
    }

    config_destroy(&cfg);
}

static const uint8_t png_signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

// Gives each fully transparent pixel the average colour of its visible
// neighbours. Icons are filtered when they are scaled, and the colour stored in
// a transparent pixel (often black) would otherwise darken the icon's edge.
static void bleedTransparentPixels(uint8_t *pixels, int width, int height) {
    size_t size = (size_t)width*height*4;
    uint8_t *source = (uint8_t*)malloc(size);
    if (source == NULL) return;
    memcpy(source, pixels, size);

    for (int y=0; y<height; y++) {
        for (int x=0; x<width; x++) {
            uint8_t *pixel = &pixels[((size_t)y*width + x)*4];
            if (pixel[3] != 0) continue;

            unsigned int sum[3] = {0, 0, 0};
            unsigned int count = 0;
            for (int dy=-1; dy<=1; dy++) {
                for (int dx=-1; dx<=1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;

                    const uint8_t *neighbour = &source[((size_t)ny*width + nx)*4];
                    if (neighbour[3] == 0) continue;

                    sum[0] += neighbour[0];
                    sum[1] += neighbour[1];
                    sum[2] += neighbour[2];
                    count++;
                }
            }

            if (count == 0) continue;
            pixel[0] = sum[0] / count;
            pixel[1] = sum[1] / count;
            pixel[2] = sum[2] / count;
        }
    }

    free(source);
}

void menuEntryParseIcon(menuEntry_s* me) {
    if (me->icon_size==0 || me->icon==NULL) return;

    ThemeLayoutObject *layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuActiveEntryIcon];
    ThemeLayoutObject *layoutobj2 = &themeCurrent.layoutObjects[ThemeLayoutId_MenuListIcon];

    // Icons are JPEG (Switch 1 256x256 or Switch 2 512x512+ icon_*.dat), or PNG.
    bool is_png = me->icon_size >= sizeof(png_signature) && memcmp(me->icon, png_signature, sizeof(png_signature)) == 0;
    ImageMode mode = is_png ? IMAGE_MODE_RGBA32 : IMAGE_MODE_RGB24;

    uint8_t *tmp_gfx = NULL;
    int src_w = 0, src_h = 0;
    bool ret = false;

    if (is_png) {
        ret = assetsLoadPngFromMemoryAny(me->icon, me->icon_size, &tmp_gfx, mode, &src_w, &src_h);
        if (ret) bleedTransparentPixels(tmp_gfx, src_w, src_h);
    }
    else {
        ret = assetsLoadJpgFromMemoryAny(me->icon, me->icon_size, &tmp_gfx, mode, &src_w, &src_h);
    }

    if (ret) me->icon_gfx = downscaleImg(tmp_gfx, src_w, src_h, layoutobj->size[0], layoutobj->size[1], mode);

    if (ret && me->icon_gfx==NULL) ret = false;

    me->icon_size = 0;
    free(me->icon);
    me->icon = NULL;

    if (ret) me->icon_gfx_small = downscaleImg(tmp_gfx, src_w, src_h, layoutobj2->size[0], layoutobj2->size[1], mode);

    if (!ret || me->icon_gfx_small == NULL) {
        FreeImageBuffer(me->icon_gfx);
        me->icon_gfx = NULL;
    }
    else {
        me->icon_mode = mode;
    }

    free(tmp_gfx);
}

uint8_t *downscaleImg(const uint8_t *image, int srcWidth, int srcHeight, int destWidth, int destHeight, ImageMode mode) {
    uint8_t *out;

    switch (mode) {
        case IMAGE_MODE_RGBA32:
            out = (uint8_t*)malloc(destWidth*destHeight*4);
            break;

        default:
            out = (uint8_t*)malloc(destWidth*destHeight*3);
            break;
    }

    if (out == NULL) {
        return NULL;
    }

    if (srcWidth == destWidth && srcHeight == destHeight) {
        memcpy(out, image, destWidth*destHeight*(mode==IMAGE_MODE_RGBA32 ? 4 : 3));
        return out;
    }

    int tmpx, tmpy;
    int pos;
    float sourceX, sourceY;
    float xScale = (float)srcWidth / (float)destWidth;
    float yScale = (float)srcHeight / (float)destHeight;
    int pixelX, pixelY;
    uint8_t r1, r2, r3, r4;
    uint8_t g1, g2, g3, g4;
    uint8_t b1, b2, b3, b4;
    uint8_t a1, a2, a3, a4;
    float fx, fy, fx1, fy1;
    int w1, w2, w3, w4;

    for (tmpx=0; tmpx<destWidth; tmpx++) {
        for (tmpy=0; tmpy<destHeight; tmpy++) {
            sourceX = tmpx * xScale;
            sourceY = tmpy * yScale;
            pixelX = (int)sourceX;
            pixelY = (int)sourceY;
            if (pixelX >= srcWidth) pixelX = srcWidth - 1;
            if (pixelY >= srcHeight) pixelY = srcHeight - 1;
            int pixelX1 = (pixelX + 1 < srcWidth) ? (pixelX + 1) : pixelX;
            int pixelY1 = (pixelY + 1 < srcHeight) ? (pixelY + 1) : pixelY;

            // get colours from four surrounding pixels
            if (mode == IMAGE_MODE_RGBA32)
                pos = (pixelY * srcWidth + pixelX) * 4;
            else
                pos = (pixelY * srcWidth + pixelX) * 3;

            r1 = image[pos+0];
            g1 = image[pos+1];
            b1 = image[pos+2];

            if (mode == IMAGE_MODE_RGBA32)
                a1 = image[pos+3];


            if (mode == IMAGE_MODE_RGBA32)
                pos = (pixelY * srcWidth + pixelX1) * 4;
            else
                pos = (pixelY * srcWidth + pixelX1) * 3;

            r2 = image[pos+0];
            g2 = image[pos+1];
            b2 = image[pos+2];

            if (mode == IMAGE_MODE_RGBA32)
                a2 = image[pos+3];


            if (mode == IMAGE_MODE_RGBA32)
                pos = (pixelY1 * srcWidth + pixelX) * 4;
            else
                pos = (pixelY1 * srcWidth + pixelX) * 3;

            r3 = image[pos+0];
            g3 = image[pos+1];
            b3 = image[pos+2];

            if (mode == IMAGE_MODE_RGBA32)
                a3 = image[pos+3];


            if (mode == IMAGE_MODE_RGBA32)
                pos = (pixelY1 * srcWidth + pixelX1) * 4;
            else
                pos = (pixelY1 * srcWidth + pixelX1) * 3;

            r4 = image[pos+0];
            g4 = image[pos+1];
            b4 = image[pos+2];

            if (mode == IMAGE_MODE_RGBA32)
                a4 = image[pos+3];

            // determine weights
            fx = sourceX - pixelX;
            fy = sourceY - pixelY;
            fx1 = 1.0f - fx;
            fy1 = 1.0f - fy;

            w1 = (int)(fx1*fy1*256.0);
            w2 = (int)(fx*fy1*256.0);
            w3 = (int)(fx1*fy*256.0);
            w4 = (int)(fx*fy*256.0);

            // set output pixels
            if (mode == IMAGE_MODE_RGBA32)
                pos = ((tmpy*destWidth) + tmpx) * 4;
            else
                pos = ((tmpy*destWidth) + tmpx) * 3;

            out[pos+0] = (uint8_t)((r1 * w1 + r2 * w2 + r3 * w3 + r4 * w4) >> 8);
            out[pos+1] = (uint8_t)((g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) >> 8);
            out[pos+2] = (uint8_t)((b1 * w1 + b2 * w2 + b3 * w3 + b4 * w4) >> 8);

            if (mode == IMAGE_MODE_RGBA32)
                out[pos+3] = (uint8_t)((a1 * w1 + a2 * w2 + a3 * w3 + a4 * w4) >> 8);
        }
    }

    return out;
}

void menuEntryParseNacp(menuEntry_s* me) {
    NacpLanguageEntry *langentry = NULL;

    if (me->nacp==NULL) return;

    strncpy(me->version, me->nacp->display_version, sizeof(me->version)-1);

    #ifdef __SWITCH__
    Result rc=0;
    rc = nacpGetLanguageEntry(me->nacp, &langentry);

    if (R_SUCCEEDED(rc) && langentry!=NULL) {
    #else
    langentry = &me->nacp->lang[0];
    if (1) {
    #endif
        strncpy(me->name, langentry->name, sizeof(me->name)-1);
        strncpy(me->author, langentry->author, sizeof(me->author)-1);
    }

    free(me->nacp);
    me->nacp = NULL;
}

