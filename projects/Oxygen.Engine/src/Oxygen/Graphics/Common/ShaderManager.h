//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

struct ShaderModule {
  ShaderRequest request {};
  uint64_t cache_key { 0 };
  std::shared_ptr<IShaderByteCode> bytecode;
  std::vector<std::byte> reflection_blob;
};

class ShaderManager : public Composition {
public:
  struct Config {
    std::string backend_name { "d3d12" };
    std::string archive_file_name { "shaders.bin" };

    //! Directory containing the shader archive.
    std::filesystem::path archive_dir;
  };

  OXGN_GFX_API explicit ShaderManager(Config config)
    : config_(std::move(config))
  {
    AddComponent<ObjectMetadata>(
      std::string("ShaderManager for ") + config_.backend_name);
    Initialize();
  }

  OXGN_GFX_API ~ShaderManager() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ShaderManager);
  OXYGEN_DEFAULT_MOVABLE(ShaderManager);

  //! Loads shader bytecode and metadata from the archive file.
  /*!
   This is a strict operation: any failure to read, parse, or validate the
   shader library aborts loading and throws an exception.

   @throw std::runtime_error if the shader library is missing or invalid.
  */
  OXGN_GFX_API auto Load() -> void;

  //! Removes all shaders from the cache.
  OXGN_GFX_API auto Clear() noexcept -> void;
  //! Retrieves compiled shader bytecode by canonical request.
  OXGN_GFX_NDAPI auto GetShaderBytecode(const ShaderRequest& request) const
    -> std::shared_ptr<IShaderByteCode>;

  //! Checks if a shader exists in the archive.
  OXGN_GFX_NDAPI auto HasShader(const ShaderRequest& request) const noexcept
    -> bool;

  //! Returns the total number of shaders in the archive.
  OXGN_GFX_NDAPI auto GetShaderCount() const noexcept -> size_t;

  [[nodiscard]] auto GetName() const noexcept
  {
    return GetComponent<ObjectMetadata>().GetName();
  }

private:
  //! Initialize the shader manager by loading the archive file.
  OXGN_GFX_API auto Initialize() -> void;

  Config config_ {};
  std::unordered_map<uint64_t, ShaderModule> shader_cache_;
  std::filesystem::path archive_path_;
};

} // namespace oxygen::graphics
