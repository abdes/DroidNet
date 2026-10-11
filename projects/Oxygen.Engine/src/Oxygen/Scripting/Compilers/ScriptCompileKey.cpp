//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Scripting/Compilers/ScriptCompileKey.h>

namespace oxygen::scripting {

namespace {
  constexpr uint64_t kLuauCompilerFingerprint = 0x6C7561755F7631ULL;
  constexpr uint64_t kLuauVmBytecodeVersion = 1ULL;
  constexpr uint64_t kUnknownCompilerFingerprint = 0x756E6B6E6F776E31ULL;
  constexpr uint64_t kUnknownVmBytecodeVersion = 0ULL;
#ifdef _WIN64
  constexpr uint64_t kPlatformAbiSalt = 0x77696E36345F6D73ULL;
#elif defined(__linux__) && defined(__x86_64__)
  constexpr uint64_t kPlatformAbiSalt = 0x6C6E7836345F6763ULL;
#elif defined(__APPLE__) && defined(__aarch64__)
  constexpr uint64_t kPlatformAbiSalt = 0x6D61635F61726D36ULL;
#else
  constexpr uint64_t kPlatformAbiSalt = 0x756E6B6E6F776E5FULL;
#endif
} // namespace

auto ComputeScriptCompileKey(const data::AssetKey& asset_key,
  const ScriptSourceBlob& source,
  const core::meta::scripting::ScriptCompileMode compile_mode)
  -> IScriptCompilationService::CompileKey
{
  const auto is_luau
    = source.Language() == data::pak::scripting::ScriptLanguage::kLuau;
  uint64_t seed = 0;
  HashCombine(seed, asset_key);
  HashCombine(seed, compile_mode);
  HashCombine(seed, source.Language());
  HashCombine(seed, source.Compression());
  HashCombine(seed, source.GetOrigin());
  HashCombine(seed, source.GetCanonicalName().get());
  HashCombine(seed, source.ContentHash());
  HashCombine(
    seed, is_luau ? kLuauCompilerFingerprint : kUnknownCompilerFingerprint);
  HashCombine(
    seed, is_luau ? kLuauVmBytecodeVersion : kUnknownVmBytecodeVersion);
  HashCombine(seed, kPlatformAbiSalt);
  for (const auto byte : source.BytesView()) {
    HashCombine(seed, byte);
  }
  return IScriptCompilationService::CompileKey { seed };
}

} // namespace oxygen::scripting
