// f:\projects\DroidNet\projects\Oxygen.Engine\src\Oxygen\Graphics\Common\RenderItem.h
#pragma once

#include <cstdint> // For uint32_t
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace oxygen::graphics {

// Forward declarations
class Material;

// Enhanced vertex structure for PBR rendering
struct Vertex {
    float position[3];    // x, y, z (object space)
    float normal[3];      // nx, ny, nz (object space normal)
    float tangent[4];     // tx, ty, tz, handedness (object space tangent + handedness)
    float texcoord[2];    // u, v (texture coordinates)
    float color[3];       // r, g, b (vertex color, for debugging/simple materials)
};

// Legacy simple vertex for backward compatibility
struct SimpleVertex {
    float position[3]; // x, y, z
    float color[3]; // r, g, b (matches HLSL float3)
};

struct RenderItem {
    // === Geometry Data ===
    // For simple cases, we embed vertices directly
    // In production, this would reference shared mesh resources
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices; // Optional, for indexed drawing

    uint32_t vertex_count = 0;
    uint32_t index_count = 0;

    // Drawing parameters
    uint32_t instance_count = 1;
    uint32_t base_vertex_location = 0;
    uint32_t start_index_location = 0;
    uint32_t base_instance_location = 0;

    // === Material and Shading ===
    std::shared_ptr<const Material> material;

    // === Transformation ===
    glm::mat4 world_transform = glm::mat4(1.0f);  // Object to world transformation
    glm::mat4 normal_transform = glm::mat4(1.0f); // For transforming normals (usually inverse transpose of world)

    // === Rendering State ===
    enum class PrimitiveTopology : uint8_t {
        kTriangleList,
        kTriangleStrip,
        kLineList,
        kLineStrip,
        kPointList
    } primitive_topology = PrimitiveTopology::kTriangleList;

    // === Culling and Visibility ===
    bool visible = true;
    bool cast_shadows = true;
    bool receive_shadows = true;

    // Bounding sphere for frustum culling (center + radius)
    glm::vec4 bounding_sphere = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    // === Legacy Support ===
    // For backward compatibility with simple demos
    std::vector<SimpleVertex> simple_vertices;

    // === Utility Methods ===

    //! Check if using indexed drawing
    [[nodiscard]] bool IsIndexed() const { return !indices.empty() && index_count > 0; }

    //! Check if using simple vertex format
    [[nodiscard]] bool IsSimpleVertex() const { return !simple_vertices.empty(); }

    //! Get total vertex count (handles both vertex types)
    [[nodiscard]] uint32_t GetVertexCount() const {
        if (!simple_vertices.empty()) {
            return static_cast<uint32_t>(simple_vertices.size());
        }
        return vertex_count > 0 ? vertex_count : static_cast<uint32_t>(vertices.size());
    }

    //! Calculate bounding sphere from vertices (if not manually set)
    void CalculateBoundingSphere();

    //! Update normal transform from world transform
    void UpdateNormalTransform();
};

} // namespace oxygen::graphics
