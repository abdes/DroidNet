// Generated file - do not edit.
// Source: F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Core/Bindless/BindingSlots.yaml
// Generated: 2025-08-17 15:34:12

#ifndef OXYGEN_CORE_BINDLESS_BINDINGSLOTS_H
#define OXYGEN_CORE_BINDLESS_BINDINGSLOTS_H

#include <cstdint>

namespace oxygen {
namespace engine {
namespace binding {

// Invalid sentinel
static constexpr uint32_t kInvalidBindlessIndex = 0xffffffffu;

// Scene constants CBV (b1), heap index 0; holds bindless indices table

static constexpr uint32_t Scene_DomainBase = 0u;
static constexpr uint32_t Scene_Capacity = 1u;

// Unified SRV table base
static constexpr uint32_t GlobalSRV_DomainBase = 1u;
static constexpr uint32_t GlobalSRV_Capacity = 2048u;

static constexpr uint32_t Materials_DomainBase = 1000u;
static constexpr uint32_t Materials_Capacity = 4096u;

static constexpr uint32_t Textures_DomainBase = 5096u;
static constexpr uint32_t Textures_Capacity = 65536u;

static constexpr uint32_t Samplers_DomainBase = 0u;
static constexpr uint32_t Samplers_Capacity = 256u;


} // namespace binding
} // namespace engine
} // namespace oxygen

#endif // OXYGEN_CORE_BINDLESS_BINDINGSLOTS_H
