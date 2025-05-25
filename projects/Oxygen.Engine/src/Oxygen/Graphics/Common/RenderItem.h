// f:\projects\DroidNet\projects\Oxygen.Engine\src\Oxygen\Graphics\Common\RenderItem.h
#pragma once

#include <cstdint> // For uint32_t
#include <vector>

// Forward declarations for backend-specific buffer types if needed later
// namespace oxygen::graphics::d3d12 { class Buffer; }
// namespace oxygen::graphics::vulkan { class Buffer; }

namespace oxygen::graphics {

// Simple vertex structure for our triangle demo
struct Vertex {
    float position[3]; // x, y, z
    float color[3]; // r, g, b (matches HLSL float3)
};

struct RenderItem {
    // For a simple triangle, we might embed the vertices directly
    // or point to a shared mesh resource.
    // Let's start with embedded for simplicity.
    std::vector<Vertex> vertices;
    // std::vector<uint32_t> indices; // Optional, if using indexed drawing

    uint32_t vertex_count = 0;
    // uint32_t index_count = 0; // If using indices
    // uint32_t instance_count = 1;
    // uint32_t base_vertex_location = 0;
    // uint32_t start_index_location = 0;
    // uint32_t base_instance_location = 0;

    // Later, this might include:
    // - Pointers to vertex/index buffers (backend-specific or agnostic wrappers)
    // - Transformation matrix
    // - Material ID / Shader ID
    // - Primitive topology (e.g., triangles, lines)
};

} // namespace oxygen::graphics
