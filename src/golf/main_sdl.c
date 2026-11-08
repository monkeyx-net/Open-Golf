// PortMaster platform layer: SDL2 window with a GLES 3.0 context instead of
// sokol_app (X11/GLX). Mouse events are passed through unchanged; a gamepad
// drives a virtual mouse cursor:
//   left stick / D-pad  move the cursor
//   A                   click (in game: grab the ball, pull back, release to shoot)
//   B                   cancel the shot / back
//   L1/R1, right stick  rotate the camera (right stick up/down scrolls lists)
//   Start               pause / resume
#include <math.h>
#include <stdlib.h>

#include <SDL.h>

#include "golf/main.c"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui/cimgui.h"

static int win_w, win_h;
static float cursor_x, cursor_y;
static uint64_t frame_count;
static bool using_pad;

int sapp_width(void) { return win_w; }
int sapp_height(void) { return win_h; }
float sapp_dpi_scale(void) { return 1.0f; }
void sapp_consume_event(void) {}
bool sapp_keyboard_shown(void) { return false; }
void sapp_show_keyboard(bool show) { GOLF_UNUSED(show); }
void sapp_set_clipboard_string(const char *str) { GOLF_UNUSED(str); }
const char *sapp_get_clipboard_string(void) { return ""; }

sg_context_desc sapp_sgcontext(void) {
    return (sg_context_desc) {
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
        .sample_count = 1,
    };
}

static void send_event(sapp_event_type type, sapp_mousebutton button, float scroll_y) {
    sapp_event ev = {
        .frame_count = frame_count,
        .type = type,
        .mouse_button = button,
        .mouse_x = cursor_x,
        .mouse_y = cursor_y,
        .scroll_y = scroll_y,
        .window_width = win_w,
        .window_height = win_h,
        .framebuffer_width = win_w,
        .framebuffer_height = win_h,
    };
    event(&ev);
}

static float stick(SDL_GameController *pad, SDL_GameControllerAxis axis) {
    float v = SDL_GameControllerGetAxis(pad, axis) / 32767.0f;
    if (fabsf(v) < 0.2f) return 0;
    v = (v - copysignf(0.2f, v)) / 0.8f;
    return v * fabsf(v);
}

static bool in_game_state(golf_game_state_t state) {
    return golf_get()->state == GOLF_STATE_IN_GAME && golf_game_get()->state == state;
}

static void move_cursor_to_ball(void) {
    vec2 p = golf_graphics_world_to_screen(golf_game_get()->ball.pos);
    cursor_x = p.x;
    cursor_y = p.y;
    send_event(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 0);
}

static void pad_button(SDL_GameControllerButton button, bool down) {
    using_pad = true;
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A:
            if (down && in_game_state(GOLF_GAME_STATE_WAITING_FOR_AIM)) {
                move_cursor_to_ball();
            }
            send_event(down ? SAPP_EVENTTYPE_MOUSE_DOWN : SAPP_EVENTTYPE_MOUSE_UP, SAPP_MOUSEBUTTON_LEFT, 0);
            break;
        case SDL_CONTROLLER_BUTTON_B:
            if (!down) break;
            if (in_game_state(GOLF_GAME_STATE_AIMING)) {
                // Zero power: releasing on the ball goes back to waiting for aim.
                move_cursor_to_ball();
                send_event(SAPP_EVENTTYPE_MOUSE_UP, SAPP_MOUSEBUTTON_LEFT, 0);
            }
            else if (in_game_state(GOLF_GAME_STATE_PAUSED)) {
                golf_game_resume();
            }
            else if (golf_get()->state == GOLF_STATE_MAIN_MENU) {
                golf_ui_get()->main_menu.is_level_select_open = false;
            }
            break;
        case SDL_CONTROLLER_BUTTON_START:
            if (!down) break;
            if (in_game_state(GOLF_GAME_STATE_WAITING_FOR_AIM) || in_game_state(GOLF_GAME_STATE_WATCHING_BALL)) {
                golf_game_pause();
            }
            else if (in_game_state(GOLF_GAME_STATE_PAUSED)) {
                golf_game_resume();
            }
            break;
        default:
            break;
    }
}

static void pad_update(SDL_GameController *pad, float dt) {
    float dx = stick(pad, SDL_CONTROLLER_AXIS_LEFTX);
    float dy = stick(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) dx -= 0.5f;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) dx += 0.5f;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) dy -= 0.5f;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) dy += 0.5f;
    if (dx != 0 || dy != 0) {
        using_pad = true;
        // Slower while aiming, for finer control of the shot power.
        float speed = (in_game_state(GOLF_GAME_STATE_AIMING) ? 0.45f : 0.9f) * win_h;
        cursor_x = fminf(fmaxf(cursor_x + dx * speed * dt, 0), (float)(win_w - 1));
        cursor_y = fminf(fmaxf(cursor_y + dy * speed * dt, 0), (float)(win_h - 1));
        send_event(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 0);
    }

    float rotate = stick(pad, SDL_CONTROLLER_AXIS_RIGHTX);
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) rotate -= 1;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) rotate += 1;
    if (rotate != 0 && (in_game_state(GOLF_GAME_STATE_WAITING_FOR_AIM) ||
                in_game_state(GOLF_GAME_STATE_AIMING) || in_game_state(GOLF_GAME_STATE_WATCHING_BALL))) {
        golf_game_t *game = golf_game_get();
        game->cam.auto_rotate = false;
        game->cam.angle_velocity = 0;
        game->cam.angle += 1.5f * rotate * dt;
    }

    float scroll = stick(pad, SDL_CONTROLLER_AXIS_RIGHTY);
    if (scroll != 0) {
        send_event(SAPP_EVENTTYPE_MOUSE_SCROLL, SAPP_MOUSEBUTTON_INVALID, -20.0f * scroll * dt);
    }

    // Draw the cursor on menus only; in game the aim line shows it.
    bool playing = golf_get()->state == GOLF_STATE_IN_GAME &&
        !in_game_state(GOLF_GAME_STATE_PAUSED) && !in_game_state(GOLF_GAME_STATE_FINISHED);
    igGetIO()->MouseDrawCursor = using_pad && !playing;
}

static sapp_mousebutton mouse_button(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return SAPP_MOUSEBUTTON_LEFT;
        case SDL_BUTTON_RIGHT: return SAPP_MOUSEBUTTON_RIGHT;
        case SDL_BUTTON_MIDDLE: return SAPP_MOUSEBUTTON_MIDDLE;
        default: return SAPP_MOUSEBUTTON_INVALID;
    }
}

int main(int argc, char *argv[]) {
    GOLF_UNUSED(argc);
    GOLF_UNUSED(argv);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // GOLF_WINDOW=WxH opens a window of that size (desktop testing).
    int w = 0, h = 0;
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP;
    const char *window_env = getenv("GOLF_WINDOW");
    if (window_env && sscanf(window_env, "%dx%d", &w, &h) == 2) {
        flags = SDL_WINDOW_OPENGL;
    }
    SDL_Window *window = SDL_CreateWindow("Open Golf", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!window || !SDL_GL_CreateContext(window)) {
        fprintf(stderr, "Unable to create a GLES 3.0 window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    if (!window_env) {
        SDL_ShowCursor(SDL_DISABLE);
    }
    SDL_GL_GetDrawableSize(window, &win_w, &win_h);
    cursor_x = 0.5f * win_w;
    cursor_y = 0.5f * win_h;

    golf_alloc_init();
    golf_log_init();
    init();
    igGetStyle()->MouseCursorScale = win_h / 300.0f;

    SDL_GameController *pad = NULL;
    uint64_t last_time = stm_now();
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_CONTROLLERDEVICEADDED:
                    if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which);
                    break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                        SDL_GameControllerClose(pad);
                        pad = NULL;
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_CONTROLLERBUTTONUP:
                    pad_button((SDL_GameControllerButton)e.cbutton.button, e.type == SDL_CONTROLLERBUTTONDOWN);
                    break;
                case SDL_MOUSEMOTION:
                    cursor_x = (float)e.motion.x;
                    cursor_y = (float)e.motion.y;
                    using_pad = false;
                    send_event(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 0);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    send_event(e.type == SDL_MOUSEBUTTONDOWN ? SAPP_EVENTTYPE_MOUSE_DOWN : SAPP_EVENTTYPE_MOUSE_UP,
                            mouse_button(e.button.button), 0);
                    break;
                case SDL_MOUSEWHEEL:
                    send_event(SAPP_EVENTTYPE_MOUSE_SCROLL, SAPP_MOUSEBUTTON_INVALID, (float)e.wheel.y);
                    break;
            }
        }

        float dt = (float)stm_sec(stm_laptime(&last_time));
        SDL_GL_GetDrawableSize(window, &win_w, &win_h);
        if (pad) {
            pad_update(pad, dt);
        }
        frame();
        SDL_GL_SwapWindow(window);
        frame_count++;
    }

    cleanup();
    SDL_Quit();
    return 0;
}
