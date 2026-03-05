//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Data/PakFormat_core.h>

// packed structs intentionally embed unaligned NamedType ResourceIndexT fields
OXYGEN_DIAGNOSTIC_PUSH
OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4315)
// NOLINTBEGIN(*-avoid-c-arrays,*-magic-numbers)

//! Oxygen PAK format audio domain schema.
/*!
 Owns packed audio resource descriptors.
*/
namespace oxygen::data::pak::audio {

//! Audio resource table entry.
#pragma pack(push, 1)
struct AudioResourceDesc {
  core::OffsetT data_offset; // Absolute offset to audio data
  core::DataBlobSizeT size_bytes; // Size of audio data
  uint32_t sample_rate; // Audio sample rate
  uint32_t channels; // Number of channels
  uint32_t audio_format; // PCM, Vorbis, etc.
  uint16_t bits_per_sample; // Bits per sample
  uint16_t alignment; // Required alignment

  // Tail reserve to keep fixed-size 32-byte record contract.
  uint8_t _reserved[4] = {}; // Tail reserve to keep fixed 32-byte record size
};
#pragma pack(pop)
static_assert(sizeof(AudioResourceDesc) == 32);

} // namespace oxygen::data::pak::audio

// NOLINTEND(*-avoid-c-arrays,*-magic-numbers)
OXYGEN_DIAGNOSTIC_POP
