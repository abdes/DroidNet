//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace oxygen {

//! Identifies a GPU device in the system.
/*!
  The device ID is a unique identifier for a GPU device in the system. The
  64-bit data is interpreted differently between backends. For DX12, the DXGI
  layer splits it into two parts: an unsigned 32-bit high part (DWORD) and a
  signed 32-bit low part (LONG) to form a `LUID`. For Vulkan, the 64-bit data is
  the physical device index.
*/
using DeviceId = int64_t;

//! Graphics configuration data, serialized to JSON and used to configure the
//! graphics backend module when being loaded.
struct GraphicsConfig {
  bool enable_debug { false }; //!< Enable the backend debug layer.
  bool enable_validation { false }; //!< Enable GPU validation.

  //! Device selection guidance.
  /*!
    The graphics backend will try to select the most suitable GPU based on its
    capabilities, but the selection can be influenced by the following
    properties.

    \note The properties are hints and if they cannot be satisfied, the
    renderer will fall back to the default behavior.

    \note The `preferred_card_name` and `preferred_card_device_id` are
    mutually exclusive.
  */
  //! @{
  std::optional<std::string> preferred_card_name;
  std::optional<DeviceId> preferred_card_device_id;
  //! @}

  bool headless { false }; //!< Run the engine without a window.
  bool enable_imgui { false }; //!< Enable ImGui integration.
  bool enable_vsync { true }; //!< Enable vertical synchronization.

  //! Backend-specific configuration as a JSON string.
  std::string extra = "{}";
};

//! Configuration structure passed to backends during creation. A C-compatible
//! structure that can be passed across DLL boundaries. Acts like a
//! `std::string_view`: its data is read-only, and the lifetime of the data will
//! last only for the duration of the call.
struct SerializedBackendConfig {
  const char* json_data; //!< UTF-8 encoded JSON string with configuration
  size_t size; //!< Length of the JSON data in bytes
};

//! Configuration structure for path resolution passed to backends during
//! creation. C-compatible and valid only for the duration of the call.
struct SerializedPathFinderConfig {
  const char* json_data; //!< UTF-8 encoded JSON string with path configuration
  size_t size; //!< Length of the JSON data in bytes
};

} // namespace oxygen
