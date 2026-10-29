//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <optional>
#include <ranges>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>

namespace oxygen::content::import {

namespace {

  namespace fs = std::filesystem;
  using nlohmann::json;

  constexpr std::string_view kBreaksNameRule
    = "it breaks the mount name rule of oxygen.mounts.schema.json";

  //! Where a mounts file stands: the `--mounts` file and the project's own
  //! may override committed paths; a parent file may not.
  enum class MountsFileLevel : uint8_t {
    kOption = 0,
    kProject,
    kParent,
  };

  [[nodiscard]] auto to_string(MountsFileLevel level) -> std::string_view
  {
    switch (level) {
    case MountsFileLevel::kOption:
      return "--mounts";
    case MountsFileLevel::kProject:
      return "project .mounts.json";
    case MountsFileLevel::kParent:
      return "parent .mounts.json";
    }
    return "Unknown";
  }

  //! Where a mount's folder comes from, before it is made canonical.
  struct Binding final {
    std::string path_text;
    fs::path base;
    MountOrigin origin = MountOrigin::kNone;
    fs::path origin_file;
  };

  struct MountsFileEntry final {
    std::string name;
    std::string path;
  };

  struct MountsFile final {
    fs::path file;
    MountOrigin origin = MountOrigin::kMountsFile;
    MountsFileLevel level = MountsFileLevel::kProject;
    std::vector<MountsFileEntry> entries;
    //! `"root": true`: no mounts file above this one is read.
    bool root = false;
    //! Found but refused with an error: it binds nothing.
    bool rejected = false;
  };

  struct ReadResult final {
    std::optional<MountsFile> file;
    bool stops_walk = false;
  };

  struct Entry final {
    Mount mount;
    std::optional<Binding> committed;
    std::optional<Binding> binding;
  };

  //! What a diagnostic is about, beyond its message.
  struct Subject final {
    fs::path file;
    std::string field;
    std::string mount;
  };

  //! Where a diagnostic points, for its log line.
  auto DescribeLocation(const Subject& subject) -> std::string
  {
    if (subject.file.empty()) {
      return {};
    }
    if (subject.field.empty()) {
      return fmt::format(" (in '{}')", subject.file.string());
    }
    return fmt::format(
      " (in '{}' at '{}')", subject.file.string(), subject.field);
  }

  //! The file or option that gave a mount its folder.
  auto DescribeOrigin(const MountTable& table, const Mount& mount)
    -> std::string
  {
    if (!mount.origin_file.empty()) {
      return mount.origin_file.string();
    }
    return table.declared_in;
  }

  auto ToVerbosity(const ImportSeverity severity) -> loguru::Verbosity
  {
    switch (severity) {
    case ImportSeverity::kError:
      return loguru::Verbosity_ERROR;
    case ImportSeverity::kWarning:
      return loguru::Verbosity_WARNING;
    case ImportSeverity::kInfo:
      break;
    }
    return loguru::Verbosity_INFO;
  }

  //! Formats each diagnostic from its code once, logs it at its severity in
  //! the current log scope, and collects it.
  class Reporter final {
  public:
    explicit Reporter(std::vector<ImportDiagnostic>& diagnostics)
      : diagnostics_(make_observer(&diagnostics))
    {
    }

    template <typename... Args>
    auto Report(const MountCode<Args...>& code, const Subject& subject,
      std::type_identity_t<Args>... facts) const -> void
    {
      // The format was checked against `Args` where the code is defined.
      auto message
        = fmt::vformat(code.message.get(), fmt::make_format_args(facts...));
      VLOG_F(ToVerbosity(code.severity), "[{}] {}{}", code.id, message,
        DescribeLocation(subject));
      diagnostics_->push_back(ImportDiagnostic {
        .severity = code.severity,
        .code = std::string { code.id },
        .message = std::move(message),
        .source_path = subject.file.string(),
        .object_path = subject.field,
        .object_name = subject.mount,
      });
    }

  private:
    observer_ptr<std::vector<ImportDiagnostic>> diagnostics_;
  };

  auto MountNamePattern() -> const std::regex&
  {
    static const auto pattern = std::regex {
      json::parse(kMountsSchema)
        .at("definitions")
        .at("mount_name")
        .at("pattern")
        .get<std::string>(),
    };
    return pattern;
  }

  auto FoldName(std::string_view name) -> std::string
  {
    auto out = std::string { name };
    std::ranges::transform(out, out.begin(), [](const char c) -> char {
      return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return out;
  }

  //! Path components compare ignoring case on Windows, as the file system
  //! reports elsewhere.
  auto FoldComponent(const fs::path& component) -> std::string
  {
#if defined(_WIN32)
    return FoldName(component.generic_string());
#else
    return component.generic_string();
#endif
  }

  auto IsAbsoluteText(const fs::path& path) -> bool
  {
    return path.has_root_name() || path.has_root_directory();
  }

  auto ToPath(std::string text) -> fs::path
  {
    std::ranges::replace(text, '\\', '/');
    return fs::path { text };
  }

  //! Resolved through the file system to its final path; a folder that does
  //! not exist yet goes through its deepest existing parent, and one with no
  //! existing parent keeps its text form.
  auto Canonical(const fs::path& path) -> fs::path
  {
    auto ec = std::error_code {};
    auto out = fs::weakly_canonical(path, ec);
    if (ec) {
      out = path.lexically_normal();
    }
    if (!out.has_filename() && out.has_relative_path()) {
      out = out.parent_path();
    }
    return out;
  }

  auto Components(const fs::path& path) -> std::vector<fs::path>
  {
    auto out = std::vector<fs::path> {};
    for (const auto& part : path) {
      if (!part.empty()) {
        out.push_back(part);
      }
    }
    return out;
  }

  //! The segments of `path` below `root`, or nothing when `path` is not
  //! `root` or below it. Both are canonical.
  auto SegmentsBelow(const fs::path& path, const fs::path& root)
    -> std::optional<std::vector<fs::path>>
  {
    const auto path_parts = Components(path);
    const auto root_parts = Components(root);
    if (path_parts.size() < root_parts.size()) {
      return std::nullopt;
    }
    for (const auto& [index, part] : std::views::enumerate(root_parts)) {
      if (FoldComponent(part)
        != FoldComponent(path_parts.at(static_cast<size_t>(index)))) {
        return std::nullopt;
      }
    }
    return std::vector<fs::path>(
      path_parts.begin() + static_cast<std::ptrdiff_t>(root_parts.size()),
      path_parts.end());
  }

  auto IsSameFolder(const fs::path& a, const fs::path& b) -> bool
  {
    return SegmentsBelow(a, b)
      .transform([](const auto& segments) -> bool { return segments.empty(); })
      .value_or(false);
  }

  //! Reports a name that breaks the name rule or is reserved; true when the
  //! name may be used.
  auto CheckName(std::string_view name, MountTableOwner owner,
    const Subject& subject, const Reporter& reporter) -> bool
  {
    if (!IsValidMountName(name)) {
      reporter.Report(
        mount_codes::kNameInvalid, subject, name, kBreaksNameRule);
      return false;
    }
    if (owner == MountTableOwner::kProject
      && FoldName(name) == FoldName(kEngineMountName)) {
      reporter.Report(mount_codes::kNameInvalid, subject, name,
        fmt::format(
          "'{}' is reserved for the engine content build", kEngineMountName));
      return false;
    }
    return true;
  }

  auto ParseMountsFile(const fs::path& path, const Reporter& reporter)
    -> std::optional<json>
  {
    const auto subject = Subject { .file = path, .field = {}, .mount = {} };
    auto stream = std::ifstream { path, std::ios::binary };
    if (!stream) {
      reporter.Report(mount_codes::kFileInvalid, subject, "it cannot be read");
      return std::nullopt;
    }
    auto doc = json::parse(stream, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
      reporter.Report(
        mount_codes::kFileInvalid, subject, "it is not a JSON object");
      return std::nullopt;
    }
    return doc;
  }

  auto CheckFileNames(const json& doc, const fs::path& path,
    MountTableOwner owner, const Reporter& reporter) -> bool
  {
    const auto mounts = doc.find("mounts");
    if (mounts == doc.end() || !mounts->is_object()) {
      return true;
    }
    auto valid = true;
    auto seen = std::unordered_map<std::string, std::string> {};
    for (const auto& item : mounts->items()) {
      const auto& name = item.key();
      const auto subject = Subject {
        .file = path,
        .field = "mounts." + name,
        .mount = name,
      };
      if (!CheckName(name, owner, subject, reporter)) {
        valid = false;
        continue;
      }
      const auto [it, inserted] = seen.try_emplace(FoldName(name), name);
      if (!inserted) {
        reporter.Report(mount_codes::kNameDuplicate, subject, name, it->second);
        valid = false;
      }
    }
    return valid;
  }

  auto ValidateFileSchema(
    const json& doc, const fs::path& path, const Reporter& reporter) -> bool
  {
    auto validator
      = internal::MakeJsonSchemaValidator(json::parse(kMountsSchema));
    auto issues = std::vector<internal::JsonSchemaIssue> {};
    auto internal_error = std::string {};
    if (!internal::CollectJsonSchemaIssues(
          validator, doc, issues, internal_error)) {
      reporter.Report(mount_codes::kFileInvalid,
        Subject { .file = path, .field = {}, .mount = {} },
        "the schema validator failed: " + internal_error);
      return false;
    }
    for (const auto& issue : issues) {
      reporter.Report(mount_codes::kFileInvalid,
        Subject { .file = path, .field = issue.object_path, .mount = {} },
        issue.message);
    }
    return issues.empty();
  }

  //! Reads one mounts file. A file with an error contributes no binding;
  //! `"root": true` still stops the walk.
  auto ReadMountsFile(const MountsFile& header, MountTableOwner owner,
    const Reporter& reporter) -> ReadResult
  {
    const auto doc = ParseMountsFile(header.file, reporter);
    if (!doc.has_value()) {
      return {};
    }
    const auto root = doc->find("root");
    const auto stops_walk
      = root != doc->end() && root->is_boolean() && root->get<bool>();

    const auto version = doc->find("version");
    if (version != doc->end() && version->is_number() && *version != 1) {
      reporter.Report(mount_codes::kFileVersion,
        Subject { .file = header.file, .field = "version", .mount = {} },
        version->dump());
      return { .file = std::nullopt, .stops_walk = stops_walk };
    }
    if (!CheckFileNames(*doc, header.file, owner, reporter)
      || !ValidateFileSchema(*doc, header.file, reporter)) {
      return { .file = std::nullopt, .stops_walk = stops_walk };
    }

    auto file = header;
    if (const auto mounts = doc->find("mounts"); mounts != doc->end()) {
      for (const auto& item : mounts->items()) {
        file.entries.push_back(MountsFileEntry {
          .name = item.key(),
          .path = item.value().get<std::string>(),
        });
      }
    }
    DLOG_F(1, "read {} file '{}': {} mount(s), root: {}", header.level,
      header.file.string(), file.entries.size(), stops_walk);
    file.root = stops_walk;
    return { .file = std::move(file), .stops_walk = stops_walk };
  }

  //! The mounts files of one run in precedence order: `--mounts`, the
  //! project's own, then parents nearest first, stopping after `"root": true`.
  auto CollectMountsFiles(const MountTableRequest& request,
    const fs::path& project_root, const Reporter& reporter)
    -> std::vector<MountsFile>
  {
    DLOG_SCOPE_F(1, "Search mounts files");
    auto files = std::vector<MountsFile> {};
    const auto read = [&](const MountsFile& header) -> bool {
      auto result = ReadMountsFile(header, request.owner, reporter);
      if (result.file.has_value()) {
        files.push_back(std::move(*result.file));
      } else {
        auto rejected = header;
        rejected.root = result.stops_walk;
        rejected.rejected = true;
        files.push_back(std::move(rejected));
      }
      if (result.stops_walk) {
        DLOG_F(1, "'root': true in '{}' stops the walk", header.file.string());
      }
      return result.stops_walk;
    };

    if (request.mounts_option.has_value()) {
      const auto option = request.mounts_option->is_absolute()
        ? *request.mounts_option
        : request.working_directory / *request.mounts_option;
      auto ec = std::error_code {};
      if (!fs::is_regular_file(option, ec)) {
        reporter.Report(mount_codes::kFileInvalid,
          Subject { .file = option, .field = {}, .mount = {} },
          "the file named by --mounts does not exist");
        files.push_back(MountsFile {
          .file = option.lexically_normal(),
          .origin = MountOrigin::kMountsOption,
          .level = MountsFileLevel::kOption,
          .entries = {},
          .root = false,
          .rejected = true,
        });
      } else if (read(MountsFile {
                   .file = option.lexically_normal(),
                   .origin = MountOrigin::kMountsOption,
                   .level = MountsFileLevel::kOption,
                   .entries = {},
                   .root = false,
                   .rejected = false,
                 })) {
        return files;
      }
    }

    auto ec = std::error_code {};
    auto dir = project_root;
    auto level = MountsFileLevel::kProject;
    while (true) {
      const auto candidate = dir / kMountsFileName;
      if (!fs::is_regular_file(candidate, ec)) {
        DLOG_F(1, "no {} file in '{}'", level, dir.string());
      } else if (read(MountsFile {
                   .file = candidate,
                   .origin = MountOrigin::kMountsFile,
                   .level = level,
                   .entries = {},
                   .root = false,
                   .rejected = false,
                 })) {
        break;
      }
      if (!dir.has_relative_path()) {
        break;
      }
      dir = dir.parent_path();
      level = MountsFileLevel::kParent;
    }
    return files;
  }

  class TableBuilder final {
  public:
    TableBuilder(const MountTableRequest& request, MountTable& table)
      : request_(make_observer(&request))
      , table_(make_observer(&table))
      , reporter_(table.diagnostics)
    {
    }

    [[nodiscard]] auto GetReporter() const -> const Reporter&
    {
      return reporter_;
    }

    auto Declare() -> void
    {
      const auto base = request_->form == MountDeclarationForm::kCommittedFile
        ? table_->project_root
        : request_->working_directory;
      for (const auto& [index, declaration] :
        std::views::enumerate(request_->declarations)) {
        const auto field = fmt::format("mounts[{}]", index);
        const auto name_subject = Subject {
          .file = DeclaredIn(),
          .field = field + ".name",
          .mount = declaration.name,
        };
        if (!CheckName(
              declaration.name, request_->owner, name_subject, reporter_)) {
          continue;
        }
        if (const auto existing = by_name_.find(FoldName(declaration.name));
          existing != by_name_.end()) {
          reporter_.Report(mount_codes::kNameDuplicate, name_subject,
            declaration.name, entries_.at(existing->second).mount.name);
          continue;
        }
        auto entry = Entry {
          .mount = Mount {
            .name = declaration.name,
            .kind = declaration.kind,
            .scope = MountScope::kDeclared,
            .declared_path = declaration.path,
            .folder = {},
            .origin = MountOrigin::kNone,
            .origin_file = {},
            .state = MountState::kUnbound,
          },
          .committed = std::nullopt,
          .binding = std::nullopt,
        };
        if (declaration.path.has_value()) {
          const auto path_field = field + ".path";
          if (request_->form == MountDeclarationForm::kCommittedFile
            && IsAbsoluteText(ToPath(*declaration.path))) {
            reporter_.Report(mount_codes::kAbsoluteCommitted,
              Subject {
                .file = DeclaredIn(),
                .field = path_field,
                .mount = declaration.name,
              },
              *declaration.path);
          } else {
            entry.committed = Binding {
              .path_text = *declaration.path,
              .base = base,
              .origin = MountOrigin::kCommitted,
              .origin_file = {},
            };
          }
        }
        Add(std::move(entry));
      }
    }

    auto Apply(const MountsFile& file) -> void
    {
      if (file.entries.empty()) {
        return;
      }
      DLOG_SCOPE_F(1, "Apply '{}'", file.file.string());
      for (const auto& item : file.entries) {
        const auto subject = Subject {
          .file = file.file,
          .field = "mounts." + item.name,
          .mount = item.name,
        };
        const auto binding = Binding {
          .path_text = item.path,
          .base = file.file.parent_path(),
          .origin = file.origin,
          .origin_file = file.file,
        };
        const auto existing = by_name_.find(FoldName(item.name));
        if (existing == by_name_.end()) {
          DLOG_F(1, "{}: local mount -> '{}'", item.name, item.path);
          Add(Entry {
            .mount = Mount {
              .name = item.name,
              .kind = MountKind::kSource,
              .scope = MountScope::kLocal,
              .declared_path = std::nullopt,
              .folder = {},
              .origin = MountOrigin::kNone,
              .origin_file = {},
              .state = MountState::kUnbound,
            },
            .committed = std::nullopt,
            .binding = binding,
          });
          continue;
        }
        auto& entry = entries_.at(existing->second);
        if (entry.mount.name != item.name) {
          reporter_.Report(
            mount_codes::kNameDuplicate, subject, item.name, entry.mount.name);
          continue;
        }
        if (file.level == MountsFileLevel::kParent
          && entry.mount.declared_path.has_value()) {
          reporter_.Report(mount_codes::kParentIgnored, subject, item.name);
          continue;
        }
        if (entry.binding.has_value()) {
          DLOG_F(1, "{}: '{}' not used, already bound by {} '{}'", item.name,
            item.path, entry.binding->origin,
            entry.binding->origin_file.string());
          continue;
        }
        DLOG_F(1, "{}: bound -> '{}'", item.name, item.path);
        entry.binding = binding;
      }
    }

    auto ApplyCommitted() -> void
    {
      if (entries_.empty()) {
        return;
      }
      DLOG_SCOPE_F(1, "Apply the committed declaration");
      for (auto& entry : entries_) {
        if (!entry.committed.has_value()) {
          if (entry.mount.scope == MountScope::kLocal) {
            continue;
          }
          if (entry.mount.declared_path.has_value()) {
            DLOG_F(1, "{}: committed path '{}' refused", entry.mount.name,
              *entry.mount.declared_path);
          } else if (entry.binding.has_value()) {
            DLOG_F(1, "{}: placeholder, bound by {} '{}'", entry.mount.name,
              entry.binding->origin, entry.binding->origin_file.string());
          } else {
            DLOG_F(1, "{}: placeholder, unbound so far", entry.mount.name);
          }
          continue;
        }
        if (entry.binding.has_value()) {
          DLOG_F(1, "{}: committed '{}' overridden by {} '{}'",
            entry.mount.name, entry.committed->path_text, entry.binding->origin,
            entry.binding->origin_file.string());
          continue;
        }
        DLOG_F(
          1, "{}: bound -> '{}'", entry.mount.name, entry.committed->path_text);
        entry.binding = entry.committed;
      }
    }

    auto Finish() -> void
    {
      for (auto& entry : entries_) {
        Bind(entry);
        table_->mounts.push_back(std::move(entry.mount));
      }
      CheckSourceFolders();
    }

  private:
    [[nodiscard]] auto DeclaredIn() const -> fs::path
    {
      return fs::path { request_->declared_in };
    }

    [[nodiscard]] auto DescribeOrigin(const Mount& mount) const -> std::string
    {
      return import::DescribeOrigin(*table_, mount);
    }

    auto Add(Entry entry) -> void
    {
      by_name_.emplace(FoldName(entry.mount.name), entries_.size());
      entries_.push_back(std::move(entry));
    }

    auto Bind(Entry& entry) -> void
    {
      auto& mount = entry.mount;
      if (!entry.binding.has_value()) {
        // A committed path refused as absolute was reported where it was
        // declared; only a placeholder is reported unbound.
        if (mount.declared_path.has_value()) {
          return;
        }
        reporter_.Report(mount_codes::kUnbound,
          Subject { .file = DeclaredIn(), .field = {}, .mount = mount.name },
          mount.name);
        return;
      }
      const auto& binding = *entry.binding;
      const auto text = ToPath(binding.path_text);
      mount.folder
        = Canonical(IsAbsoluteText(text) ? text : binding.base / text);
      mount.origin = binding.origin;
      mount.origin_file = binding.origin_file;
      const auto subject = Subject {
        .file = DescribeOrigin(mount),
        .field = {},
        .mount = mount.name,
      };

      auto ec = std::error_code {};
      const auto status = fs::status(mount.folder, ec);
      if (!fs::exists(status)) {
        mount.state = MountState::kMissing;
        reporter_.Report(mount_codes::kFolderMissing, subject, mount.name,
          mount.folder.string());
        return;
      }
      mount.state = MountState::kBound;
      const auto is_folder = fs::is_directory(status);
      if ((mount.kind == MountKind::kPak) == is_folder) {
        reporter_.Report(mount_codes::kPathKind, subject, to_string(mount.kind),
          mount.name, is_folder ? "folder" : "file", mount.folder.string());
      }
    }

    auto CheckSourceFolders() -> void
    {
      auto sources = std::vector<observer_ptr<const Mount>> {};
      for (const auto& mount : table_->mounts) {
        if (mount.kind == MountKind::kSource && !mount.folder.empty()) {
          sources.push_back(make_observer(&mount));
        }
      }
      for (const auto& [index, mount] : std::views::enumerate(sources)) {
        CheckOutputFolder(*mount);
        for (const auto& other :
          std::span(sources).subspan(static_cast<size_t>(index) + 1)) {
          if (SegmentsBelow(mount->folder, other->folder).has_value()
            || SegmentsBelow(other->folder, mount->folder).has_value()) {
            reporter_.Report(mount_codes::kFolderOverlap,
              Subject {
                .file = DescribeOrigin(*other),
                .field = {},
                .mount = other->name,
              },
              other->name, other->folder.string(), mount->name,
              mount->folder.string(), DescribeOrigin(*mount));
          }
        }
      }
    }

    auto CheckOutputFolder(const Mount& mount) -> void
    {
      const auto segments = SegmentsBelow(mount.folder, table_->project_root);
      if (!segments.has_value()) {
        return;
      }
      const auto in_output
        = std::ranges::any_of(*segments, [](const fs::path& segment) -> bool {
            return segment.string().starts_with('.');
          });
      if (in_output) {
        reporter_.Report(mount_codes::kFolderInOutput,
          Subject {
            .file = DescribeOrigin(mount),
            .field = {},
            .mount = mount.name,
          },
          mount.name, mount.folder.string());
      }
    }

    observer_ptr<const MountTableRequest> request_;
    observer_ptr<MountTable> table_;
    Reporter reporter_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> by_name_;
  };

  //! What a mounts file did to a declared mount: replaced its committed path
  //! or bound its placeholder.
  auto DescribeBinding(const Mount& mount) -> std::string
  {
    if (mount.scope != MountScope::kDeclared
      || mount.origin == MountOrigin::kCommitted) {
      return {};
    }
    if (mount.declared_path.has_value()) {
      return fmt::format(", overriding committed '{}'", *mount.declared_path);
    }
    return ", binding a placeholder";
  }

  //! The mounts files of this run, in precedence order.
  auto LogMountsFiles(const std::span<const MountsFile> files) -> void
  {
    if (files.empty()) {
      LOG_F(INFO, "Mounts files found: none");
      return;
    }
    LOG_SCOPE_F(INFO, "Mounts files found: {}", files.size());
    for (const auto& file : files) {
      if (file.rejected) {
        LOG_F(INFO, "'{}' ({}, rejected: binds nothing{})", file.file.string(),
          file.level, file.root ? ", root: stops the walk" : "");
        continue;
      }
      LOG_F(INFO, "'{}' ({}, mounts: {}{})", file.file.string(), file.level,
        file.entries.size(), file.root ? ", root: stops the walk" : "");
    }
  }

  //! The calculated values of one run: logged, never stored.
  auto LogTable(const MountTable& table) -> void
  {
    const auto count = [&](const ImportSeverity severity) -> std::ptrdiff_t {
      return std::ranges::count(
        table.diagnostics, severity, &ImportDiagnostic::severity);
    };
    const auto errors = count(ImportSeverity::kError);
    const auto warnings = count(ImportSeverity::kWarning);
    if (table.mounts.empty()) {
      LOG_F(INFO, "Mount table: no mounts (errors: {}, warnings: {})", errors,
        warnings);
      return;
    }
    LOG_SCOPE_F(INFO, "Mount table (mounts: {}, errors: {}, warnings: {})",
      table.mounts.size(), errors, warnings);
    for (const auto& mount : table.mounts) {
      if (mount.state == MountState::kUnbound) {
        LOG_F(INFO, "{} [{} {}] unbound: {}", mount.name, mount.kind,
          mount.scope,
          mount.declared_path.has_value() ? "committed path refused"
                                          : "placeholder nothing binds");
        continue;
      }
      LOG_F(INFO, "{} [{} {}] {} '{}' from {} '{}'{}", mount.name, mount.kind,
        mount.scope, mount.state, mount.folder.string(), mount.origin,
        DescribeOrigin(table, mount), DescribeBinding(mount));
    }
  }

} // namespace

auto MountTable::IsValid() const -> bool
{
  return std::ranges::none_of(
    diagnostics, [](const ImportDiagnostic& diagnostic) -> bool {
      return diagnostic.severity == ImportSeverity::kError;
    });
}

auto MountTable::Find(const std::string_view name) const
  -> observer_ptr<const Mount>
{
  const auto it = std::ranges::find(mounts, name, &Mount::name);
  return it == mounts.end() ? observer_ptr<const Mount> {}
                            : make_observer(&*it);
}

auto IsValidMountName(const std::string_view name) -> bool
{
  return std::regex_match(name.begin(), name.end(), MountNamePattern());
}

auto ResolveMountTable(const MountTableRequest& request) -> MountTable
{
  auto table = MountTable {
    .project_root = Canonical(request.project_root.is_absolute()
        ? request.project_root
        : request.working_directory / request.project_root),
    .declared_in = request.declared_in,
    .mounts = {},
    .diagnostics = {},
  };
  LOG_SCOPE_F(INFO, "Resolve mounts of '{}' ({} declared in '{}')",
    table.project_root.string(), request.declarations.size(),
    request.declared_in);

  auto builder = TableBuilder { request, table };
  builder.Declare();
  const auto files
    = CollectMountsFiles(request, table.project_root, builder.GetReporter());
  LogMountsFiles(files);
  const auto is_parent = [](const MountsFile& file) -> bool {
    return file.level == MountsFileLevel::kParent;
  };
  for (const auto& file : files | std::views::filter(std::not_fn(is_parent))) {
    builder.Apply(file);
  }
  builder.ApplyCommitted();
  for (const auto& file : files | std::views::filter(is_parent)) {
    builder.Apply(file);
  }
  builder.Finish();

  LogTable(table);
  return table;
}

auto CheckMountNameConflicts(const std::span<const MountTable> tables)
  -> std::vector<ImportDiagnostic>
{
  //! A mount and the table that holds it.
  struct Use final {
    observer_ptr<const MountTable> table;
    observer_ptr<const Mount> mount;
  };
  struct Conflict final {
    Use first;
    Use other;
  };

  auto first = std::unordered_map<std::string, Use> {};
  auto conflicts = std::vector<Conflict> {};
  for (const auto& table : tables) {
    for (const auto& mount : table.mounts) {
      if (mount.folder.empty()) {
        continue;
      }
      const auto use = Use {
        .table = make_observer(&table),
        .mount = make_observer(&mount),
      };
      const auto [it, inserted] = first.try_emplace(FoldName(mount.name), use);
      if (!inserted && !IsSameFolder(mount.folder, it->second.mount->folder)) {
        conflicts.push_back(Conflict { .first = it->second, .other = use });
      }
    }
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  if (conflicts.empty()) {
    LOG_F(INFO, "Mount names agree across {} mount tables", tables.size());
    return diagnostics;
  }
  LOG_SCOPE_F(
    INFO, "Mount name conflicts across {} mount tables", tables.size());
  const auto reporter = Reporter { diagnostics };
  for (const auto& [earlier, other] : conflicts) {
    const auto& mount = *other.mount;
    reporter.Report(mount_codes::kNameConflict,
      Subject {
        .file = DescribeOrigin(*other.table, mount),
        .field = {},
        .mount = mount.name,
      },
      mount.name, mount.folder.string(), earlier.mount->folder.string(),
      DescribeOrigin(*earlier.table, *earlier.mount));
  }
  return diagnostics;
}

} // namespace oxygen::content::import
