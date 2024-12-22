//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/Shaders.h"

#include "Content.h"
#include "Oxygen/Base/Logging.h"

using namespace oxygen::renderer::d3d12;

namespace {

  template <typename Enum>
  constexpr auto to_underlying(Enum e) noexcept
  {
    return static_cast<std::underlying_type_t<Enum>>(e);
  }

  class CompiledShader
  {
  public:
    //static constexpr uint32_t hash_length{ 16 };
    static constexpr uint32_t hash_length{ 0 };
    static constexpr auto buffer_size(const uint64_t size) -> uint64_t { return sizeof(byte_code_size_) + hash_length + size; }

    [[nodiscard]] constexpr auto byte_code_size() const -> uint64_t { return byte_code_size_; }
    //[[nodiscard]] constexpr auto hash() const -> const uint8_t* { return &hash_[0]; }
    [[nodiscard]] constexpr auto byte_code() const -> const uint8_t* { return &byte_code_; }
    [[nodiscard]] constexpr auto buffer_size() const -> uint64_t { return sizeof(byte_code_size_) + hash_length + byte_code_size_; }

  private:
    //uint8_t hash_[hash_length]{};
    uint64_t byte_code_size_{ 0 };
    uint8_t byte_code_{ 0 };
  };

  using CompiledShaderPtr = const CompiledShader*;


  // This is a chunk of memory that contains all compiled engine shaders.
  // The blob is an array of shader byte code, consisting of a 64 bit size and
  // an array of bytes.
  std::unique_ptr<uint8_t[]> engine_shaders_blob{};

  // Each element in this array points to an offset within the shaders blob
  CompiledShaderPtr engine_shaders[to_underlying(EngineShaderId::kCount)]{};

  auto LoadEngineShaders() -> bool
  {
    DCHECK_F(!engine_shaders_blob);

    uint64_t size{ 0 };
    bool result{ oxygen::content::LoadEngineShaders(engine_shaders_blob, size) };
    DCHECK_F(engine_shaders_blob && size);

    uint64_t offset{ 0 };
    uint64_t index{ 0 };
    while (offset < size && result)
    {
      DCHECK_LT_F(index, to_underlying(EngineShaderId::kCount));
      CompiledShaderPtr shader{ engine_shaders[index] };
      DCHECK_F(!shader);
      result &= index < to_underlying(EngineShaderId::kCount) && !shader;
      if (!result) break;

      shader = reinterpret_cast<CompiledShaderPtr>(&engine_shaders_blob[offset]);
      offset += shader->buffer_size();
      ++index;
    }
    DCHECK_F(offset == size && index == to_underlying(EngineShaderId::kCount));

    return result;
  }

} // anonymous namespace

bool
shaders::Initialize()
{
  return LoadEngineShaders();
}

void
shaders::Shutdown()
{
  for (uint32_t i{ 0 }; i < to_underlying(EngineShaderId::kCount); ++i)
  {
    engine_shaders[i] = {};
  }
  engine_shaders_blob.reset();
}

auto shaders::GetEngineShader(const EngineShaderId id) -> D3D12_SHADER_BYTECODE
{
  DCHECK_LT_F(to_underlying(id), to_underlying(EngineShaderId::kCount));
  const CompiledShaderPtr shader{ engine_shaders[to_underlying(id)] };
  DCHECK_F(shader && shader->byte_code_size());

  return { shader->byte_code(), shader->byte_code_size() };
}
