//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/CodeInput.h>
#include <Oxygen/Input/InputMappingContext.h>

namespace oxygen::input {

namespace {
  auto MakeCodeKey(const std::string_view owner, const std::string_view name,
    const std::string_view extension) -> data::AssetKey
  {
    auto code_path = std::string { owner };
    code_path.push_back('/');
    code_path.append(name);
    code_path.append(extension);
    return data::AssetKey::FromCodePath(code_path);
  }
} // namespace

auto MakeCodeAction(const std::string_view owner, std::string name,
  const ActionValueType value_type) -> std::shared_ptr<Action>
{
  auto key = MakeCodeKey(owner, name, data::kInputActionDescriptorExtension);
  return std::make_shared<Action>(std::move(name), value_type, key);
}

auto MakeCodeContext(const std::string_view owner, std::string name)
  -> std::shared_ptr<InputMappingContext>
{
  auto key
    = MakeCodeKey(owner, name, data::kInputMappingContextDescriptorExtension);
  return std::make_shared<InputMappingContext>(std::move(name), key);
}

} // namespace oxygen::input
