//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#pragma managed(push, off)
#include <EditorModule/NodeRegistry.h>
#pragma managed(pop)

#pragma managed(push, on)

#include <optional>
#include <string>

#include <msclr/marshal_cppstd.h>

#include <Views/ViewConfigManaged.h>

namespace Oxygen::Interop {

  namespace detail {
    //! The engine node ID of a managed one, if it is a nonempty node ID.
    inline auto ToNativeNodeId(System::Guid id)
      -> std::optional<::oxygen::data::NodeId> {
      const auto parsed = ::oxygen::data::NodeId::FromString(
        msclr::interop::marshal_as<std::string>(id.ToString("D")));
      if (!parsed || parsed.value().IsNil()) {
        return std::nullopt;
      }
      return parsed.value();
    }
  } // namespace detail

  /// <summary>
  /// A node of the editor's scene, or a node an instance brings in below it.
  /// </summary>
  public
  ref class NodeReferenceManaged sealed {
  public:
    /// <summary>A node of the scene; an instance root when
    /// <see cref="Path"/> is not empty.</summary>
    property System::Guid Node;

    /// <summary>The node IDs below <see cref="Node"/>, shallowest first;
    /// empty for the node itself.</summary>
    property array<System::Guid>^ Path;

    static NodeReferenceManaged^ FromNative(
      const ::oxygen::interop::module::NodeReferenceKey& key) {
      auto bytes = gcnew array<System::Byte>(16);
      for (int b = 0; b < 16; ++b) {
        bytes[b] = key.node[static_cast<std::size_t>(b)];
      }
      auto managed = gcnew NodeReferenceManaged();
      managed->Node = System::Guid(bytes);
      managed->Path
        = gcnew array<System::Guid>(static_cast<int>(key.path.size()));
      for (int i = 0; i < managed->Path->Length; ++i) {
        // Engine node IDs keep the canonical text's byte order.
        managed->Path[i] = System::Guid::ParseExact(
          gcnew System::String(::oxygen::data::to_string(
            key.path[static_cast<std::size_t>(i)])
              .c_str()),
          "D");
      }
      return managed;
    }

    //! The native key; a path element that is not a node ID is refused.
    static auto ToNative(NodeReferenceManaged^ reference)
      -> ::oxygen::interop::module::NodeReferenceKey {
      if (reference == nullptr) {
        throw gcnew System::ArgumentNullException("reference");
      }
      auto key = ::oxygen::interop::module::NodeReferenceKey {
        .node = detail::ToNativeKey<::oxygen::interop::module::UuidKey>(
          reference->Node),
        .path = {},
      };
      if (reference->Path == nullptr) {
        return key;
      }
      key.path.reserve(static_cast<std::size_t>(reference->Path->Length));
      for each (System::Guid id in reference->Path) {
        const auto parsed = detail::ToNativeNodeId(id);
        if (!parsed.has_value()) {
          throw gcnew System::ArgumentException(
            "A node path holds an empty node ID.", "reference");
        }
        key.path.push_back(*parsed);
      }
      return key;
    }
  };

} // namespace Oxygen::Interop

#pragma managed(pop)
