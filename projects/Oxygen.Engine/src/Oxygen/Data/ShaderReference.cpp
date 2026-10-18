//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <Oxygen/Data/ShaderReference.h>

using oxygen::data::ShaderReference;

ShaderReference::ShaderReference(const pak::render::ShaderReferenceDesc& desc,
  const DescriptorStrings& strings)
  : shader_type_(static_cast<ShaderType>(desc.shader_type))
  , source_path_(strings.Get(desc.source_path_offset))
  , entry_point_(strings.Get(desc.entry_point_offset))
  , defines_(strings.Get(desc.defines_offset))
  , shader_hash_(desc.shader_hash)
{
}
