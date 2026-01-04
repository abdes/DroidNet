//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <unknwn.h>
#include <windows.h>
#include <wrl/client.h>

#include <d3d12shader.h>

#include <dxcapi.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Reflect.h>

using Microsoft::WRL::ComPtr;

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  inline constexpr uint32_t kOxygenReflectionMagic = 0x4F585246U; // "OXRF"
  inline constexpr uint32_t kOxygenReflectionVersion = 1;

  inline constexpr uint8_t kShaderModelMajor = 6;
  inline constexpr uint8_t kShaderModelMinor = 6;

  auto WriteU32(serio::AnyWriter& w, const uint32_t v, const char* what) -> void
  {
    const auto r = w.Write<uint32_t>(v);
    if (!r) {
      throw std::runtime_error(std::string(what) + ": " + r.error().message());
    }
  }

  auto WriteU8(serio::AnyWriter& w, const uint8_t v, const char* what) -> void
  {
    const auto r = w.Write<uint8_t>(v);
    if (!r) {
      throw std::runtime_error(std::string(what) + ": " + r.error().message());
    }
  }

  auto WriteU16(serio::AnyWriter& w, const uint16_t v, const char* what) -> void
  {
    const auto r = w.Write<uint16_t>(v);
    if (!r) {
      throw std::runtime_error(std::string(what) + ": " + r.error().message());
    }
  }

  auto WriteString16(
    serio::AnyWriter& w, const std::string& s, const char* what) -> void
  {
    if (s.size() > 0xFFFFu) {
      throw std::runtime_error(std::string(what) + " too long");
    }
    WriteU16(w, static_cast<uint16_t>(s.size()), "write string length");
    if (!s.empty()) {
      const auto bytes = std::as_bytes(std::span(s.data(), s.size()));
      const auto r = w.WriteBlob(bytes);
      if (!r) {
        throw std::runtime_error(
          std::string(what) + ": " + r.error().message());
      }
    }
  }

} // namespace

auto ExtractAndSerializeReflection(const oxygen::graphics::ShaderInfo& shader,
  std::span<const std::byte> dxil) -> std::vector<std::byte>
{
  ComPtr<IDxcUtils> utils;
  oxygen::windows::ThrowOnFailed(
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)));

  // Create reflection interface.
  DxcBuffer buffer {};
  buffer.Ptr = dxil.data();
  buffer.Size = dxil.size();
  buffer.Encoding = DXC_CP_ACP;

  ComPtr<ID3D12ShaderReflection> reflection;
  oxygen::windows::ThrowOnFailed(
    utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection)));

  D3D12_SHADER_DESC desc {};
  oxygen::windows::ThrowOnFailed(reflection->GetDesc(&desc));

  // Serialize a minimal reflection blob.
  // Format:
  // - u32 magic ("OXRF")
  // - u32 version
  // - u8  stage (oxygen::ShaderType)
  // - u8  shader_model_major
  // - u8  shader_model_minor
  // - u8  reserved
  // - str16 entry_point
  // - u32 bound_resources
  // - u32 threadgroup_size_x/y/z (for compute; 0 otherwise)
  // - repeated resources:
  //   - u8  resource_type (D3D_SIT_*)
  //   - u8  bind_point_kind (0=cbv,1=srv,2=uav,3=sampler)
  //   - u16 space
  //   - u32 bind_point
  //   - u32 bind_count
  //   - u32 byte_size (CBV only; 0 otherwise)
  //   - str16 name

  uint32_t tgx = 0;
  uint32_t tgy = 0;
  uint32_t tgz = 0;
  if (shader.type == oxygen::ShaderType::kCompute) {
    reflection->GetThreadGroupSize(&tgx, &tgy, &tgz);
  }

  serio::MemoryStream stream;
  serio::Writer<serio::MemoryStream> w(stream);
  const auto packed = w.ScopedAlignment(1);
  (void)packed;

  WriteU32(w, kOxygenReflectionMagic, "write magic");
  WriteU32(w, kOxygenReflectionVersion, "write version");
  WriteU8(w, static_cast<uint8_t>(shader.type), "write stage");
  WriteU8(w, kShaderModelMajor, "write shader_model_major");
  WriteU8(w, kShaderModelMinor, "write shader_model_minor");
  WriteU8(w, 0, "write reserved");
  WriteString16(w, shader.entry_point, "write entry_point");
  WriteU32(w, desc.BoundResources, "write bound_resources");
  WriteU32(w, tgx, "write tgx");
  WriteU32(w, tgy, "write tgy");
  WriteU32(w, tgz, "write tgz");

  for (uint32_t i = 0; i < desc.BoundResources; ++i) {
    D3D12_SHADER_INPUT_BIND_DESC rdesc {};
    oxygen::windows::ThrowOnFailed(
      reflection->GetResourceBindingDesc(i, &rdesc));

    uint8_t kind = 0;
    switch (rdesc.Type) {
    case D3D_SIT_CBUFFER:
      kind = 0;
      break;
    case D3D_SIT_SAMPLER:
      kind = 3;
      break;
    default:
      switch (rdesc.Type) {
      case D3D_SIT_UAV_RWTYPED:
      case D3D_SIT_UAV_RWSTRUCTURED:
      case D3D_SIT_UAV_RWBYTEADDRESS:
      case D3D_SIT_UAV_APPEND_STRUCTURED:
      case D3D_SIT_UAV_CONSUME_STRUCTURED:
      case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
      case D3D_SIT_UAV_FEEDBACKTEXTURE:
        kind = 2;
        break;
      default:
        kind = 1;
        break;
      }
      break;
    }

    WriteU8(w, static_cast<uint8_t>(rdesc.Type), "write resource_type");
    WriteU8(w, kind, "write bind_kind");
    WriteU16(w, static_cast<uint16_t>(rdesc.Space), "write space");
    WriteU32(w, static_cast<uint32_t>(rdesc.BindPoint), "write bind_point");
    WriteU32(w, static_cast<uint32_t>(rdesc.BindCount), "write bind_count");

    uint32_t byte_size = 0;
    if (rdesc.Type == D3D_SIT_CBUFFER) {
      const auto* name = rdesc.Name ? rdesc.Name : "";
      if (std::strlen(name) > 0U) {
        if (auto* cb = reflection->GetConstantBufferByName(name);
          cb != nullptr) {
          D3D12_SHADER_BUFFER_DESC cb_desc {};
          oxygen::windows::ThrowOnFailed(cb->GetDesc(&cb_desc));
          byte_size = cb_desc.Size;
        }
      }
    }

    WriteU32(w, byte_size, "write byte_size");

    WriteString16(w, std::string(rdesc.Name ? rdesc.Name : ""), "write name");
  }

  const auto flush = w.Flush();
  if (!flush) {
    throw std::runtime_error(
      std::string("flush reflection: ") + flush.error().message());
  }

  const auto bytes = stream.Data();
  std::vector<std::byte> out(bytes.begin(), bytes.end());

  LOG_F(INFO, "Reflection: {} resources", desc.BoundResources);
  return out;
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
