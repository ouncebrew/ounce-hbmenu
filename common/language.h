#pragma once
#ifdef __SWITCH__
#include <switch.h>
#endif

typedef enum
{
    StrId_Loading = 0,
    StrId_AppletMode,
    StrId_Directory,
    StrId_DefaultPublisher,
    StrId_IOError,
    StrId_CouldNotOpenFile,
    StrId_NroNotFound,

    StrId_NoAppsFound_Title,
    StrId_NoAppsFound_Msg,

    StrId_LastLoadResult,
    StrId_AppLaunchError,

    StrId_AppInfo_Author,
    StrId_AppInfo_Version,
    StrId_Actions_Launch,
    StrId_Actions_Open,
    StrId_Actions_Back,
    StrId_Actions_Apply,
    StrId_Actions_Star,
    StrId_Actions_Unstar,

    StrId_MsgBox_OK,

    StrId_Reboot,
    StrId_ReturnToHome,

    StrId_TitleSelector,
    StrId_ErrorReadingTitleMetadata,
    StrId_NoTitlesFound,
    StrId_SelectTitle,

    StrId_NoTargetTitleSupport,
    StrId_MissingTargetTitle,

    StrId_NetLoader,
    StrId_NetLoaderUnavailable,
    StrId_NetLoaderOffline,
    StrId_NetLoaderError,
    StrId_NetLoaderActive,
    StrId_NetLoaderTransferring,

    StrId_ThemeMenu,
    StrId_ThemeNotApplied,
    StrId_DefaultThemeName,

    StrId_RecompileWarning,

    StrId_Switch1Warning,
    StrId_MsgBox_LaunchAnyway,
    StrId_MsgBox_Cancel,

    // What a Switch 2 program's DYNLINK_RESULT_* return means.
    StrId_LoadResult_NotLinked,
    StrId_LoadResult_LibraryNotFound,
    StrId_LoadResult_LibraryNotLoaded,
    StrId_LoadResult_UndefinedSymbol,
    StrId_LoadResult_BadRelocation,
    StrId_LoadResult_PreloadNotLoaded,
    StrId_LoadResult_NoRuntimeLibrary,

    StrId_Max,
} StrId;

extern const char* const g_strings[StrId_Max][17];

