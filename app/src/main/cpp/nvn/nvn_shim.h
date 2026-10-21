// M3 — NVN→Vulkan shim: exported C surface.
//
// The game imports `nvnBootstrapLoader` (SDK symbol) and resolves everything
// else through `nvnDeviceGetProcAddress`. This header declares the entry
// points the shim registers; nvn_shim.cpp implements them on Vulkan.
//
// The M3 probe draws the test triangle THROUGH the registry-resolved NVN
// entry points (mng::nvn::probe_render_triangle) — the same path the guest
// will use after M2 module loading feeds it real data.
#pragma once

#include "nvn/nvn_types.h"

extern "C" {

// SDK interception point — the game's import resolves here.
const void* nvnBootstrapLoader(const char* name);

} // extern "C"

namespace mng::nvn {

// Boot the shim context off an ANativeWindow (arrives through
// NVNwindowBuilderSetNativeWindow when the game creates its window).
// Returns 0 on success; negative codes are reported by the M3 probe screen.
int shim_attach_native_window(void* anative_window, int32_t* out_w, int32_t* out_h);

// Counters and object census for the probe UI.
uint32_t shim_texture_count();
uint32_t shim_shader_count();
uint32_t shim_registered_entries();

// M3 acceptance probe: resolves the NVN entry points by NAME from the shim's
// own registry and draws the RGB-gradient triangle on the attached window
// through them. Returns 0 on success, negative on failure. No game data.
int probe_render_triangle(void* anative_window, int frames);

} // namespace mng::nvn
