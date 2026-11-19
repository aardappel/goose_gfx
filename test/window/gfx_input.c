/* The real-window half of gfx_mouse_capture.goose. A hidden SDL window
   exercises the platform setters without taking desktop focus or moving
   the user's pointer. Run with run_input_test.py. */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include "../../src/gfx/gfx_api.h"

#include <stdio.h>

static int failures;

static void check(bool ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s (%s)\n", what, SDL_GetError());
        failures++;
    }
}

static Uint32 wake_event;

static Uint32 SDLCALL push_wake(void *userdata, SDL_TimerID timer, Uint32 interval) {
    (void)userdata;
    (void)timer;
    (void)interval;
    SDL_Event e;
    SDL_zero(e);
    e.type = wake_event;
    SDL_PushEvent(&e);
    return 0;
}

static SDL_Joystick *virtual_pad;

/* frame()'s next call, timed in milliseconds. */
static Uint64 timed_frame(void) {
    Uint64 t = SDL_GetTicks();
    gs_gfx_frame();
    return SDL_GetTicks() - t;
}

/* Whether a frame() waits its full second within 5 s; until one does, the
   window may still be showing. */
static bool idles(void) {
    Uint64 start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 5000)
        if (timed_frame() >= 900) return true;
    return false;
}

/* WAIT_WHEN_IDLE with SDL's virtual gamepad: a press let go before frame()
   counts, and with a pad open the wait is short, so a press or the slightest
   stick move shows within a frame or two. */
static void gamepad_checks(void) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    virtual_pad = id ? SDL_OpenJoystick(id) : NULL;
    check(virtual_pad != NULL, "attach a virtual gamepad");
    if (!virtual_pad) return;
    /* A real pad connected may hold a slot already: the virtual one's is the
       one that fills. */
    bool before[GS_GFX_MAX_GAMEPADS];
    for (int i = 0; i < GS_GFX_MAX_GAMEPADS; i++) before[i] = gs_gfx_gamepad_connected(i);
    gs_gfx_frame();
    int slot = -1;
    for (int i = 0; i < GS_GFX_MAX_GAMEPADS; i++)
        if (slot < 0 && !before[i] && gs_gfx_gamepad_connected(i)) slot = i;
    check(slot >= 0, "the virtual gamepad fills a slot");
    if (slot < 0) return;

    SDL_SetJoystickVirtualButton(virtual_pad, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_UpdateJoysticks();
    SDL_SetJoystickVirtualButton(virtual_pad, SDL_GAMEPAD_BUTTON_SOUTH, false);
    SDL_UpdateJoysticks();
    gs_gfx_frame();
    check(gs_gfx_gamepad_pressed(slot, SDL_GAMEPAD_BUTTON_SOUTH) && !gs_gfx_gamepad_down(slot, SDL_GAMEPAD_BUTTON_SOUTH),
          "a press let go before frame() still counts");

    /* Until the present's retries run out, the hidden window may not wait. */
    Uint64 idle = 0;
    for (int i = 0; i < 12; i++) idle = timed_frame();
    check(idle >= 10 && idle < 100, "with a pad open, a frame that drew nothing waits briefly");
    SDL_SetJoystickVirtualAxis(virtual_pad, SDL_GAMEPAD_AXIS_LEFTX, 1500);
    timed_frame();
    check(gs_gfx_gamepad_axis(slot, SDL_GAMEPAD_AXIS_LEFTX) > 0.0f,
          "a slight stick move shows, with no deadzone");
    SDL_SetJoystickVirtualButton(virtual_pad, SDL_GAMEPAD_BUTTON_EAST, true);
    timed_frame();
    check(gs_gfx_gamepad_pressed(slot, SDL_GAMEPAD_BUTTON_EAST), "a press shows at the next frame");
    SDL_SetJoystickVirtualButton(virtual_pad, SDL_GAMEPAD_BUTTON_EAST, false);
    printf("gamepad wait: %llu ms\n", (unsigned long long)idle);
    SDL_CloseJoystick(virtual_pad);
    SDL_DetachVirtualJoystick(id);
    gs_gfx_frame();
    check(!gs_gfx_gamepad_connected(slot), "the virtual gamepad leaves its slot");
}

/* WAIT_WHEN_IDLE: a frame that drew nothing makes the next frame() wait for an
   event, up to a second, which delta_time() leaves out; one that drew doesn't. */
static void wait_checks(void) {
    gs_gfx_bytes title = { (uint8_t *)"Goose wait regression", 21 };
    check(gs_gfx_open(title, 64, 48, GS_GFX_WINDOW_HIDDEN | GS_GFX_WAIT_WHEN_IDLE),
          "open WAIT_WHEN_IDLE");
    gs_gfx_frame();
    check(idles(), "a frame that drew nothing waits");
    check(gs_gfx_delta_time() < 0.5, "delta_time() leaves out the wait");
    gs_gfx_pass_desc pass;
    SDL_zero(pass);
    pass.color[0] = gs_gfx_screen();
    pass.clear_color = 1;
    check(gs_gfx_begin_pass(&pass), "clear the screen");
    gs_gfx_end_pass();
    check(timed_frame() < 500, "a frame that drew doesn't wait");
    check(idles(), "the frame after it waits again");
    wake_event = SDL_RegisterEvents(1);
    SDL_AddTimer(200, push_wake, NULL);
    Uint64 woken = timed_frame();
    check(woken >= 150 && woken < 600, "an event ends the wait");
    printf("wait: an event at 200 ms ended it at %llu ms\n", (unsigned long long)woken);
    /* The wait reads a pad only while the window has focus. */
    gs_gfx_inject_focus(true);
    gs_gfx_frame();
    gamepad_checks();
    gs_gfx_close();
}

/* WINDOW_ONLY: a window and no device, its native handle for another renderer,
   and with WAIT_WHEN_IDLE the wait following what the renderer says it showed. */
static void window_only_checks(void) {
    gs_gfx_bytes title = { (uint8_t *)"Goose window only", 17 };
    check(gs_gfx_open(title, 64, 48, GS_GFX_WINDOW_HIDDEN | GS_GFX_WINDOW_ONLY | GS_GFX_WAIT_WHEN_IDLE),
          "open WINDOW_ONLY");
    check(gs_gfx_native_window() != 0, "WINDOW_ONLY gives the native window");
    int n = 0;
    SDL_Window **windows = SDL_GetWindows(&n);
    SDL_Window *window = n == 1 ? windows[0] : NULL;
    SDL_free(windows);
    check(window != NULL, "WINDOW_ONLY opens one window");
    int pw = 0, ph = 0;
    if (window) SDL_GetWindowSizeInPixels(window, &pw, &ph);
    gs_gfx_int2 screen;
    gs_gfx_screen_size(&screen);
    check(screen.x == pw && screen.y == ph, "the screen size is the window's in pixels");
    check(gs_gfx_set_cursor(GS_GFX_CURSOR_POINTER), "WINDOW_ONLY sets the cursor");
    gs_gfx_frame();
    check(idles(), "a frame the renderer showed nothing in waits");
    check(!gs_gfx_needs_present(), "nothing to present after the wait");
    gs_gfx_presented(1);
    check(timed_frame() < 500, "a frame the renderer showed doesn't wait");
    gs_gfx_bytes key = { (uint8_t *)"A", 1 };
    gs_gfx_inject_focus(true);
    gs_gfx_inject_key(key, true);
    gs_gfx_frame();
    check(gs_gfx_focused() && gs_gfx_key_pressed(key), "input reaches WINDOW_ONLY");
    gs_gfx_close();
    check(gs_gfx_native_window() == 0, "no native window after close");
}

int main(void) {
    /* Keep SDL alive after gfx closes, so cursor cleanup can be inspected. */
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    /* The hidden window never has the desktop's focus, without which SDL drops
       a pad's input. */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    check(gs_gfx_pixel_density() == 1 && gs_gfx_display_scale() == 1,
          "scales are 1 before any window");
    gs_gfx_bytes title = { (uint8_t *)"Goose input regression", 22 };
    if (!gs_gfx_open(title, 64, 48, GS_GFX_WINDOW_HIDDEN)) {
        uint8_t why[1024] = { 0 };
        gs_gfx_bytes out = { why, sizeof why - 1 };
        gs_gfx_error(out);
        fprintf(stderr, "gfx open: %s\n", why);
        SDL_Quit();
        return 1;
    }
    int n = 0;
    SDL_Window **windows = SDL_GetWindows(&n);
    if (n != 1) {
        fprintf(stderr, "expected one actual SDL window, found %d\n", n);
        SDL_free(windows);
        gs_gfx_close();
        SDL_Quit();
        return 1;
    }
    SDL_Window *window = windows[0];
    SDL_free(windows);
    SDL_WindowID id = SDL_GetWindowID(window);
    gs_gfx_frame();
    check(!gs_gfx_focused(), "a hidden window starts without focus");
    bool waited = false;
    for (int i = 0; i < 4; i++) waited = waited || timed_frame() >= 500;
    check(!waited, "without WAIT_WHEN_IDLE frame() doesn't wait");

    int window_w = 0, window_h = 0;
    SDL_GetWindowSize(window, &window_w, &window_h);
    gs_gfx_int2 screen;
    gs_gfx_screen_size(&screen);
    check(gs_gfx_pixel_density() == (float)screen.x / window_w,
          "pixel density is the screen's pixels per window coordinate");
    check(gs_gfx_display_scale() == SDL_GetWindowDisplayScale(window) &&
          gs_gfx_display_scale() > 0, "display scale is SDL's");
    check(window_w == 64 && window_h == 48, "the scales do not size the window");

    check(!gs_gfx_set_mouse_relative(true), "capture without focus is rejected");

    check(gs_gfx_set_cursor(GS_GFX_CURSOR_POINTER), "select the pointer cursor");
    SDL_Cursor *pointer = SDL_GetCursor();
    check(pointer && pointer != SDL_GetDefaultCursor(), "system cursor reaches SDL");
    check(gs_gfx_set_cursor(GS_GFX_CURSOR_TEXT), "select the text cursor");
    check(SDL_GetCursor() != pointer, "changing shape changes the SDL cursor");
    check(gs_gfx_set_cursor(GS_GFX_CURSOR_POINTER), "select the pointer again");
    check(SDL_GetCursor() == pointer, "system cursors are reused");
    check(!gs_gfx_set_cursor(-1), "negative cursor shape is rejected");
    check(!gs_gfx_set_cursor(INT64_MAX), "oversized cursor shape is rejected");
    check(!gs_gfx_set_cursor(GS_GFX_CURSOR_W_RESIZE + 1), "unknown cursor shape is rejected");
    check(SDL_GetCursor() == pointer, "invalid shapes preserve the current cursor");
    for (int64_t shape = GS_GFX_CURSOR_DEFAULT; shape <= GS_GFX_CURSOR_W_RESIZE; shape++)
        check(gs_gfx_set_cursor(shape), "select each supported system cursor");

    /* Injected focus changes gfx's state, never actual desktop focus. */
    gs_gfx_inject_focus(true);
    gs_gfx_frame();
    check(gs_gfx_set_cursor_visible(false), "hide the cursor");
    check(!SDL_CursorVisible(), "hiding reaches SDL");
    check(gs_gfx_set_cursor(GS_GFX_CURSOR_POINTER), "change shape while hidden");
    check(!SDL_CursorVisible(), "changing shape keeps the cursor hidden");
    check(gs_gfx_set_mouse_relative(true), "enable relative mode");
    check(SDL_GetWindowRelativeMouseMode(window), "relative mode reaches the SDL window");
    check(gs_gfx_set_cursor_visible(true), "remember visible preference in relative mode");
    check(gs_gfx_set_cursor(GS_GFX_CURSOR_TEXT), "change shape in relative mode");
    check(SDL_GetWindowRelativeMouseMode(window), "changing shape keeps relative mode");
    check(!SDL_CursorVisible(), "relative mode still hides the SDL cursor");
    check(gs_gfx_set_mouse_relative(false), "disable relative mode");
    check(!SDL_GetWindowRelativeMouseMode(window), "relative mode disabled in SDL");
    check(SDL_CursorVisible(), "visible preference restored in SDL");
    check(gs_gfx_set_mouse_relative(true), "capture again");

    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.windowID = id + 1;
    SDL_PushEvent(&event);
    gs_gfx_frame();
    check(gs_gfx_focused() && gs_gfx_mouse_relative(), "another window's focus is ignored");

    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = id;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = true;
    event.button.clicks = 2;
    SDL_PushEvent(&event);
    gs_gfx_frame();
    gs_gfx_inject_focus(false);
    gs_gfx_frame();
    check(!gs_gfx_focused(), "focus loss reaches the state query");
    check(!SDL_GetWindowRelativeMouseMode(window), "focus loss releases SDL relative mode");
    check(SDL_CursorVisible(), "focus loss shows the SDL cursor");
    check(gs_gfx_mouse_released(GS_GFX_MOUSE_LEFT), "focus loss releases a held button");
    gs_gfx_event events[8];
    gs_gfx_event_slice out = { events, 8 };
    int64_t count = gs_gfx_events(out);
    check(count == 2 && events[0].kind == GS_GFX_EVENT_MOUSE_BUTTON &&
          !events[0].down && events[0].clicks == 2 &&
          events[1].kind == GS_GFX_EVENT_FOCUS_LOST,
          "double-click release precedes the focus notification");

    gs_gfx_inject_focus(true);
    gs_gfx_frame();
    check(!SDL_GetWindowRelativeMouseMode(window), "focus gain does not recapture");
    check(gs_gfx_set_mouse_relative(true), "explicitly recapture");
    gs_gfx_close();
    check(SDL_CursorVisible(), "close restores cursor visibility");
    check(SDL_GetCursor() == SDL_GetDefaultCursor(), "close restores the default cursor");

    check(gs_gfx_pixel_density() == 1 && gs_gfx_display_scale() == 1,
          "scales are 1 after close");
    wait_checks();
    window_only_checks();
    check(gs_gfx_open_headless(16, 16, 0), "reopen headless after releasing cursors");
    check(gs_gfx_pixel_density() == 1 && gs_gfx_display_scale() == 1,
          "scales are 1 headless");
    SDL_Cursor *before = SDL_GetCursor();
    for (int64_t shape = GS_GFX_CURSOR_DEFAULT; shape <= GS_GFX_CURSOR_W_RESIZE; shape++)
        check(gs_gfx_set_cursor(shape), "headless accepts each supported shape");
    check(!gs_gfx_set_cursor(-1), "headless rejects invalid shapes");
    check(SDL_GetCursor() == before, "headless leaves the SDL cursor unchanged");
    gs_gfx_close();
    check(gs_gfx_misuse_count() == 0, "no gfx misuse");
    SDL_Quit();
    if (!failures) puts("gfx window input passed");
    return failures ? 1 : 0;
}
