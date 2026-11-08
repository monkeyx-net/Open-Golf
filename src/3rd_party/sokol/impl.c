#define SOKOL_WIN32_NO_GL_LOADER
#ifdef GOLF_PORTMASTER
// GLES3 on Linux: sokol_gfx only knows the desktop GL headers there.
#define SOKOL_EXTERNAL_GL_LOADER
#include <GLES3/gl3.h>
#endif
#define SOKOL_IMPL
#ifndef GOLF_PORTMASTER
#include "sokol/sokol_audio.h"
#endif
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_time.h"

#ifdef GOLF_PORTMASTER
// The window, context and events come from SDL2 (src/golf/main_sdl.c), which
// provides the few sapp_* functions the game uses. sokol_app.h is only
// included for its types.
#undef SOKOL_IMPL
#include "sokol/sokol_app.h"
#else
#define SOKOL_WIN32_FORCE_MAIN
#include "sokol/sokol_app.h"
#include "sokol/sokol_glue.h"
#endif

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui/cimgui.h"

#define SOKOL_IMGUI_IMPL
#include "sokol/sokol_imgui.h"
