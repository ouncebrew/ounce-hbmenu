#include "common.h"
#include <ctype.h>

static menu_s s_menu[2];
static menu_s s_menuFileassoc[2];
static bool s_curMenu, s_curMenuFileassoc;

menu_s* menuGetCurrent(void) {
    return &s_menu[s_curMenu];
}

menu_s* menuFileassocGetCurrent(void) {
    return &s_menuFileassoc[s_curMenuFileassoc];
}

menuEntry_s* menuCreateEntry(MenuEntryType type) {
    menuEntry_s* me = (menuEntry_s*)malloc(sizeof(menuEntry_s));
    menuEntryInit(me, type);
    return me;
}

void menuDeleteEntry(menuEntry_s* me, bool skip_icongfx) {
    menuEntryFree(me, skip_icongfx);
    free(me);
}

static void _menuAddEntry(menu_s *m, menuEntry_s* me) {
    me->menu = m;
    if (m->lastEntry)
    {
        m->lastEntry->next = me;
        m->lastEntry = me;
    } else
    {
        m->firstEntry = me;
        m->lastEntry = me;
    }
    m->xPos = 0;
    m->slideSpeed = 0;
    m->nEntries ++;
}

static void menuAddEntry(menuEntry_s* me) {
    _menuAddEntry(&s_menu[!s_curMenu], me);
}

void menuFileassocAddEntry(menuEntry_s* me) {
    _menuAddEntry(&s_menuFileassoc[!s_curMenuFileassoc], me);
}

static void menuAddEntryToFront(menuEntry_s* me) {
    menu_s* m = &s_menu[!s_curMenu];
    me->menu = m;
    if (m->lastEntry)
    {
        me->next = m->firstEntry;
        m->firstEntry = me;
    } else
    {
        m->firstEntry = me;
        m->lastEntry = me;
    }
    m->xPos = 0;
    m->slideSpeed = 0;
    m->nEntries ++;
}

static void _menuClear(menu_s* m) {
    menuEntry_s *cur, *next;
    for (cur = m->firstEntry; cur; cur = next)
    {
        next = cur->next;
        menuDeleteEntry(cur, 0);
    }
    memset(m, 0, sizeof(*m));
}

static void menuClear(void) {
    _menuClear(&s_menu[!s_curMenu]);
}

static void menuFileassocClear(void) {
    _menuClear(&s_menuFileassoc[!s_curMenuFileassoc]);
}

static int menuEntryCmp(const void *p1, const void *p2) {
    const menuEntry_s* lhs = *(menuEntry_s**)p1;
    const menuEntry_s* rhs = *(menuEntry_s**)p2;

    if(lhs->type == rhs->type)
        return strcasecmp(lhs->name, rhs->name);
    if(lhs->type == ENTRY_TYPE_FOLDER)
        return -1;
    return 1;
}

static void menuSort(void) {
    int i;
    menu_s* m = &s_menu[!s_curMenu];
    int nEntries = m->nEntries;
    if (nEntries==0) return;
    int nEntriesStar = 0, nEntriesNoStar = 0;

    menuEntry_s** list = (menuEntry_s**)calloc(nEntries, sizeof(menuEntry_s*));
    if(list == NULL) return;
    menuEntry_s** listStar = (menuEntry_s**)calloc(nEntries, sizeof(menuEntry_s*));
    if(listStar == NULL) {
        free(list);
        return;
    }

    menuEntry_s* p = m->firstEntry;
    for(i = 0; i < nEntries; ++i) {
        if (p->starred)
            listStar[nEntriesStar++] = p;
        else
            list[nEntriesNoStar++] = p;
        p = p->next;
    }

    qsort(listStar, nEntriesStar, sizeof(menuEntry_s*), menuEntryCmp);
    qsort(list, nEntriesNoStar, sizeof(menuEntry_s*), menuEntryCmp);

    menuEntry_s** pp = &m->firstEntry;
    for(i = 0; i < nEntriesStar; ++i) {
        *pp = listStar[i];
        pp = &(*pp)->next;
    }
    for(i = 0; i < nEntriesNoStar; ++i) {
        *pp = list[i];
        pp = &(*pp)->next;
    }
    m->lastEntry = list[nEntries-1];
    *pp = NULL;

    free(list);
    free(listStar);
}

void menuReorder (void) {
    s_curMenu = !s_curMenu;
    menuSort();
    s_curMenu = !s_curMenu;
    menuClear();
}

static char* trim_text(char* text) {
    while (*text && isspace((unsigned char)*text))
        text++;
    char* end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        end--;
    *end = '\0';
    return text;
}

static bool s_hideFolders;

static void hiddenFoldersPath(char* out, size_t out_size) {
    snprintf(out, out_size, "%s/config/nx-hbmenu/hidden_folders.txt",
             menuGetRootBasePath());
}

static bool menuFolderHidden(const char* name) {
    (void)name;
    return s_hideFolders;
}

static void hiddenFoldersSave(void) {
    char path[PATH_MAX + 32];
    hiddenFoldersPath(path, sizeof(path));
    FILE* file = fopen(path, "w");
    if (!file)
        return;
    fprintf(file, "%d\n", s_hideFolders ? 1 : 0);
    fclose(file);
}

void menuHiddenLoad(void) {
    s_hideFolders = false;
    char path[PATH_MAX + 32];
    hiddenFoldersPath(path, sizeof(path));
    FILE* file = fopen(path, "r");
    if (!file)
        return;
    int value = 0;
    if (fscanf(file, "%d", &value) == 1)
        s_hideFolders = value != 0;
    fclose(file);
}

bool menuHiddenFoldersEnabled(void) {
    return s_hideFolders;
}

void menuHiddenToggle(void) {
    s_hideFolders = !s_hideFolders;
    hiddenFoldersSave();
}

void settingsMenuScan(const char* target) {
    menuClear();
    menu_s* menu = &s_menu[!s_curMenu];
    if (getcwd(menu->dirname, PATH_MAX + 1) == NULL) {
        snprintf(menu->dirname, sizeof(menu->dirname), "%s", target);
    }
    menuEntry_s* me = menuCreateEntry(ENTRY_TYPE_FOLDER);
    if (!me)
        return;
    snprintf(me->path, sizeof(me->path), "settings/hide-folders");
    snprintf(me->name, sizeof(me->name), "Hide folders: %s",
             s_hideFolders ? "On" : "Off");
    menuAddEntry(me);
    menuSort();
    s_curMenu = !s_curMenu;
    menuClear();
}

void settingsMenuToggleCurrent(void) {
    menu_s* menu = menuGetCurrent();
    if (!menu || menu->curEntry < 0 || menu->curEntry >= menu->nEntries)
        return;
    s_hideFolders = !s_hideFolders;
    hiddenFoldersSave();
    settingsMenuScan(menuGetRootPath());
    menu = menuGetCurrent();
    if (menu->nEntries > 0)
        menu->curEntry = 0;
}

static const char* menuEntryArg1(const menuEntry_s* entry) {
    if (!entry || entry->args.buf[0] < 2)
        return NULL;
    const char* p = (const char*)&entry->args.buf[1];
    const size_t avail = sizeof(entry->args.buf) - sizeof(uint32_t);
    const size_t len0 = strnlen(p, avail);
    if (len0 + 1 >= avail)
        return NULL;
    return p + len0 + 1;
}

static bool menuHasEntry(const char* path, const char* arg1) {
    for (menuEntry_s* entry = s_menu[!s_curMenu].firstEntry; entry; entry = entry->next) {
        if (strcmp(entry->path, path) != 0)
            continue;
        const char* existing_arg1 = menuEntryArg1(entry);
        if (!arg1 && !existing_arg1)
            return true;
        if (arg1 && existing_arg1 && strcmp(existing_arg1, arg1) == 0)
            return true;
    }
    return false;
}

static bool isRawGameDir(const char* dir_path) {
    struct stat st;
    if (stat(dir_path, &st) != 0 || !S_ISDIR(st.st_mode))
        return false;
    char probe[PATH_MAX + 32];
    snprintf(probe, sizeof(probe), "%s/exefs/main", dir_path);
    if (fileExists(probe))
        return true;
    snprintf(probe, sizeof(probe), "%s/game/exefs/main", dir_path);
    return fileExists(probe);
}

static bool findGameLoaderNro(char* out, size_t out_sz) {
    static const char* const candidates[] = {
        "sdmc:/switch/nup-loader/nup-loader.nro",
        "sdmc:/switch/nup-loader.nro",
        "sdmc:/kaihen/programs/nup-loader.nro",
        "sdmc:/nup-loader.nro",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (fileExists(candidates[i])) {
            snprintf(out, out_sz, "%s", candidates[i]);
            return true;
        }
    }
    return false;
}

static void menuLoadShortcuts(void) {
    char path[PATH_MAX + 32];
    snprintf(path, sizeof(path), "%s/shortcuts.txt", menuGetRootPath());
    FILE* file = fopen(path, "r");
    if (!file)
        return;

    char loader_nro[PATH_MAX + 8] = {};
    const bool has_loader = findGameLoaderNro(loader_nro, sizeof(loader_nro));

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        char* text = trim_text(line);
        if (!text[0] || text[0] == '#')
            continue;
        char* separator = strchr(text, '=');
        const bool had_eq = (separator != NULL);
        if (!separator)
            separator = strchr(text, '|');
        if (!separator)
            continue;
        *separator = '\0';
        char* name = trim_text(text);
        char* rhs = trim_text(separator + 1);
        char* icon_override = NULL;
        bool trace_mode = false;

        // Parse optional |icon=sdmc:/... and |trace modifiers when '=' separated name=target
        if (had_eq) {
            char* mod = strchr(rhs, '|');
            while (mod) {
                *mod = '\0';
                char* opt = trim_text(mod + 1);
                char* next_mod = strchr(opt, '|');
                if (next_mod)
                    *next_mod = '\0';
                opt = trim_text(opt);
                if (strncmp(opt, "icon=", 5) == 0)
                    icon_override = trim_text(opt + 5);
                else if (strcmp(opt, "trace") == 0)
                    trace_mode = true;
                mod = next_mod;
            }
        }

        char* target = trim_text(rhs);
        size_t tlen = strlen(target);
        while (tlen > 6 && target[tlen - 1] == '/')
            target[--tlen] = '\0';
        if (!name[0] || strncmp(target, "sdmc:/", 6) != 0)
            continue;
        if (tlen >= sizeof(((menuEntry_s*)0)->path))
            continue;

        if (isRawGameDir(target)) {
            if (!has_loader || menuHasEntry(loader_nro, target))
                continue;
            menuEntry_s* entry = menuCreateEntry(ENTRY_TYPE_FILE);
            if (!entry)
                continue;
            if (!menuEntryLoadGameShortcut(entry, name, loader_nro, target, icon_override, trace_mode)) {
                menuDeleteEntry(entry, 0);
                continue;
            }
            menuAddEntry(entry);
            continue;
        }

        if (menuHasEntry(target, NULL))
            continue;

        menuEntry_s* entry = menuCreateEntry(ENTRY_TYPE_FILE);
        if (!entry)
            continue;
        snprintf(entry->path, sizeof(entry->path), "%s", target);
        if (!menuEntryLoad(entry, name, false, true)) {
            menuDeleteEntry(entry, 0);
            continue;
        }
        menuAddEntry(entry);
    }
    fclose(file);
}

#ifdef __SWITCH__
extern bool hbPluginGetBool(const char* reg_name, const char* key, bool default_val) __attribute__((weak));
#endif

static bool isDirPath(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void tryAddAutoGame(const char* loader_nro, const char* game_dir,
                           const char* folder_name, int prog_idx) {
    if (!isRawGameDir(game_dir) || menuHasEntry(loader_nro, game_dir))
        return;
    menuEntry_s* entry = menuCreateEntry(ENTRY_TYPE_FILE);
    if (!entry)
        return;
    char label_hint[64] = {};
    const char* name_arg = NULL;
    if (prog_idx == 1) {
        snprintf(label_hint, sizeof(label_hint), "+ [Switch 2]");
        name_arg = label_hint;
    } else if (prog_idx > 1) {
        snprintf(label_hint, sizeof(label_hint), "+ [P%d]", prog_idx);
        name_arg = label_hint;
    }
    if (!menuEntryLoadGameShortcut(entry, name_arg, loader_nro, game_dir, NULL, false)) {
        menuDeleteEntry(entry, 0);
        return;
    }
    // If no NACP was found and game_dir ends with "programN", prefer parent folder_name + tag
    if (prog_idx > 0 && folder_name && folder_name[0] &&
        strncmp(entry->name, "program", 7) == 0) {
        if (prog_idx == 1)
            snprintf(entry->name, sizeof(entry->name), "%.42s [Switch 2]", folder_name);
        else
            snprintf(entry->name, sizeof(entry->name), "%.48s [P%d]", folder_name, prog_idx);
    }
    menuAddEntry(entry);
}

static void menuAutoScanGames(void) {
    bool enabled = true;
#ifdef __SWITCH__
    if (hbPluginGetBool)
        enabled = hbPluginGetBool("nupvfs", "games", true);
#endif
    if (!enabled)
        return;

    char loader_nro[PATH_MAX + 8] = {};
    if (!findGameLoaderNro(loader_nro, sizeof(loader_nro)))
        return;

    static const char* const roots[] = {
        "sdmc:/switch/KaiDump/dumps",
        "sdmc:/backups",
        "sdmc:/games",
        "sdmc:/nup",
        "sdmc:/NUX",
        "sdmc:/backups/nup",
        "sdmc:/backups/NUX",
    };

    for (size_t r = 0; r < sizeof(roots) / sizeof(roots[0]); r++) {
        DIR* dir = opendir(roots[r]);
        if (!dir)
            continue;
        struct dirent* dp;
        while ((dp = readdir(dir)) != NULL) {
            if (dp->d_name[0] == '.')
                continue;
            char full[PATH_MAX + 16];
            snprintf(full, sizeof(full), "%s/%s", roots[r], dp->d_name);
            if (!isDirPath(full))
                continue;

            tryAddAutoGame(loader_nro, full, dp->d_name, 0);

            char prog_parent[PATH_MAX + 24];
            snprintf(prog_parent, sizeof(prog_parent), "%s/game", full);
            if (!isDirPath(prog_parent))
                snprintf(prog_parent, sizeof(prog_parent), "%s", full);
            for (int p = 1; p <= 7; p++) {
                char prog_dir[PATH_MAX + 40];
                snprintf(prog_dir, sizeof(prog_dir), "%s/program%d", prog_parent, p);
                if (!isDirPath(prog_dir))
                    break;
                tryAddAutoGame(loader_nro, prog_dir, dp->d_name, p);
            }
        }
        closedir(dir);
    }
}

int menuScan(const char* target) {
    int pos;
    char dirsep[8];

    const int previousEntry = s_menu[s_curMenu].curEntry;
    menuClear();
    if (chdir(target) < 0) return 1;
    if (getcwd(s_menu[!s_curMenu].dirname, PATH_MAX+1) == NULL)
        return 1;

    memset(dirsep, 0, sizeof(dirsep));
    dirsep[0] = '/';

    //While cwd will not have '/' at the end normally, it will have it when cwd is the root dir ("sdmc:/"). Don't add '/' to the path below when it's already present.
    pos = strlen(s_menu[!s_curMenu].dirname);
    if (pos > 0) {
        if (s_menu[!s_curMenu].dirname[pos-1] == '/') dirsep[0] = 0;
    }

    DIR* dir;
    struct dirent* dp;
    char tmp_path[PATH_MAX+1];
    dir = opendir(s_menu[!s_curMenu].dirname);
    if (!dir) return 2;

    while ((dp = readdir(dir)))
    {
        menuEntry_s* me = NULL;
        bool shortcut = false;
        if (dp->d_name[0]=='.')
            continue;

        bool entrytype=0;

        memset(tmp_path, 0, sizeof(tmp_path));
        snprintf(tmp_path, sizeof(tmp_path)-1, "%s%s%s", s_menu[!s_curMenu].dirname, dirsep, dp->d_name);

        #ifdef _DIRENT_HAVE_D_TYPE
        if (dp->d_type == DT_UNKNOWN)
            continue;
        entrytype = dp->d_type != DT_REG;
        #else
        struct stat tmpstat;

        if(stat(tmp_path, &tmpstat)==-1)
            continue;

        entrytype = (tmpstat.st_mode & S_IFMT) != S_IFREG;
        #endif

        if (entrytype && strcmp(s_menu[!s_curMenu].dirname, menuGetRootPath()) == 0 &&
            menuFolderHidden(dp->d_name))
            continue;

        if (entrytype)
            me = menuCreateEntry(ENTRY_TYPE_FOLDER);
        else
        {
            const char* ext = getExtension(dp->d_name);
            if (strcasecmp(ext, ".nro")==0/* || (shortcut = strcasecmp(ext, ".xml")==0)*/)
                me = menuCreateEntry(ENTRY_TYPE_FILE);

            if (!me)
                me = menuCreateEntry(ENTRY_TYPE_FILE_OTHER);
        }

        if (!me)
            continue;

        strncpy(me->path, tmp_path, sizeof(me->path)-1);
        me->path[sizeof(me->path)-1] = 0;

        if (menuEntryLoad(me, dp->d_name, shortcut, true))
            menuAddEntry(me);
        else
            menuDeleteEntry(me, 0);
    }

    closedir(dir);
    if (strcmp(s_menu[!s_curMenu].dirname, menuGetRootPath()) == 0) {
        menuLoadShortcuts();
        menuAutoScanGames();
    }
    menuSort();

    if (s_menu[!s_curMenu].nEntries > 0) {
        s_menu[!s_curMenu].curEntry = previousEntry;
        if (s_menu[!s_curMenu].curEntry >= s_menu[!s_curMenu].nEntries)
            s_menu[!s_curMenu].curEntry = s_menu[!s_curMenu].nEntries - 1;
        if (s_menu[!s_curMenu].curEntry < 0)
            s_menu[!s_curMenu].curEntry = 0;
    }

    // Swap the menu and clear the previous menu
    s_curMenu = !s_curMenu;
    menuClear();
    return 0;
}

int themeMenuScan(const char* target) {
    menuClear();
    if (chdir(target) < 0) return 1;
    if (getcwd(s_menu[!s_curMenu].dirname, PATH_MAX+1) == NULL)
        return 1;
    DIR* dir;
    struct dirent* dp;
    char tmp_path[PATH_MAX+1];
    dir = opendir(s_menu[!s_curMenu].dirname);
    if (!dir) return 2;

    while ((dp = readdir(dir)))
    {
        menuEntry_s* me = NULL;

        bool shortcut = false;
        if (dp->d_name[0]=='.')
            continue;

        memset(tmp_path, 0, sizeof(tmp_path));
        snprintf(tmp_path, sizeof(tmp_path)-1, "%s/%s", s_menu[!s_curMenu].dirname, dp->d_name);

        bool entrytype=0;

        #ifdef _DIRENT_HAVE_D_TYPE
        if (dp->d_type == DT_UNKNOWN)
            continue;
        entrytype = dp->d_type != DT_REG;
        #else
        struct stat tmpstat;

        if(stat(tmp_path, &tmpstat)==-1)
            continue;

        entrytype = (tmpstat.st_mode & S_IFMT) != S_IFREG;
        #endif

        const char* ext = getExtension(dp->d_name);
        if (entrytype || strcasecmp(ext, ".cfg")==0 || strcasecmp(ext, ".romfs")==0 || strcasecmp(ext, ".zip")==0)
            me = menuCreateEntry(ENTRY_TYPE_THEME);

        if (!me)
            continue;

        strncpy(me->path, tmp_path, sizeof(me->path)-1);
        me->path[sizeof(me->path)-1] = 0;
        if (menuEntryLoad(me, dp->d_name, shortcut, true))
            menuAddEntry(me);
        else
            menuDeleteEntry(me, 0);
    }

    closedir(dir);
    menuSort();

    menuEntry_s* me = menuCreateEntry(ENTRY_TYPE_THEME);

    if(me) {
        if(menuEntryLoad(me, textGetString(StrId_DefaultThemeName), false, false))//Create Default theme Menu Entry
            menuAddEntryToFront(me);
        else
            menuDeleteEntry(me, 0);
    }
    // Swap the menu and clear the previous menu
    s_curMenu = !s_curMenu;
    menuClear();
    return 0;
}

int menuFileassocScan(const char* target) {
    menuFileassocClear();
    if (chdir(target) < 0) return 1;
    if (getcwd(s_menuFileassoc[!s_curMenuFileassoc].dirname, PATH_MAX+1) == NULL)
        return 1;
    DIR* dir;
    struct dirent* dp;
    char tmp_path[PATH_MAX+1];
    dir = opendir(s_menuFileassoc[!s_curMenuFileassoc].dirname);
    if (!dir) return 2;

    while ((dp = readdir(dir)))
    {
        if (dp->d_name[0]=='.')
            continue;

        memset(tmp_path, 0, sizeof(tmp_path));
        snprintf(tmp_path, sizeof(tmp_path)-1, "%s/%s", s_menuFileassoc[!s_curMenuFileassoc].dirname, dp->d_name);

        const char* ext = getExtension(dp->d_name);
        if (strcasecmp(ext, ".cfg")!=0)
            continue;

        menuEntryFileassocLoad(tmp_path);
    }

    closedir(dir);

    // Swap the menu and clear the previous menu
    s_curMenuFileassoc = !s_curMenuFileassoc;
    menuFileassocClear();
    return 0;
}
