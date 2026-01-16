//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/EngineTag.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::internal {

auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }

} // namespace oxygen::content::internal

namespace {

using oxygen::clap::Cli;
using oxygen::clap::CliBuilder;
using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;
using oxygen::clap::Option;

using oxygen::data::AssetKey;
using oxygen::data::loose_cooked::v1::FileKind;

constexpr std::string_view kProgramName = "Oxygen.Content.Inspector";
constexpr std::string_view kVersion = "0.1";

struct ValidateOptions {
  std::string cooked_root;
  bool quiet = false;
};

struct DumpOptions {
  std::string cooked_root;
  bool assets = false;
  bool files = false;
  bool show_digests = false;
};

struct DumpResourceOptions {
  std::string cooked_root;
};

auto AssetTypeToString(const uint8_t asset_type) -> std::string_view
{
  using oxygen::data::AssetType;

  const auto max = static_cast<uint8_t>(AssetType::kMaxAssetType);
  if (asset_type > max) {
    return "unknown";
  }

  return nostd::to_string(static_cast<AssetType>(asset_type));
}

auto FileKindToString(const FileKind kind) -> std::string_view
{
  switch (kind) {
  case FileKind::kBuffersTable:
    return "buffers.table";
  case FileKind::kBuffersData:
    return "buffers.data";
  case FileKind::kTexturesTable:
    return "textures.table";
  case FileKind::kTexturesData:
    return "textures.data";
  case FileKind::kUnknown:
  default:
    return "unknown";
  }
}

auto IsAllZero(
  const std::array<uint8_t, oxygen::data::loose_cooked::v1::kSha256Size>&
    digest) -> bool
{
  for (const auto b : digest) {
    if (b != 0) {
      return false;
    }
  }
  return true;
}

auto DumpHexSha256(std::ostream& os,
  const std::span<const uint8_t, oxygen::data::loose_cooked::v1::kSha256Size>&
    digest) -> void
{
  static constexpr char kHex[] = "0123456789abcdef";
  for (const auto b : digest) {
    os << kHex[(b >> 4) & 0x0f] << kHex[b & 0x0f];
  }
}

auto ToHex64(const uint64_t value) -> std::string
{
  std::ostringstream ss;
  ss << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return ss.str();
}

auto FindFileRelPath(const oxygen::content::LooseCookedInspection& inspection,
  const FileKind kind) -> std::optional<std::string>
{
  for (const auto& entry : inspection.Files()) {
    if (entry.kind == kind) {
      return entry.relpath;
    }
  }
  return std::nullopt;
}

template <typename T>
auto LoadPackedTable(const std::filesystem::path& table_path) -> std::vector<T>
{
  using oxygen::serio::FileStream;
  using oxygen::serio::Reader;

  FileStream<> stream(table_path, std::ios::in);
  const auto size_result = stream.Size();
  if (!size_result) {
    throw std::runtime_error("failed to get table size");
  }

  const auto size_bytes = size_result.value();
  if (size_bytes == 0) {
    return {};
  }

  if (size_bytes % sizeof(T) != 0) {
    throw std::runtime_error("table size is not a multiple of entry size");
  }

  const auto count = size_bytes / sizeof(T);
  std::vector<T> entries(count);

  Reader<FileStream<>> reader(stream);
  const auto pack = reader.ScopedAlignment(1);
  auto read_result = reader.ReadBlobInto(
    std::as_writable_bytes(std::span<T>(entries.data(), entries.size())));
  if (!read_result) {
    throw std::runtime_error("failed to read table file");
  }

  return entries;
}

auto DumpFileRecords(
  const std::span<const oxygen::content::LooseCookedInspection::FileEntry>
    entries,
  std::ostream& os) -> void
{
  if (entries.empty()) {
    os << "(none)\n";
    return;
  }

  os << "Kind             Path                                Size\n";
  os << "---------------  ----------------------------------  ----------\n";

  for (const auto& e : entries) {
    os << std::left << std::setw(15) << FileKindToString(e.kind) << "  ";
    os << std::left << std::setw(34) << e.relpath << "  ";
    os << std::right << std::setw(10) << e.size;
    os << "\n";
  }
}

auto DumpAssets(
  const std::span<const oxygen::content::LooseCookedInspection::AssetEntry>
    entries,
  std::ostream& os, const bool show_digests) -> void
{
  if (entries.empty()) {
    os << "(none)\n";
    return;
  }

  for (const auto& e : entries) {
    os << "- key='" << oxygen::data::to_string(e.key) << "'";
    os << " type='" << AssetTypeToString(e.asset_type) << "'(";
    os << static_cast<unsigned>(e.asset_type) << ")'";

    if (!e.virtual_path.empty()) {
      os << " vpath='" << e.virtual_path << "'";
    }

    if (!e.descriptor_relpath.empty()) {
      os << " desc='" << e.descriptor_relpath << "'";
    }

    os << " desc_size=" << e.descriptor_size;

    if (show_digests) {
      if (e.descriptor_sha256) {
        os << " desc_sha256=";
        DumpHexSha256(os,
          std::span<const uint8_t, oxygen::data::loose_cooked::v1::kSha256Size>(
            e.descriptor_sha256->data(), e.descriptor_sha256->size()));
      }
    }

    os << "\n";
  }
}

auto ValidateRootOrThrow(const std::filesystem::path& cooked_root) -> void
{
  using oxygen::content::internal::EngineTagFactory;

  oxygen::content::AssetLoader loader(EngineTagFactory::Get());
  loader.AddLooseCookedRoot(cooked_root);
}

auto RunValidate(const ValidateOptions& opts) -> int
{
  const std::filesystem::path cooked_root(opts.cooked_root);

  try {
    ValidateRootOrThrow(cooked_root);
    if (!opts.quiet) {
      std::cout << "OK: valid loose cooked root: " << cooked_root.string()
                << "\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto RunDumpIndex(const DumpOptions& opts) -> int
{
  const std::filesystem::path cooked_root(opts.cooked_root);

  try {
    oxygen::content::LooseCookedInspection inspection;
    inspection.LoadFromRoot(cooked_root);

    const bool dump_assets = opts.assets || (!opts.assets && !opts.files);
    const bool dump_files = opts.files || (!opts.assets && !opts.files);

    std::cout << "Cooked Root : " << cooked_root.string() << "\n";
    std::cout << "Index GUID  : " << nostd::to_string(inspection.Guid())
              << "\n";

    if (dump_assets) {
      std::cout << "\nAssets (" << inspection.Assets().size() << ")\n";
      DumpAssets(inspection.Assets(), std::cout, opts.show_digests);
    }

    if (dump_files) {
      std::cout << "\nFile Records (" << inspection.Files().size() << ")\n";
      DumpFileRecords(inspection.Files(), std::cout);
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto RunDumpBuffers(const DumpResourceOptions& opts) -> int
{
  const std::filesystem::path cooked_root(opts.cooked_root);

  try {
    oxygen::content::LooseCookedInspection inspection;
    inspection.LoadFromRoot(cooked_root);

    const auto relpath = FindFileRelPath(inspection, FileKind::kBuffersTable);
    if (!relpath) {
      std::cerr << "ERROR: buffers.table not found in index\n";
      return 2;
    }

    const auto table_path = cooked_root / *relpath;
    auto entries
      = LoadPackedTable<oxygen::data::pak::BufferResourceDesc>(table_path);

    std::cout << "Dumping " << entries.size() << " buffers in: '"
              << table_path.string() << "'\n\n";

    if (entries.empty()) {
      std::cout << "(none)\n";
      return 0;
    }

    // clang-format off
    std::cout << "Idx  Offset              Size       Stride  Format          Usage Flags                      Hash\n";
    std::cout << "---- ------------------- ---------- ------ --------------- --------------------------------  ----------------\n";
    // clang-format on

    for (size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      const auto format_name
        = nostd::to_string(static_cast<oxygen::Format>(e.element_format));
      const auto usage_name = nostd::to_string(
        static_cast<oxygen::data::BufferResource::UsageFlags>(e.usage_flags));

      std::cout << std::right << std::setw(3) << i << "  ";
      std::cout << std::left << std::setw(19) << ToHex64(e.data_offset) << " ";
      std::cout << std::right << std::setw(10) << e.size_bytes << " ";
      std::cout << std::right << std::setw(6) << e.element_stride << " ";
      std::cout << std::left << std::setw(15) << format_name << " ";
      std::cout << std::left << std::setw(32) << usage_name << " ";
      std::cout << std::left << std::setw(16) << ToHex64(e.content_hash)
                << "\n";
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto RunDumpTextures(const DumpResourceOptions& opts) -> int
{
  const std::filesystem::path cooked_root(opts.cooked_root);

  try {
    oxygen::content::LooseCookedInspection inspection;
    inspection.LoadFromRoot(cooked_root);

    const auto relpath = FindFileRelPath(inspection, FileKind::kTexturesTable);
    if (!relpath) {
      std::cerr << "ERROR: textures.table not found in index\n";
      return 2;
    }

    const auto table_path = cooked_root / *relpath;
    auto entries
      = LoadPackedTable<oxygen::data::pak::TextureResourceDesc>(table_path);

    std::cout << "Dumping " << entries.size() << " textures in: '"
              << table_path.string() << "'\n\n";

    if (entries.empty()) {
      std::cout << "(none)\n";
      return 0;
    }

    // clang-format off
    std::cout << "Idx  Offset              Size       Dims        Mips Layers Type           Format          Hash\n";
    std::cout << "---- ------------------- ---------- ----------- ---- ------ -------------- --------------- ----------------\n";
    // clang-format on

    for (size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      const auto type_name
        = nostd::to_string(static_cast<oxygen::TextureType>(e.texture_type));
      const auto format_name
        = nostd::to_string(static_cast<oxygen::Format>(e.format));
      std::ostringstream dims;
      dims << e.width << "x" << e.height;

      std::cout << std::right << std::setw(3) << i << "  ";
      std::cout << std::left << std::setw(19) << ToHex64(e.data_offset) << " ";
      std::cout << std::right << std::setw(10) << e.size_bytes << " ";
      std::cout << std::left << std::setw(11) << dims.str() << " ";
      std::cout << std::right << std::setw(4) << e.mip_levels << " ";
      std::cout << std::right << std::setw(6) << e.array_layers << " ";
      std::cout << std::left << std::setw(14) << type_name << " ";
      std::cout << std::left << std::setw(15) << format_name << " ";
      std::cout << std::left << std::setw(16) << ToHex64(e.content_hash)
                << "\n";
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto BuildCli(ValidateOptions& validate_opts, DumpOptions& dump_opts,
  DumpResourceOptions& buffers_opts, DumpResourceOptions& textures_opts)
  -> std::unique_ptr<Cli>
{
  auto validate_root = Option::Positional("cooked_root")
                         .About("Loose cooked root directory")
                         .Required()
                         .WithValue<std::string>()
                         .StoreTo(&validate_opts.cooked_root)
                         .Build();

  auto validate_quiet = Option::WithKey("quiet")
                          .About("Do not print on success")
                          .Short("q")
                          .Long("quiet")
                          .WithValue<bool>()
                          .StoreTo(&validate_opts.quiet)
                          .Build();

  const std::shared_ptr<Command> validate_cmd
    = CommandBuilder("validate")
        .About("Validate a loose cooked root (index + files).")
        .WithPositionalArguments(validate_root)
        .WithOption(std::move(validate_quiet));

  auto dump_root = Option::Positional("cooked_root")
                     .About("Loose cooked root directory")
                     .Required()
                     .WithValue<std::string>()
                     .StoreTo(&dump_opts.cooked_root)
                     .Build();

  auto dump_assets = Option::WithKey("assets")
                       .About("Dump asset entries")
                       .Long("assets")
                       .WithValue<bool>()
                       .StoreTo(&dump_opts.assets)
                       .Build();

  auto dump_files = Option::WithKey("files")
                      .About("Dump file records")
                      .Long("files")
                      .WithValue<bool>()
                      .StoreTo(&dump_opts.files)
                      .Build();

  auto dump_digests = Option::WithKey("digests")
                        .About("Include SHA-256 digests")
                        .Long("digests")
                        .WithValue<bool>()
                        .StoreTo(&dump_opts.show_digests)
                        .Build();

  const std::shared_ptr<Command> dump_cmd
    = CommandBuilder("index")
        .About("Dump container.index.bin contents (validated).")
        .WithPositionalArguments(dump_root)
        .WithOption(std::move(dump_assets))
        .WithOption(std::move(dump_files))
        .WithOption(std::move(dump_digests));

  auto buffers_root = Option::Positional("cooked_root")
                        .About("Loose cooked root directory")
                        .Required()
                        .WithValue<std::string>()
                        .StoreTo(&buffers_opts.cooked_root)
                        .Build();

  const std::shared_ptr<Command> buffers_cmd
    = CommandBuilder("buffers")
        .About("Dump buffers.table entries.")
        .WithPositionalArguments(buffers_root);

  auto textures_root = Option::Positional("cooked_root")
                         .About("Loose cooked root directory")
                         .Required()
                         .WithValue<std::string>()
                         .StoreTo(&textures_opts.cooked_root)
                         .Build();

  const std::shared_ptr<Command> textures_cmd
    = CommandBuilder("textures")
        .About("Dump textures.table entries.")
        .WithPositionalArguments(textures_root);

  return CliBuilder()
    .ProgramName(std::string(kProgramName))
    .Version(std::string(kVersion))
    .About(
      "Inspect and validate loose cooked content roots (filesystem-backed).")
    .WithHelpCommand()
    .WithVersionCommand()
    .WithCommand(validate_cmd)
    .WithCommand(dump_cmd)
    .WithCommand(buffers_cmd)
    .WithCommand(textures_cmd)
    .Build();
}

} // namespace

auto main(int argc, char** argv) -> int
{
  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = false;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = true;
  loguru::g_preamble_header = false;
  loguru::g_stderr_verbosity = loguru::Verbosity_OFF;

  loguru::init(argc, const_cast<const char**>(argv));
  loguru::set_thread_name("main");

  int exit_code = 0;
  try {
    ValidateOptions validate_opts;
    DumpOptions dump_opts;
    DumpResourceOptions buffers_opts;
    DumpResourceOptions textures_opts;

    const auto cli
      = BuildCli(validate_opts, dump_opts, buffers_opts, textures_opts);
    const auto context = cli->Parse(argc, const_cast<const char**>(argv));

    const auto command_path = context.active_command->PathAsString();
    const auto& ovm = context.ovm;

    if (command_path == Command::VERSION || command_path == Command::HELP
      || ovm.HasOption(Command::HELP)) {
      exit_code = 0;
    } else if (command_path == "validate") {
      exit_code = RunValidate(validate_opts);
    } else if (command_path == "index") {
      exit_code = RunDumpIndex(dump_opts);
    } else if (command_path == "buffers") {
      exit_code = RunDumpBuffers(buffers_opts);
    } else if (command_path == "textures") {
      exit_code = RunDumpTextures(textures_opts);
    } else {
      std::cerr << "ERROR: Unknown command\n";
      exit_code = 1;
    }
  } catch (const std::exception& /*ex*/) {
    // The error is already printed by the CLI parser.
    exit_code = 3;
  }

  loguru::flush();
  loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
  loguru::shutdown();

  return exit_code;
}
