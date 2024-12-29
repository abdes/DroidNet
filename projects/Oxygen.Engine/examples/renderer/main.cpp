//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "main_module.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>

#include "oxygen/base/Compilers.h"
#include "oxygen/base/logging.h"
#include "oxygen/core/engine.h"
#include "oxygen/core/version.h"
#include "oxygen/platform-sdl/platform.h"
#include "Oxygen/Renderers/Direct3d12/Shaders.h"
#include "oxygen/Renderers/Loader/RendererLoader.h"
#include "ShaderCompiler.h"

using namespace std::chrono_literals;

using Microsoft::WRL::ComPtr;
using oxygen::Engine;
using oxygen::graphics::CreateRenderer;
using oxygen::graphics::DestroyRenderer;
using oxygen::graphics::GraphicsBackendType;

namespace {

  struct ShaderProfile
  {
    const char* file_name;
    const char* entry_point;
    oxygen::renderer::d3d12::EngineShaderId shader_id;
    oxygen::renderer::d3d12::ShaderType shader_type;
  };

  constexpr ShaderProfile kShaderProfiles[]
  {
    {
      .file_name = "FullScreenTriangle.hlsl",
      .entry_point = "FullScreenTriangleVS",
      .shader_id = oxygen::renderer::d3d12::EngineShaderId::kFullscreenTriangleVS,
      .shader_type = oxygen::renderer::d3d12::ShaderType::kVertex
    },
    {
      .file_name = "FillColor.hlsl",
      .entry_point = "FillColorPS",
      .shader_id = oxygen::renderer::d3d12::EngineShaderId::kFillColorPS,
      .shader_type = oxygen::renderer::d3d12::ShaderType::kPixel
    },
  };
  static_assert(std::size(kShaderProfiles) == static_cast<size_t>(oxygen::renderer::d3d12::EngineShaderId::kCount));

  constexpr auto kShadersSourceBase = R"(F:\projects\DroidNet\projects\Oxygen.Engine\Oxygen\Renderers\Direct3d12\Shaders)";
  constexpr auto kOutputArchive = "shaders.bin";
  std::vector<ComPtr<IDxcBlob>> shader_blobs;

  auto GetExecutablePath() -> std::filesystem::path
  {
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
  }

  auto GetShaderArchivePath() -> std::filesystem::path
  {
    // Get the path to the directory where the executable is located.
    const std::filesystem::path exe_path = GetExecutablePath();
    return exe_path / kOutputArchive;
  }

  auto CompilerShaders() -> bool
  {
    LOG_SCOPE_FUNCTION(INFO);

    // Initialize a ShaderCompiler.
    oxygen::ShaderCompiler compiler;
    if (!compiler.Init())
    {
      LOG_F(ERROR, "Failed to initialize ShaderCompiler.");
      return false;
    }

    for (const auto& profile : kShaderProfiles)
    {
      LOG_F(INFO, "{}", profile.file_name);

      std::filesystem::path shader_path = std::filesystem::path(kShadersSourceBase) / profile.file_name;

      // Read the shader file.
      std::ifstream shader_file(shader_path, std::ios::binary | std::ios::ate);
      if (!shader_file.is_open())
      {
        LOG_F(ERROR, "Failed to open shader file: {}", shader_path.string());
        return false;
      }

      std::streamsize size = shader_file.tellg();
      shader_file.seekg(0, std::ios::beg);

      std::vector<char> buffer(size);
      if (!shader_file.read(buffer.data(), size))
      {
        LOG_F(ERROR, "Failed to read shader file: {}", shader_path.string());
        return false;
      }

      // Compile the shader.
      ComPtr<IDxcBlob> shader_blob;
      std::vector<DxcDefine> defines; // Add any necessary defines here.
      if (!compiler.Compile(buffer.data(), static_cast<uint32_t>(size), profile.file_name, profile.shader_type, defines, shader_blob))
      {
        LOG_F(ERROR, "Failed to compile shader: {}", profile.file_name);
        return false;
      }

      shader_blobs.push_back(shader_blob);
      LOG_F(INFO, "-> blob size   : {}", shader_blob->GetBufferSize());
    }

    // Optionally, save the compiled shaders to disk or use them as needed.
    for (size_t i = 0; i < shader_blobs.size(); ++i)
    {
      const auto& blob = shader_blobs[i];
      const auto& profile = kShaderProfiles[i];

      std::filesystem::path output_path = std::filesystem::path(kShadersSourceBase) / (profile.file_name + std::string(".cso"));
      std::ofstream output_file(output_path, std::ios::binary);
      if (!output_file.is_open())
      {
        LOG_F(ERROR, "Failed to open output file: {}", output_path.string());
        return false;
      }

      output_file.write(
        static_cast<const char*>(blob->GetBufferPointer()),
        static_cast<std::streamsize>(blob->GetBufferSize()));
      if (!output_file)
      {
        LOG_F(ERROR, "Failed to write to output file: {}", output_path.string());
        return false;
      }
    }

    return true;
  }

  auto SaveCompiledShaders() -> bool
  {

    std::basic_ofstream<uint8_t> output_archive(GetShaderArchivePath(), std::ios::binary);
    if (!output_archive.is_open())
    {
      LOG_F(ERROR, "Failed to open output archive: {}", kOutputArchive);
      return false;
    }

    for (const auto& blob : shader_blobs)
    {
      const auto size = static_cast<std::streamsize>(blob->GetBufferSize());
      output_archive.write(reinterpret_cast<const uint8_t*>(&size), sizeof(size));
      output_archive.write(static_cast<const uint8_t*>(blob->GetBufferPointer()), size);
    }

    return true;
  }

  auto NeedToCompileShaders() -> bool
  {
    LOG_SCOPE_FUNCTION(1);

    // Get the path to the directory where the executable is located.
    const auto archive_path = GetShaderArchivePath();

    // Check if the output archive file exists.
    if (!exists(archive_path))
    {
      LOG_F(1, "Output archive does not exist: {}", archive_path.string());
      return true;
    }

    // Get the last write time of the output archive file.
    const auto output_archive_time = last_write_time(archive_path);

    // Check if any shader source file is more recent than the output archive.
    const bool need_to_compile = std::ranges::any_of(
      kShaderProfiles,
      [&](const ShaderProfile& profile)
      {
        const std::filesystem::path shader_path = std::filesystem::path(kShadersSourceBase) / profile.file_name;

        // Check if the shader source file exists.
        if (!exists(shader_path))
        {
          LOG_F(ERROR, "Shader source file does not exist: {}", shader_path.string());
          return true;
        }

        // Get the last write time of the shader source file.
        const auto shader_time = last_write_time(shader_path);

        // If the shader source file is more recent than the output archive, we need to recompile.
        if (shader_time > output_archive_time)
        {
          LOG_F(1, "Shader source file is more recent than output archive: {}", shader_path.string());
          return true;
        }

        return false;
      });

    return need_to_compile;
  }

}  // namespace


auto main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) -> int {
  auto status{ EXIT_SUCCESS };

#if defined(_DEBUG) && defined(OXYGEN_MSVC_VERSION)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  // Optional, but useful to time-stamp the start of the log.
  // Will also detect verbosity level on command line as -v.
  loguru::init(argc, argv);

  LOG_F(INFO, "{}", oxygen::version::NameVersion());

  // We want to control the destruction order of the important objects in the
  // system. For example, destroy the core before we destroy the platform.
  std::shared_ptr<oxygen::Platform> platform;
  std::shared_ptr<oxygen::Engine> engine;

  try {
    if (NeedToCompileShaders()) {
      // Compile shaders.
      if (!CompilerShaders())
      {
        return EXIT_FAILURE;
      }

      // Save the compiled shaders to the binary output archive.
      // Compile shaders.
      if (!SaveCompiledShaders())
      {
        return EXIT_FAILURE;
      }
      shader_blobs.clear();
    }
    else
    {
      LOG_F(INFO, "Engine shaders are up to date");
    }


    platform = std::make_shared<oxygen::platform::sdl::Platform>();

    Engine::Properties props{
        .application =
            {
                .name = "Triangle",
                .version = 0x0001'0000,
            },
        .extensions = {},
        .max_fixed_update_duration = 10ms,
    };


    constexpr oxygen::RendererProperties renderer_props{
#ifdef _DEBUG
        .enable_debug = true,
#endif
        .enable_validation = false,
    };
    CreateRenderer(GraphicsBackendType::kDirect3D12, platform, renderer_props);

    engine = std::make_shared<Engine>(platform, oxygen::graphics::GetRenderer(), props);

    const auto my_module = std::make_shared<MainModule>(platform);
    engine->AttachModule(my_module);

    engine->Run();

    LOG_F(INFO, "Exiting application");
    DestroyRenderer();
  }
  catch (std::exception const& err) {
    LOG_F(ERROR, "A fatal error occurred: {}", err.what());
    status = EXIT_FAILURE;
  }

  // Explicit destruction order due to dependencies.
  engine.reset();
  platform.reset();

  return status;
}
