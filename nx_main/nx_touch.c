#include "nx_touch.h"

#include <switch/arm/counter.h>

/* A controller button press (e.g. ZR on Joy-Con 2) is also reported by the
 * pointer stack as a mouse click one frame later. Ignore mouse click edges
 * for a short grace period after any controller input. */
#define MOUSE_CLICK_GRACE_NS 200000000ULL

#ifdef __SWITCH2__
#include "../switch2/render.h"
#include "../switch2/switch2_ui.h"
#endif

#define TAP_MOVEMENT_GAP 20
#define VERTICAL_SWIPE_HORIZONTAL_PLAY 250
#define VERTICAL_SWIPE_MINIMUM_DISTANCE 300
#define HORIZONTAL_SWIPE_VERTICAL_PLAY 250
#define HORIZONTAL_SWIPE_MINIMUM_DISTANCE 300

#define distance(x1, y1, x2, y2) (int) sqrt(((x2 - x1) * (x2 - x1)) + ((y2 - y1) * (y2 - y1)))

struct touchInfo_s touchInfo;

static bool s_mouseConnected;
static bool s_mouseVisible;
static bool s_mouseHover;
static bool s_mouseLeft;
static bool s_mouseRight;
static int s_mouseX = 640;
static int s_mouseY = 360;
static PadPointer s_pointer;
static bool s_pointerReady;
static u64 s_controllerInputTick;
extern PadState g_pad;

void touchInit(void) {
    touchInfo.gestureInProgress = false;
    touchInfo.isTap = true;
    touchInfo.initMenuXPos = 0;
    touchInfo.initMenuIndex = 0;
    touchInfo.lastSlideSpeed = 0;
    hidInitializeTouchScreen();
#ifdef __SWITCH2__
    hidInitializeMouse();
    s_pointerReady = R_SUCCEEDED(padPointerInitialize(&s_pointer, 1280, 720));
    if (s_pointerReady) {
        HidNmouseHandle handles[4];
        s32 count = 0;
        if (R_SUCCEEDED(hidGetAllNmouseHandles(handles, 4, &count))) {
            for (s32 i = 0; i < count; i++)
                hidEnableNmousePointer(handles[i], true);
        }
    }
#endif
}

void handleTappingOnApp(menu_s* menu, int px) {
    int i = 0;
    menuEntry_s *me = NULL;
    ThemeLayoutObject *layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuList];

    for (me = menu->firstEntry, i = 0; me; me = me->next, i ++) {
        int entry_start_x = layoutobj->posStart[0] + i * layoutobj->posEnd[0];

        int screen_width = 1280;
        if (entry_start_x >= (screen_width - menu->xPos))
            break;

        if (px >= (entry_start_x + menu->xPos) && px <= (entry_start_x + menu->xPos) + layoutobj->size[0]) {
            launchMenuEntryTask(me);
            break;
        }
    }
}

void handleTappingOnOpenLaunch(menu_s* menu) {
    if (menu->nEntries > 0)
    {
        int i;
        menuEntry_s* me;
        for (i = 0, me = menu->firstEntry; i != menu->curEntry; i ++, me = me->next);
        launchMenuEntryTask(me);
    }
}

static inline bool checkInsideTextLayoutObject(ThemeLayoutId id, int x, int y) {
    ThemeLayoutObject *layoutobj = &themeCurrent.layoutObjects[id];
    if (!layoutobj->visible) return false;

    return x > layoutobj->posFinal[0] && x < layoutobj->posFinal[0]+(layoutobj->touchSize[0] ? layoutobj->touchSize[0] : layoutobj->textSize[0]) && y > layoutobj->posFinal[1]-layoutobj->touchSize[1] && y < layoutobj->posFinal[1];
}

void handleTouch(menu_s* menu) {
    ThemeLayoutObject *layoutobj = NULL;
    HidTouchScreenState touch = {0};
    hidGetTouchScreenStates(&touch, 1);

    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuListTiles];
    int entries_count = layoutobj->posEnd[0];
    layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MenuList];

    // On touch start.
    if (touch.count == 1 && !touchInfo.gestureInProgress) {
        touchInfo.gestureInProgress = true;
        touchInfo.firstTouch = touch.touches[0];
        touchInfo.prevTouch = touch.touches[0];
        touchInfo.isTap = true;
        touchInfo.initMenuXPos = menu->xPos;
        touchInfo.initMenuIndex = menu->curEntry;
        touchInfo.lastSlideSpeed = 0;
        menu->slideSpeed = 0;
    }
    // On touch moving.
    else if (touch.count >= 1 && touchInfo.gestureInProgress) {
        touchInfo.lastSlideSpeed = ((int)(touch.touches[0].x - touchInfo.prevTouch.x));

        touchInfo.prevTouch = touch.touches[0];

        if (touchInfo.isTap && (abs(touchInfo.firstTouch.x - touch.touches[0].x) > TAP_MOVEMENT_GAP || abs(touchInfo.firstTouch.y - touch.touches[0].y) > TAP_MOVEMENT_GAP)) {
            touchInfo.isTap = false;
        }
        if (!menuIsMsgBoxOpen() && touchInfo.firstTouch.y > layoutobj->posStart[1] && touchInfo.firstTouch.y < layoutobj->posStart[1]+layoutobj->size[1] && !touchInfo.isTap && menu->nEntries > entries_count) {

            if (!touchInfo.isTap) {
                menu->slideSpeed = touchInfo.lastSlideSpeed;
            }
        }
    }
    // On touch end.
    else if (touchInfo.gestureInProgress) {
        int x1 = touchInfo.firstTouch.x;
        int y1 = touchInfo.firstTouch.y;
        int x2 = touchInfo.prevTouch.x;
        int y2 = touchInfo.prevTouch.y;

        if (!touchInfo.isTap) {
            menu->slideSpeed = touchInfo.lastSlideSpeed;
        }

        bool netloader_active = menuIsNetloaderActive();

        if (menuIsMsgBoxOpen() && !netloader_active) {
            layoutobj = &themeCurrent.layoutObjects[ThemeLayoutId_MsgBoxSeparator];
            MessageBox currMsgBox = menuGetCurrentMsgBox();
            int start_x = 1280 / 2 - currMsgBox.width / 2;
            int start_y = (720 / 2 - currMsgBox.height / 2) + currMsgBox.height;
            int end_x = start_x + currMsgBox.width;
            int end_y = start_y;
            start_y+= layoutobj->posStart[1];

            if (x1 > start_x && x1 < end_x && y1 > start_y && y1 < end_y && touchInfo.isTap) {
                // A confirmation box has its confirm choice on the left half.
                if (menuIsConfirmMsgBoxOpen() && x1 < start_x + (int)currMsgBox.width / 2)
                    menuConfirmMsgBox();
                else
                    menuCloseMsgBox();
            }
        } else if (touchInfo.isTap && !netloader_active) {
            // App Icons
            if (y1 > layoutobj->posStart[1] && y1 < layoutobj->posStart[1]+layoutobj->size[1]) {
                handleTappingOnApp(menu, touchInfo.prevTouch.x);
            }
            // Bottom Buttons
            else {
                // Back Button
                if (checkInsideTextLayoutObject(ThemeLayoutId_ButtonB, x1, y1) || checkInsideTextLayoutObject(ThemeLayoutId_ButtonBText, x1, y1)) {
                    launchMenuBackTask();
                }
                // Open/Launch Button
                else if (menu->nEntries != 0 && (checkInsideTextLayoutObject(ThemeLayoutId_ButtonA, x1, y1) || checkInsideTextLayoutObject(ThemeLayoutId_ButtonAText, x1, y1))) {
                    handleTappingOnOpenLaunch(menu);
                }
                // Star Button
                else if (menu->nEntries != 0) {
                    int i;
                    menuEntry_s* me;
                    for (i = 0, me = menu->firstEntry; i != menu->curEntry; i ++, me = me->next);
                    if (me->type != ENTRY_TYPE_THEME && (checkInsideTextLayoutObject(ThemeLayoutId_ButtonX, x1, y1) || checkInsideTextLayoutObject(ThemeLayoutId_ButtonXText, x1, y1))) {
                        menuHandleXButton();
                    }
                }
            }
        }
        // Vertical Swipe
        else if (abs(x1 - x2) < VERTICAL_SWIPE_HORIZONTAL_PLAY && distance(x1, y1, x2, y2) > VERTICAL_SWIPE_MINIMUM_DISTANCE) {
            // Swipe up to go back
            if (y1 - y2 > 0) {
                launchMenuBackTask();
            }
            // Swipe down to go into netloader
            else if (y1 - y2 < 0) {
                launchMenuNetloaderTask();
            }
        }
        // Horizontal Swipe
        else if (y1 < layoutobj->posStart[1] && y2 < layoutobj->posStart[1]) {
            if (abs(y1 - y2) < HORIZONTAL_SWIPE_VERTICAL_PLAY && distance(x1, y1, x2, y2) > HORIZONTAL_SWIPE_MINIMUM_DISTANCE) {
                // Swipe left to go into theme-menu
                if (x1 - x2 > 0) {
                    themeMenuStartup();
                }
            }
        }

        touchInfo.gestureInProgress = false;
    }
}

void handleMouse(menu_s* menu) {
    bool connected = false;
    bool left = false;
    bool right = false;
    bool pointerMoved = false;
    int x = s_mouseX;
    int y = s_mouseY;

#ifdef __SWITCH2__
    if (s_pointerReady) {
        padPointerUpdate(&s_pointer, &g_pad);
        connected = s_pointer.active || s_pointerReady;
        if (s_pointer.active) {
            /* Softer pointer follow than the raw absolute position. */
            x = s_mouseX + (int)((float)(s_pointer.x - s_mouseX) * 0.45f);
            y = s_mouseY + (int)((float)(s_pointer.y - s_mouseY) * 0.45f);
            pointerMoved = s_pointer.delta_x != 0 || s_pointer.delta_y != 0;
            left = (s_pointer.buttons_cur & HidMouseButton_Left) != 0;
            right = (s_pointer.buttons_cur & HidMouseButton_Right) != 0;
        }
    }
#endif

    if (!s_pointerReady) {
        HidMouseState states[8];
        const size_t count = hidGetMouseStates(states, 8);
        for (size_t i = 0; i < count; i++) {
            const HidMouseState* state = &states[i];
            connected = true;
            if (state->x >= 0 && state->x < 1280 && state->y >= 0 && state->y < 720 &&
                (state->x || state->y)) {
                x = state->x;
                y = state->y;
            } else {
                x += state->delta_x;
                y += state->delta_y;
            }
            pointerMoved |= state->delta_x != 0 || state->delta_y != 0;
            left |= (state->buttons & HidMouseButton_Left) != 0;
            right |= (state->buttons & HidMouseButton_Right) != 0;
        }
    }

    if (!connected) {
        s_mouseConnected = false;
        s_mouseHover = false;
        s_mouseLeft = s_mouseRight = false;
        return;
    }
    s_mouseConnected = true;
    s_mouseX = x;
    s_mouseY = y;
    if (s_mouseX < 0) s_mouseX = 0;
    if (s_mouseX > 1279) s_mouseX = 1279;
    if (s_mouseY < 0) s_mouseY = 0;
    if (s_mouseY > 719) s_mouseY = 719;

#ifdef __SWITCH2__
    if (pointerMoved)
        s_mouseVisible = true;
    if (s_mouseVisible && (pointerMoved || left || right))
        switch2HandlePointer(s_mouseX, s_mouseY,
                             left && !s_mouseLeft, right && !s_mouseRight,
                             pointerMoved);
    s_mouseLeft = left;
    s_mouseRight = right;
    return;
#endif

    /* Edge auto-scroll: while the cursor is visible and parked at a screen
     * edge, walk the selection so the list scrolls in that direction. */
    bool atScrollEdge = false;
    if (s_mouseVisible && !menuIsSettingsMenu() &&
        !menuIsMsgBoxOpen() && !menuIsConfirmMsgBoxOpen() && menu->nEntries > 0) {
        static u64 edgeNextTick;
        int edge = 0;
        if (s_mouseX < 32)
            edge = -1;
        else if (s_mouseX > 1279 - 32)
            edge = 1;
        atScrollEdge = edge != 0;
        const u64 now = armGetSystemTick();
        if (edge && now >= edgeNextTick) {
            menuScrollPage(menu, edge);
            edgeNextTick = now + armNsToTicks(300000000ULL);
        }
        if (!edge)
            edgeNextTick = 0;
    }

    ThemeLayoutObject* list = &themeCurrent.layoutObjects[ThemeLayoutId_MenuList];
    const bool over_list = s_mouseY > list->posStart[1] &&
                           s_mouseY < list->posStart[1] + list->size[1];
    s_mouseHover = over_list ||
                   checkInsideTextLayoutObject(ThemeLayoutId_ButtonA, s_mouseX, s_mouseY) ||
                   checkInsideTextLayoutObject(ThemeLayoutId_ButtonAText, s_mouseX, s_mouseY) ||
                   checkInsideTextLayoutObject(ThemeLayoutId_ButtonB, s_mouseX, s_mouseY) ||
                   checkInsideTextLayoutObject(ThemeLayoutId_ButtonBText, s_mouseX, s_mouseY) ||
                   checkInsideTextLayoutObject(ThemeLayoutId_ButtonX, s_mouseX, s_mouseY) ||
                   checkInsideTextLayoutObject(ThemeLayoutId_ButtonXText, s_mouseX, s_mouseY);
    const HidAnalogStickState stick_l = padGetStickPos(&g_pad, 0);
    const HidAnalogStickState stick_r = padGetStickPos(&g_pad, 1);
    const bool stick_scrolling = stick_l.x > 10000 || stick_l.x < -10000 ||
                                 stick_r.x > 10000 || stick_r.x < -10000;
    if (over_list && pointerMoved && !atScrollEdge && !stick_scrolling &&
        !menuIsSettingsMenu() && menu->nEntries > 0) {
        int i = 0;
        for (menuEntry_s* me = menu->firstEntry; me; me = me->next, i++) {
            int start_x = list->posStart[0] + i * list->posEnd[0];
            if (s_mouseX >= start_x + menu->xPos &&
                s_mouseX <= start_x + menu->xPos + (int)list->size[0]) {
                menu->curEntry = i;
                menu->holdXPos = true;
                break;
            }
        }
    }

    if (pointerMoved || left || right)
        s_mouseVisible = true;

    const bool controllerGrace =
        (armGetSystemTick() - s_controllerInputTick) < armNsToTicks(MOUSE_CLICK_GRACE_NS);
    if (!controllerGrace)
    if (left && !s_mouseLeft && !menuIsMsgBoxOpen()) {
        ThemeLayoutObject* list = &themeCurrent.layoutObjects[ThemeLayoutId_MenuList];
        if (s_mouseY > list->posStart[1] && s_mouseY < list->posStart[1] + list->size[1]) {
            if (menuIsSettingsMenu())
                settingsMenuToggleCurrent();
            else
                handleTappingOnApp(menu, s_mouseX);
        } else if (checkInsideTextLayoutObject(ThemeLayoutId_ButtonA, s_mouseX, s_mouseY) ||
                   checkInsideTextLayoutObject(ThemeLayoutId_ButtonAText, s_mouseX, s_mouseY)) {
            handleTappingOnOpenLaunch(menu);
        } else if (checkInsideTextLayoutObject(ThemeLayoutId_ButtonB, s_mouseX, s_mouseY) ||
                   checkInsideTextLayoutObject(ThemeLayoutId_ButtonBText, s_mouseX, s_mouseY)) {
            launchMenuBackTask();
        }
    }
    if (!controllerGrace && right && !s_mouseRight && !menuIsMsgBoxOpen())
        launchMenuBackTask();
    s_mouseLeft = left;
    s_mouseRight = right;
}

bool mouseIsActive(void) {
    return s_mouseVisible;
}

void mouseControllerInput(void) {
    s_mouseVisible = false;
    s_controllerInputTick = armGetSystemTick();
}

void mouseExit(void) {
#ifdef __SWITCH2__
    if (s_pointerReady) {
        padPointerExit(&s_pointer);
        s_pointerReady = false;
    }
#endif
}

void drawMouseCursor(void) {
#ifdef __SWITCH2__
    if (!s_mouseVisible)
        return;
    assetsDataEntry* cursor = NULL;
    AssetId cursor_id = s_mouseLeft ? AssetId_cursor_click
                       : s_mouseHover ? AssetId_cursor_hover
                                      : AssetId_cursor;
    assetsGetData(cursor_id, &cursor);
    if (cursor && cursor->buffer) {
        enum RenderImageFormat format = cursor->imageMode == IMAGE_MODE_RGBA32
                                            ? RenderImageFormat_Rgba32
                                            : RenderImageFormat_Rgb24;
        const int cursor_size = 48;
        const int hotspot_x = cursor_id == AssetId_cursor_hover ? 12 : 8;
        const int hotspot_y = cursor_id == AssetId_cursor_hover ? 8 : 8;
        const int cursor_x = s_mouseX - hotspot_x;
        const int cursor_y = s_mouseY - hotspot_y;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dy)
                    continue;
                renderImageScaled(cursor->buffer, (int)cursor->imageSize[0],
                                  (int)cursor->imageSize[1],
                                  cursor_x + dx, cursor_y + dy,
                                  cursor_size, cursor_size, format, 0xff000000u);
            }
        }
        renderImageScaled(cursor->buffer, (int)cursor->imageSize[0],
                          (int)cursor->imageSize[1],
                          cursor_x, cursor_y,
                          cursor_size, cursor_size, format, 0xffffffffu);
        return;
    }

    const uint32_t white = 0xffffffffu;
    const uint32_t black = 0xff000000u;
    for (int row = 0; row < 16; row++) {
        int width = row < 9 ? (row / 2) + 2 : 6;
        if (row > 11)
            width = 3;
        renderRect(s_mouseX - 1, s_mouseY + row, s_mouseX + width + 1,
                   s_mouseY + row + 1, black);
        renderRect(s_mouseX, s_mouseY + row, s_mouseX + width,
                   s_mouseY + row + 1, white);
    }
#endif
}
