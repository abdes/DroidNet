//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scripting/Bindings/Contracts/BindingDescription.h>
#include <Oxygen/Scripting/Bindings/Contracts/IScriptBindingPack.h>
#include <Oxygen/Scripting/Bindings/LuauDefinitions.h>
#include <Oxygen/Scripting/Bindings/Packs/Content/ContentBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/CoreBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Input/InputBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Physics/PhysicsBindingPack.h>
#include <Oxygen/Scripting/Bindings/Packs/Scene/SceneBindingPack.h>

namespace oxygen::scripting::bindings {

namespace {

  constexpr std::string_view kHeader
    = "-- Luau definitions of the Oxygen scripting API.\n"
      "-- Generated from the engine's binding registrations; do not edit.\n"
      "-- Parameter constructors (oxygen.param.*) are typed by the value the\n"
      "-- parameter has in self.params, which is what scripts read.\n";

  constexpr std::string_view kIndent = "  ";

  //! A function signature split into its parts: `<generics>(params) -> ret`.
  struct SignatureParts {
    std::string_view generics;
    std::string_view params;
    std::string_view returns;
  };

  auto SplitSignature(const std::string_view signature) -> SignatureParts
  {
    auto parts = SignatureParts {};
    auto rest = signature;
    if (rest.starts_with('<')) {
      const auto close = rest.find('>');
      CHECK_F(close != std::string_view::npos, "bad signature '{}'", signature);
      parts.generics = rest.substr(0, close + 1);
      rest.remove_prefix(close + 1);
    }
    CHECK_F(rest.starts_with('('), "bad signature '{}'", signature);
    auto depth = 0;
    auto end = std::size_t { 0 };
    for (; end < rest.size(); ++end) {
      if (rest[end] == '(' || rest[end] == '{' || rest[end] == '<') {
        ++depth;
      } else if (rest[end] == ')' || rest[end] == '}' || rest[end] == '>') {
        if (rest[end] == '>' && end > 0 && rest[end - 1] == '-') {
          continue; // part of a nested function type's arrow
        }
        if (--depth == 0) {
          break;
        }
      }
    }
    CHECK_F(end < rest.size(), "bad signature '{}'", signature);
    parts.params = rest.substr(1, end - 1);
    auto after = rest.substr(end + 1);
    constexpr std::string_view kArrow = " -> ";
    CHECK_F(after.starts_with(kArrow), "bad signature '{}'", signature);
    parts.returns = after.substr(kArrow.size());
    return parts;
  }

  //! `function name(self, params): returns`, as an extern type declares it.
  auto ExternMethod(const BindingFunction& method) -> std::string
  {
    const auto parts = SplitSignature(method.signature);
    auto text = std::string("function ") + method.name
      + std::string(parts.generics) + "(self";
    if (!parts.params.empty()) {
      text += ", ";
      text += parts.params;
    }
    text += "): ";
    text += parts.returns;
    return text;
  }

  //! `(self: Type, params) -> returns`, for a method of a table type.
  auto TableMethod(const std::string_view type_name,
    const BindingFunction& method) -> std::string
  {
    const auto parts = SplitSignature(method.signature);
    auto text
      = std::string(parts.generics) + "(self: " + std::string(type_name);
    if (!parts.params.empty()) {
      text += ", ";
      text += parts.params;
    }
    text += ") -> ";
    text += parts.returns;
    return text;
  }

  struct MergedClass {
    std::string name;
    bool is_table = false;
    std::vector<BindingFunction> methods;
    std::vector<BindingField> properties;
    std::vector<BindingFunction> metamethods;
  };

  //! Members of one `oxygen` table, by dotted path; children by name.
  struct NamespaceNode {
    std::vector<BindingFunction> functions;
    std::vector<BindingField> fields;
    std::map<std::string, NamespaceNode, std::less<>> children;
    bool untyped = false;
  };

  auto ChildFor(NamespaceNode& root, const std::string_view path)
    -> NamespaceNode&
  {
    auto* node = &root;
    auto rest = path;
    while (!rest.empty()) {
      const auto dot = rest.find('.');
      const auto segment = rest.substr(0, dot);
      node = &node->children[std::string(segment)];
      rest = dot == std::string_view::npos ? std::string_view {}
                                           : rest.substr(dot + 1);
    }
    return *node;
  }

  template <typename T>
  auto ContainsName(const std::vector<T>& items, const std::string_view name)
    -> bool
  {
    return std::ranges::any_of(items, [name](const T& item) -> bool {
      return std::string_view(item.name) == name;
    });
  }

  class DefinitionsBuilder {
  public:
    auto Add(const BindingNamespaceDescription& description) -> void
    {
      for (const auto& alias : description.types) {
        AddAlias(alias);
      }
      for (const auto& type : description.classes) {
        AddClass(type);
      }
      auto& node = ChildFor(root_, description.path);
      for (const auto& function : description.functions) {
        CHECK_F(!ContainsName(node.functions, function.name),
          "oxygen.{}.{} is described twice", description.path, function.name);
        node.functions.push_back(function);
      }
      for (const auto& field : description.fields) {
        CHECK_F(!ContainsName(node.fields, field.name),
          "oxygen.{}.{} is described twice", description.path, field.name);
        node.fields.push_back(field);
      }
    }

    auto AddUntyped(const std::string_view path) -> void
    {
      ChildFor(root_, path).untyped = true;
    }

    [[nodiscard]] auto Write() const -> std::string
    {
      auto out = std::string(kHeader);
      for (const auto& alias : aliases_) {
        out += "\ntype ";
        out += alias.name;
        out += " = ";
        out += alias.definition;
        out += '\n';
      }
      for (const auto& type : classes_) {
        out += '\n';
        WriteClass(out, type);
      }
      out += "\ndeclare oxygen: ";
      WriteNamespace(out, root_, 0);
      out += '\n';
      return out;
    }

  private:
    auto AddAlias(const BindingTypeAlias& alias) -> void
    {
      const auto existing = std::ranges::find_if(
        aliases_, [&alias](const BindingTypeAlias& item) -> bool {
          return item.name == alias.name;
        });
      if (existing != aliases_.end()) {
        CHECK_F(existing->definition == alias.definition,
          "type {} is declared twice with different definitions", alias.name);
        return;
      }
      aliases_.push_back(alias);
    }

    auto AddClass(const BindingClass& type) -> void
    {
      auto existing = std::ranges::find_if(
        classes_, [&type](const MergedClass& item) -> bool {
          return item.name == type.name;
        });
      if (existing == classes_.end()) {
        classes_.push_back(MergedClass {
          .name = std::string(type.name),
          .is_table = type.metatable.empty(),
          .methods = {},
          .properties = {},
          .metamethods = {},
        });
        existing = std::prev(classes_.end());
      }
      for (const auto& method : type.methods) {
        CHECK_F(!ContainsName(existing->methods, method.name),
          "{}:{} is described twice", type.name, method.name);
        existing->methods.push_back(method);
      }
      for (const auto& property : type.properties) {
        CHECK_F(!ContainsName(existing->properties, property.name),
          "{}.{} is described twice", type.name, property.name);
        existing->properties.push_back(property);
      }
      for (const auto& method : type.metamethods) {
        existing->metamethods.push_back(method);
      }
    }

    static auto WriteClass(std::string& out, const MergedClass& type) -> void
    {
      if (type.is_table) {
        out += "type " + type.name + " = {\n";
        for (const auto& method : type.methods) {
          out += std::string(kIndent) + method.name + ": "
            + TableMethod(type.name, method) + ",\n";
        }
        for (const auto& property : type.properties) {
          out += std::string(kIndent) + std::string(property.name) + ": "
            + std::string(property.type) + ",\n";
        }
        out += "}\n";
        return;
      }
      out += "declare extern type " + type.name + " with\n";
      for (const auto& property : type.properties) {
        out += std::string(kIndent) + std::string(property.name) + ": "
          + std::string(property.type) + "\n";
      }
      for (const auto& method : type.methods) {
        out += std::string(kIndent) + ExternMethod(method) + "\n";
      }
      for (const auto& method : type.metamethods) {
        out += std::string(kIndent) + method.name + ": "
          + std::string(method.signature) + "\n";
      }
      out += "end\n";
    }

    static auto WriteNamespace(std::string& out, const NamespaceNode& node,
      const std::size_t depth) -> void
    {
      if (node.untyped) {
        out += "any";
        return;
      }
      const auto indent = std::string((depth + 1) * kIndent.size(), ' ');
      out += "{\n";
      for (const auto& function : node.functions) {
        out += indent + function.name + ": " + std::string(function.signature)
          + ",\n";
      }
      for (const auto& field : node.fields) {
        out += indent + std::string(field.name) + ": " + std::string(field.type)
          + ",\n";
      }
      for (const auto& [name, child] : node.children) {
        out += indent + name + ": ";
        WriteNamespace(out, child, depth + 1);
        out += ",\n";
      }
      out += std::string(depth * kIndent.size(), ' ') + "}";
    }

    std::vector<BindingTypeAlias> aliases_;
    std::vector<MergedClass> classes_;
    NamespaceNode root_;
  };

} // namespace

auto CreateDefaultBindingPacks() -> std::vector<contracts::ScriptBindingPackPtr>
{
  return {
    CreateCoreBindingPack(),
    CreateContentBindingPack(),
    CreateInputBindingPack(),
    CreatePhysicsBindingPack(),
    CreateSceneBindingPack(),
  };
}

auto GenerateLuauDefinitions(
  const std::span<const contracts::ScriptBindingPackPtr> packs) -> std::string
{
  auto builder = DefinitionsBuilder {};
  for (const auto& pack : packs) {
    if (!pack) {
      continue;
    }
    for (const auto& registration : pack->Namespaces()) {
      if (registration.describe == nullptr) {
        builder.AddUntyped(registration.name);
        continue;
      }
      for (const auto& description : registration.describe()) {
        builder.Add(description);
      }
    }
  }
  return builder.Write();
}

} // namespace oxygen::scripting::bindings
