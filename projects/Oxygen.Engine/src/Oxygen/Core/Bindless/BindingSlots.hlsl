// Generated file - do not edit.
// Source: F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Core/Bindless/BindingSlots.yaml
// Generated: 2025-08-17 15:34:12

#ifndef OXYGEN_BINDING_SLOTS_HLSL
#define OXYGEN_BINDING_SLOTS_HLSL

static const uint K_INVALID_BINDLESS_INDEX = 0xffffffff;

// Scene constants CBV (b1), heap index 0; holds bindless indices table

static const uint SCENE_DOMAIN_BASE = 0;
static const uint SCENE_CAPACITY = 1;

// Unified SRV table base
static const uint GLOBALSRV_DOMAIN_BASE = 1;
static const uint GLOBALSRV_CAPACITY = 2048;

static const uint MATERIALS_DOMAIN_BASE = 1000;
static const uint MATERIALS_CAPACITY = 4096;

static const uint TEXTURES_DOMAIN_BASE = 5096;
static const uint TEXTURES_CAPACITY = 65536;

static const uint SAMPLERS_DOMAIN_BASE = 0;
static const uint SAMPLERS_CAPACITY = 256;


#endif // OXYGEN_BINDING_SLOTS_HLSL
