//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! ScenePrettyPrinter Command-Line Example
//!
//! This standalone executable demonstrates the ScenePrettyPrinter system with
//! configurable command-line options to test all features of the modern
//! C++20 template-based pretty printing system.

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <fmt/format.h>

#ifdef _WIN32
#  include <fcntl.h>
#  include <io.h>
#  include <windows.h>

#endif

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/ScenePrettyPrinter.h>
#include <Oxygen/Scene/Test/Helpers/TestSceneFactory.h>

using namespace oxygen::scene;
using oxygen::scene::testing::TestSceneFactory;

namespace {

//! Command line options for the example
struct ExampleOptions {
  // Scene creation options
  std::string scene_type = "parent_children";
  std::string scene_name = "ExampleScene";
  std::string json_file; // JSON scene specification file
  std::string json_inline; // Inline JSON scene specification
  int scene_size = 3;
  int tree_depth = 2;

  // Pretty printer options
  CharacterSet charset = CharacterSet::kUnicode;
  VerbosityLevel verbosity = VerbosityLevel::kCompact;
  LineEnding line_ending = LineEnding::kUnix;
  bool show_transforms = true;
  bool show_flags = true;
  int max_depth = -1;

  // Output options
  std::string output_type = "stdout";
  std::string output_file;
  bool debug_only = false;

  // Control options
  bool help = false;
  bool show_string_output = false;
};

//! Print usage information
void PrintUsage(const char* program_name)
{
  // clang-format off
  constexpr auto usage_text = R"(Usage: {} [OPTIONS]

ScenePrettyPrinter Example - Demonstrates C++20 template-based scene printing

Scene Creation Options:
  --scene-type TYPE      Scene type: parent_children, binary_tree, linear_chain, forest (default: parent_children)
  --scene-name NAME      Base name for scene nodes (default: ExampleScene)
  --json-file FILE       JSON scene specification file (overrides --scene-type)
  --json SPEC            Inline JSON scene specification (overrides --scene-type)
  --scene-size SIZE      Number of children/nodes (default: 3)
  --tree-depth DEPTH     Tree depth for binary_tree and forest types (default: 2)

Pretty Printer Options:
  --charset CHARSET      Character set: unicode, ascii (default: unicode)
  --verbosity LEVEL      Verbosity: none, compact, detailed (default: compact)
  --line-ending ENDING   Line endings: unix, windows (default: unix)
  --show-transforms      Show transform information (default: on)
  --hide-transforms      Hide transform information
  --show-flags           Show flag information (default: on)
  --hide-flags           Hide flag information
  --max-depth DEPTH      Maximum traversal depth, -1 for unlimited (default: -1)

Output Options:
  --output TYPE          Output type: stdout, stderr, file, string, logger (default: stdout)
  --output-file FILE     Output file (required when --output file)
  --debug-only           Only output in debug builds (compile-time control)
  --show-string          Also show string output for comparison

Control Options:
  --help, -h             Show this help message

Examples:
  {} --scene-type binary_tree --tree-depth 3 --verbosity detailed
  {} --charset ascii --output file --output-file scene.txt
  {} --scene-type forest --scene-size 5 --max-depth 2
  {} --json-file example_scene.json --verbosity compact
  {} --json '{{"nodes":[{{"name":"Root","children":[{{"name":"Child"}}]}}]}}'
)";
  // clang-format on

  std::cout << fmt::format(usage_text, program_name, program_name, program_name,
    program_name, program_name, program_name);
}

//! Parse command line arguments
bool ParseArgs(int argc, char* argv[], ExampleOptions& options)
{
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      options.help = true;
      return true;
    } else if (arg == "--scene-type" && i + 1 < argc) {
      options.scene_type = argv[++i];
    } else if (arg == "--scene-name" && i + 1 < argc) {
      options.scene_name = argv[++i];
    } else if (arg == "--json-file" && i + 1 < argc) {
      options.json_file = argv[++i];
    } else if (arg == "--json" && i + 1 < argc) {
      options.json_inline = argv[++i];
    } else if (arg == "--scene-size" && i + 1 < argc) {
      options.scene_size = std::stoi(argv[++i]);
    } else if (arg == "--tree-depth" && i + 1 < argc) {
      options.tree_depth = std::stoi(argv[++i]);
    } else if (arg == "--charset" && i + 1 < argc) {
      std::string charset = argv[++i];
      if (charset == "unicode") {
        options.charset = CharacterSet::kUnicode;
      } else if (charset == "ascii") {
        options.charset = CharacterSet::kAscii;
      } else {
        std::cerr << "Error: Invalid charset '" << charset << "'\n";
        return false;
      }
    } else if (arg == "--verbosity" && i + 1 < argc) {
      std::string verbosity = argv[++i];
      if (verbosity == "none") {
        options.verbosity = VerbosityLevel::kNone;
      } else if (verbosity == "compact") {
        options.verbosity = VerbosityLevel::kCompact;
      } else if (verbosity == "detailed") {
        options.verbosity = VerbosityLevel::kDetailed;
      } else {
        std::cerr << "Error: Invalid verbosity '" << verbosity << "'\n";
        return false;
      }
    } else if (arg == "--line-ending" && i + 1 < argc) {
      std::string ending = argv[++i];
      if (ending == "unix") {
        options.line_ending = LineEnding::kUnix;
      } else if (ending == "windows") {
        options.line_ending = LineEnding::kWindows;
      } else {
        std::cerr << "Error: Invalid line ending '" << ending << "'\n";
        return false;
      }
    } else if (arg == "--show-transforms") {
      options.show_transforms = true;
    } else if (arg == "--hide-transforms") {
      options.show_transforms = false;
    } else if (arg == "--show-flags") {
      options.show_flags = true;
    } else if (arg == "--hide-flags") {
      options.show_flags = false;
    } else if (arg == "--max-depth" && i + 1 < argc) {
      options.max_depth = std::stoi(argv[++i]);
    } else if (arg == "--output" && i + 1 < argc) {
      options.output_type = argv[++i];
    } else if (arg == "--output-file" && i + 1 < argc) {
      options.output_file = argv[++i];
    } else if (arg == "--debug-only") {
      options.debug_only = true;
    } else if (arg == "--show-string") {
      options.show_string_output = true;
    } else {
      std::cerr << "Error: Unknown argument '" << arg << "'\n";
      return false;
    }
  }

  // Validation
  if (options.output_type == "file" && options.output_file.empty()) {
    std::cerr << "Error: --output-file required when --output file\n";
    return false;
  }

  return true;
}

//! Create a scene based on the specified type and options
std::shared_ptr<Scene> CreateScene(const ExampleOptions& options)
{
  auto& factory = TestSceneFactory::Instance();
  factory.Reset();

  // Priority: JSON file > inline JSON > scene type
  if (!options.json_file.empty()) {
    // Read JSON from file
    std::ifstream file(options.json_file);
    if (!file) {
      std::cerr << "Error: Cannot open JSON file '" << options.json_file
                << "'\n";
      return nullptr;
    }

    std::string json_content(
      (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    try {
      return factory.CreateFromJson(json_content, options.scene_name);
    } catch (const std::exception& e) {
      std::cerr << "Error parsing JSON file '" << options.json_file
                << "': " << e.what() << "\n";
      return nullptr;
    }
  } else if (!options.json_inline.empty()) {
    // Use inline JSON
    try {
      return factory.CreateFromJson(options.json_inline, options.scene_name);
    } catch (const std::exception& e) {
      std::cerr << "Error parsing inline JSON: " << e.what() << "\n";
      return nullptr;
    }
  } else {
    // Use traditional scene types
    if (options.scene_type == "parent_children") {
      return factory.CreateParentWithChildrenScene(
        options.scene_name, options.scene_size);
    } else if (options.scene_type == "binary_tree") {
      return factory.CreateBinaryTreeScene(
        options.scene_name, options.tree_depth);
    } else if (options.scene_type == "linear_chain") {
      return factory.CreateLinearChainScene(
        options.scene_name, options.scene_size);
    } else if (options.scene_type == "forest") {
      return factory.CreateForestScene(
        options.scene_name, options.tree_depth, options.scene_size);
    } else {
      std::cerr << "Error: Unknown scene type '" << options.scene_type << "'\n";
      return nullptr;
    }
  }
}

//! Configure a printer with the specified options
template <typename PrinterT>
PrinterT&& ConfigurePrinter(PrinterT&& printer, const ExampleOptions& options)
{
  return std::move(printer.SetCharacterSet(options.charset)
      .SetVerbosity(options.verbosity)
      .SetLineEnding(options.line_ending)
      .ShowTransforms(options.show_transforms)
      .ShowFlags(options.show_flags)
      .SetMaxDepth(options.max_depth));
}

//! Print scene with the specified output type
void PrintScene(const Scene& scene, const ExampleOptions& options)
{
  if (options.output_type == "stdout") {
    if (options.debug_only) {
      auto printer = ConfigurePrinter(CreateStdoutPrinter<true>(), options);
      printer.Print(scene);
    } else {
      auto printer = ConfigurePrinter(CreateStdoutPrinter<false>(), options);
      printer.Print(scene);
    }
  } else if (options.output_type == "stderr") {
    if (options.debug_only) {
      auto printer = ConfigurePrinter(CreateStderrPrinter<true>(), options);
      printer.Print(scene);
    } else {
      auto printer = ConfigurePrinter(CreateStderrPrinter<false>(), options);
      printer.Print(scene);
    }
  } else if (options.output_type == "file") {
    std::ofstream file(options.output_file);
    if (!file) {
      std::cerr << "Error: Cannot open file '" << options.output_file << "'\n";
      return;
    }
    auto printer = ConfigurePrinter(CreateStreamPrinter(file), options);
    printer.Print(scene);
    std::cout << "Scene printed to file: " << options.output_file << "\n";
  } else if (options.output_type == "string") {
    auto printer = ConfigurePrinter(CreateStringPrinter(), options);
    auto result = printer.ToString(scene);
    std::cout << result;
  } else if (options.output_type == "logger") {
    if (options.debug_only) {
      auto printer = ConfigurePrinter(CreateLoggerPrinter<true>(), options);
      printer.Print(scene);
    } else {
      auto printer = ConfigurePrinter(CreateLoggerPrinter<false>(), options);
      printer.Print(scene);
    }
  } else {
    std::cerr << "Error: Unknown output type '" << options.output_type << "'\n";
  }
}

} // anonymous namespace

int main(int argc, char* argv[])
{
#ifdef _WIN32
  // Set console to UTF-8 mode for proper Unicode character display
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = false;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_header = false;
  loguru::g_global_verbosity = loguru::Verbosity_OFF;

  ExampleOptions options;

  if (!ParseArgs(argc, argv, options)) {
    return 1;
  }

  if (options.help) {
    PrintUsage(argv[0]);
    return 0;
  }
  // Print configuration
  std::cout << "=== ScenePrettyPrinter Example Configuration ===\n";

  // Show scene creation method
  if (!options.json_file.empty()) {
    std::cout << "Scene Source: JSON file (" << options.json_file << ")\n";
  } else if (!options.json_inline.empty()) {
    std::cout << "Scene Source: Inline JSON specification\n";
  } else {
    std::cout << "Scene Type: " << options.scene_type << "\n";
    std::cout << "Scene Size: " << options.scene_size << "\n";
    if (options.scene_type == "binary_tree" || options.scene_type == "forest") {
      std::cout << "Tree Depth: " << options.tree_depth << "\n";
    }
  }

  std::cout << "Scene Name: " << options.scene_name << "\n";
  std::cout << "Character Set: "
            << (options.charset == CharacterSet::kUnicode ? "unicode" : "ascii")
            << "\n";
  std::cout << "Verbosity: "
            << (options.verbosity == VerbosityLevel::kNone         ? "none"
                   : options.verbosity == VerbosityLevel::kCompact ? "compact"
                                                                   : "detailed")
            << "\n";
  std::cout << "Show Transforms: " << (options.show_transforms ? "yes" : "no")
            << "\n";
  std::cout << "Show Flags: " << (options.show_flags ? "yes" : "no") << "\n";
  std::cout << "Max Depth: "
            << (options.max_depth == -1 ? "unlimited"
                                        : std::to_string(options.max_depth))
            << "\n";
  std::cout << "Output Type: " << options.output_type << "\n";
  std::cout << "Debug Only: " << (options.debug_only ? "yes" : "no") << "\n";
  std::cout << "\n";

  // Create scene
  auto scene = CreateScene(options);
  if (!scene) {
    return 1;
  }

  std::cout << "=== Scene Graph Output ===\n";
  PrintScene(*scene, options);

  // Optionally show string output for comparison
  if (options.show_string_output && options.output_type != "string") {
    std::cout << "\n=== String Output (for comparison) ===\n";
    auto printer = ConfigurePrinter(CreateStringPrinter(), options);
    auto result = printer.ToString(*scene);
    std::cout << result << "\n";
  }

  return 0;
}
