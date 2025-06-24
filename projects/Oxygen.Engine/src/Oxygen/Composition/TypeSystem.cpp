//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <unordered_map>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/Detail/FastIntMap.h>
#include <Oxygen/Composition/Detail/GetTrulySingleInstance.h>
#include <Oxygen/Composition/TypeSystem.h>

namespace {
auto GetTypeNameHash(const char* const name)
{
  return std::hash<std::string_view> {}(std::string_view(name, strlen(name)));
}
} // namespace

using oxygen::TypeRegistry;
using oxygen::composition::detail::FastIntMap;
using oxygen::composition::detail::GetTrulySingleInstance;

class TypeRegistry::Impl {
public:
  using TypeKey = size_t;
  FastIntMap type_map_;
  std::unordered_map<TypeId, std::string> id_to_name_;
  TypeId next_type_id_ = 1;
  std::shared_mutex mutex_;
};

TypeRegistry::TypeRegistry()
  : impl_(new Impl())
{
}

TypeRegistry::~TypeRegistry() { delete impl_; }

auto TypeRegistry::Get() -> TypeRegistry&
{
  return GetTrulySingleInstance<TypeRegistry>("TypeRegistry");
}

auto TypeRegistry::RegisterType(const char* name) const -> TypeId
{
  if ((name == nullptr) || strnlen(name, 1) == 0) {
    throw std::invalid_argument(
      "cannot use `null` or empty type name to register a type");
  }
  const auto name_hash = GetTypeNameHash(name);

  std::unique_lock lock(impl_->mutex_);
  if (TypeId out_id { 0 }; impl_->type_map_.Get(name_hash, out_id)) {
    return out_id;
  }
  const auto id = impl_->next_type_id_++;
  CHECK_F(id != kInvalidTypeId, "TypeId overflow, aborting...");
  impl_->type_map_.Insert(name_hash, id);
  impl_->id_to_name_[id] = name;
  return id;
}

auto TypeRegistry::GetTypeId(const char* name) const -> TypeId
{
  const auto name_hash = GetTypeNameHash(name);
  std::shared_lock lock(impl_->mutex_);
  TypeId out_id { 0 };
  if (!impl_->type_map_.Get(name_hash, out_id)) {
    throw std::invalid_argument(
      std::string("no type with name=`{") + name + "}` is registered");
  }
  return out_id;
}

auto TypeRegistry::GetTypeName(TypeId id) const -> std::string_view
{
  std::shared_lock lock(impl_->mutex_);
  auto it = impl_->id_to_name_.find(id);
  if (it == impl_->id_to_name_.end()) {
    throw std::invalid_argument("no type with given id is registered");
  }
  return it->second.c_str();
}

auto TypeRegistry::GetTypeNamePretty(TypeId id) const -> std::string_view
{
  return ExtractQualifiedClassName(GetTypeName(id));
}

auto TypeRegistry::ExtractQualifiedClassName(std::string_view signature)
  -> std::string_view
{
  // Find the start of the fully qualified class
  auto method_pos = signature.rfind("::");
  if (method_pos == std::string_view::npos)
    return {};

  // Walk backward to find the start of the class name
  std::size_t start = method_pos;
  while (
    start > 0 && signature[start - 1] != ' ' && signature[start - 1] != '`') {
    --start;
  }

  return signature.substr(start, method_pos - start);
}
