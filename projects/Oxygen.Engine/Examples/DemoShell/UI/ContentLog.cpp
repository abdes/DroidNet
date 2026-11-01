//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "DemoShell/UI/ContentLog.h"
#include "DemoShell/UI/ContentTextMatch.h"
#include <fmt/chrono.h> // IWYU pragma: keep
#include <fmt/format.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>

namespace oxygen::examples::ui {

auto ContentLog::Add(ContentLogEntry entry) -> std::uint64_t
{
  entry.id = next_id_++;
  entry.time = std::chrono::system_clock::now();
  if (entry.severity == LogSeverity::kError) {
    ++unseen_errors_;
  }
  entries_.push_back(std::move(entry));
  return entries_.back().id;
}

auto ContentLog::AddDiagnostic(
  const content::import::ImportDiagnostic& diagnostic) -> std::uint64_t
{
  return Add(ContentLogEntry {
    .severity = diagnostic.severity,
    .code = diagnostic.code,
    .message = diagnostic.message,
    .asset = diagnostic.object_path.empty() ? diagnostic.object_name
                                            : diagnostic.object_path,
    .path = diagnostic.source_path,
  });
}

auto ContentLog::Clear() -> void
{
  entries_.clear();
  unseen_errors_ = 0;
}

auto ContentLog::Find(const std::uint64_t id) const -> const ContentLogEntry*
{
  const auto found = std::ranges::find(entries_, id, &ContentLogEntry::id);
  return found == entries_.end() ? nullptr : &*found;
}

auto ContentLog::Count(const LogSeverity severity) const -> std::size_t
{
  return static_cast<std::size_t>(
    std::ranges::count(entries_, severity, &ContentLogEntry::severity));
}

auto ContentLog::Matches(const ContentLogEntry& entry, const LogFilter& filter)
  -> bool
{
  switch (entry.severity) {
  case LogSeverity::kError:
    if (!filter.errors) {
      return false;
    }
    break;
  case LogSeverity::kWarning:
    if (!filter.warnings) {
      return false;
    }
    break;
  case LogSeverity::kInfo:
    if (!filter.info) {
      return false;
    }
    break;
  }
  if (filter.text.empty()) {
    return true;
  }
  const auto text = TextFilter { filter.text };
  return text.Matches(entry.code) || text.Matches(entry.message)
    || text.Matches(entry.path) || text.Matches(entry.asset)
    || text.Matches(entry.scene);
}

auto ContentLog::Text(const LogFilter& filter) const -> std::string
{
  auto text = std::string {};
  for (const auto& entry : entries_) {
    if (!Matches(entry, filter)) {
      continue;
    }
    text += fmt::format("{:%H:%M:%S} [{}] {}: {}\n",
      std::chrono::floor<std::chrono::seconds>(entry.time),
      nostd::to_string(entry.severity), entry.code, entry.message);
  }
  return text;
}

} // namespace oxygen::examples::ui
