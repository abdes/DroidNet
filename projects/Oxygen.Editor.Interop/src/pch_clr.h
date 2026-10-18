//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// Precompile STL, GLM, and Oxygen Config/EditorInterface headers under
// `#pragma managed(push, off)`. Avoid OxCo coroutine headers in the `/clr` PCH
// as MSVC's `/clr` PCH metadata emitter triggers LNK2022 on nested OxCo types.
#pragma managed(push, off)

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#pragma warning(push)
#pragma warning(disable : 4793)
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/EditorInterface/Api.h>
#include <Oxygen/EditorInterface/EngineContext.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Types/Color.h>

#include <api_export.h>
#include <EditorModule/EditorCommand.h>
#include <EditorModule/NodeRegistry.h>
#include <EditorModule/SurfaceRegistry.h>
#pragma warning(pop)

#pragma managed(pop)

#pragma managed(push, on)
#include <vcclr.h>
#include <msclr/auto_gcroot.h>
#include <msclr/gcroot.h>
#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

#include "Config.h"
#include "EngineContext.h"
#pragma managed(pop)
