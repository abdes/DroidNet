//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Data/MaterialAsset.h>

//! The one writer of each authored format in the Cooker tests.
/*!
 A test names what a document must say, never how the contract spells it: a
 document knows its own identity, and a reference names its target's
 identity. `SpellReference` turns a target into the text the contract writes,
 so a change of the reference contract changes that function and the
 identity fields below, not the tests.

 A test that checks a spelling itself (a schema or grammar refusal) writes the
 text with `Set`.
*/
namespace oxygen::cooker::test {

//! The text an authored file whose identity is `anchor` writes to name
//! `target`: relative within one mount, absolute across mounts
//! (`content::RelativeReference`).
[[nodiscard]] inline auto SpellReference(
  const std::string_view anchor, const std::string_view target) -> std::string
{
  return oxygen::content::RelativeReference(anchor, target);
}

//! The leaf of `identity` without its extension (`Wall` for
//! `/Content/Materials/Wall.omat`): the default `name` of a document.
[[nodiscard]] inline auto StemOf(const std::string_view identity) -> std::string
{
  return std::filesystem::path { std::string { identity } }.stem().string();
}

//! A document of one authored format with its identity. Formats derive from it
//! and start from a minimal document their schema accepts.
template <typename Self> class AuthoredDocument {
public:
  [[nodiscard]] auto Identity() const -> const std::string&
  {
    return identity_;
  }

  [[nodiscard]] auto Json() const -> const nlohmann::json& { return json_; }

  [[nodiscard]] auto Text() const -> std::string { return json_.dump(2); }

  //! The text this document writes to name `target`.
  [[nodiscard]] auto Spell(const std::string_view target) const -> std::string
  {
    return SpellReference(identity_, target);
  }

  //! Sets the field at the JSON pointer `pointer` to `value`, as written.
  auto Set(const std::string_view pointer, nlohmann::json value) -> Self&
  {
    json_[nlohmann::json::json_pointer { std::string { pointer } }]
      = std::move(value);
    return Derived();
  }

  //! Removes the field or array element at the JSON pointer `pointer`.
  auto Remove(const std::string_view pointer) -> Self&
  {
    const auto at = nlohmann::json::json_pointer { std::string { pointer } };
    auto& parent = json_[at.parent_pointer()];
    if (parent.is_array()) {
      parent.erase(std::stoul(at.back()));
    } else {
      parent.erase(at.back());
    }
    return Derived();
  }

  //! Sets the reference field at `pointer` to name `target`.
  auto Reference(const std::string_view pointer, const std::string_view target)
    -> Self&
  {
    return Set(pointer, Spell(target));
  }

  //! Appends a reference naming `target` to the array at `pointer`.
  auto AppendReference(
    const std::string_view pointer, const std::string_view target) -> Self&
  {
    return Append(pointer, Spell(target));
  }

  //! Appends `value` to the array at `pointer`, creating the array.
  auto Append(const std::string_view pointer, nlohmann::json value) -> Self&
  {
    json_[nlohmann::json::json_pointer { std::string { pointer } }].push_back(
      std::move(value));
    return Derived();
  }

protected:
  AuthoredDocument(std::string identity, nlohmann::json json)
    : identity_(std::move(identity))
    , json_(std::move(json))
  {
  }

private:
  auto Derived() -> Self& { return static_cast<Self&>(*this); }

  std::string identity_;
  nlohmann::json json_;
};

//! A material descriptor (`*.omat.json`).
class MaterialDescriptor final : public AuthoredDocument<MaterialDescriptor> {
public:
  explicit MaterialDescriptor(std::string identity)
    : AuthoredDocument(identity, { { "name", StemOf(identity) } })
  {
  }

  //! Binds the texture `target` to `slot` (`base_color`, `normal`, ...).
  auto Texture(const std::string_view slot, const std::string_view target)
    -> MaterialDescriptor&
  {
    return Reference(
      "/textures/" + std::string { slot } + "/virtual_path", target);
  }
};

//! A texture descriptor (`*.otex.json`) whose `source` is the file reference
//! `source`, as written.
class TextureDescriptor final : public AuthoredDocument<TextureDescriptor> {
public:
  TextureDescriptor(std::string identity, const std::string_view source)
    : AuthoredDocument(std::move(identity), { { "source", source } })
  {
  }
};

//! A geometry descriptor (`*.ogeo.json`): one procedural LOD with one submesh
//! that uses the engine's default material.
class GeometryDescriptor final : public AuthoredDocument<GeometryDescriptor> {
public:
  explicit GeometryDescriptor(std::string identity)
    : AuthoredDocument(identity,
        {
          { "name", StemOf(identity) },
          { "lods",
            nlohmann::json::array({ {
              { "name", "LOD0" },
              { "mesh_type", "procedural" },
              { "procedural",
                { { "generator", "Capsule" }, { "mesh_name", "Capsule" } } },
              { "submeshes",
                nlohmann::json::array({ {
                  { "surface_id", "018f8f8f-1111-7111-8111-111111111111" },
                  { "material_ref",
                    SpellReference(
                      identity, data::MaterialAsset::kDefaultVirtualPath) },
                  { "views",
                    nlohmann::json::array({ { { "view_ref", "__all__" } } }) },
                } }) },
            } }) },
        })
  {
  }

  //! Makes submesh `submesh` of LOD `lod` use the material `target`.
  auto SubmeshMaterial(const std::size_t lod, const std::size_t submesh,
    const std::string_view target) -> GeometryDescriptor&
  {
    return Reference("/lods/" + std::to_string(lod) + "/submeshes/"
        + std::to_string(submesh) + "/material_ref",
      target);
  }

  //! Appends `buffer` to the descriptor's own buffers, naming it `product`.
  //! A product is named relative to the descriptor, so it is in its mount.
  auto AppendBuffer(nlohmann::json buffer, const std::string_view product)
    -> GeometryDescriptor&
  {
    buffer["virtual_path"] = Spell(product);
    return Append("/buffers", std::move(buffer));
  }
};

//! A buffer container (`*.buffers.json`) with no buffers yet.
class BufferContainer final : public AuthoredDocument<BufferContainer> {
public:
  explicit BufferContainer(std::string identity)
    : AuthoredDocument(
        std::move(identity), { { "buffers", nlohmann::json::array() } })
  {
  }

  //! Appends `buffer`, naming it `product`, relative to the container.
  auto AppendBuffer(nlohmann::json buffer, const std::string_view product)
    -> BufferContainer&
  {
    buffer["virtual_path"] = Spell(product);
    return Append("/buffers", std::move(buffer));
  }
};

//! A collision shape descriptor (`*.ocshape.json`): a unit box of the physics
//! material `material`.
class CollisionShapeDescriptor final
  : public AuthoredDocument<CollisionShapeDescriptor> {
public:
  CollisionShapeDescriptor(
    std::string identity, const std::string_view material)
    : AuthoredDocument(identity,
        {
          { "shape_type", "box" },
          { "material_ref", SpellReference(identity, material) },
          { "half_extents", { 0.5, 0.5, 0.5 } },
        })
  {
  }
};

//! A physics material descriptor (`*.opmat.json`).
class PhysicsMaterialDescriptor final
  : public AuthoredDocument<PhysicsMaterialDescriptor> {
public:
  explicit PhysicsMaterialDescriptor(std::string identity)
    : AuthoredDocument(identity, { { "name", StemOf(identity) } })
  {
  }
};

//! A physics sidecar (`*.physics-sidecar.json`) with no bindings yet.
class PhysicsSidecar final : public AuthoredDocument<PhysicsSidecar> {
public:
  explicit PhysicsSidecar(std::string identity)
    : AuthoredDocument(
        std::move(identity), { { "bindings", nlohmann::json::object() } })
  {
  }

  //! Appends `binding` to `bindings/<kind>` (`rigid_bodies`, `colliders`...).
  auto AppendBinding(const std::string_view kind, nlohmann::json binding)
    -> PhysicsSidecar&
  {
    return Append("/bindings/" + std::string { kind }, std::move(binding));
  }
};

//! An input source (`*.input.json`) with no actions and no contexts.
class InputSource final : public AuthoredDocument<InputSource> {
public:
  explicit InputSource(std::string identity)
    : AuthoredDocument(std::move(identity),
        {
          { "actions", nlohmann::json::array() },
          { "contexts", nlohmann::json::array() },
        })
  {
  }

  //! Declares the action `name` of `type` (`bool`, `axis1d`, `axis2d`).
  auto Action(const std::string_view name, const std::string_view type)
    -> InputSource&
  {
    return Append("/actions", { { "name", name }, { "type", type } });
  }

  //! Declares the mapping context `name`.
  auto Context(const std::string_view name, const int priority = 0)
    -> InputSource&
  {
    return Append("/contexts",
      {
        { "name", name },
        { "priority", priority },
        { "mappings", nlohmann::json::array() },
      });
  }

  //! The text a mapping of this source writes to use the action `target` of
  //! another source.
  [[nodiscard]] auto ActionReference(const std::string_view target) const
    -> std::string
  {
    return Spell(target);
  }
};

//! A scene descriptor (`*.scene.json`) of the current version with one root
//! node.
class SceneDescriptor final : public AuthoredDocument<SceneDescriptor> {
public:
  explicit SceneDescriptor(std::string identity)
    : AuthoredDocument(identity,
        {
          { "version", content::import::internal::kSceneDescriptorVersion },
          { "name", StemOf(identity) },
          { "nodes",
            nlohmann::json::array({ {
              { "id", "00000000-0000-4000-8000-000000000001" },
              { "name", "Root" },
            } }) },
        })
  {
  }

  //! Makes node `node` render the geometry `target`.
  auto Renderable(const std::size_t node, const std::string_view target)
    -> SceneDescriptor&
  {
    return Append(
      "/renderables", { { "node", node }, { "geometry_ref", Spell(target) } });
  }
};

} // namespace oxygen::cooker::test
