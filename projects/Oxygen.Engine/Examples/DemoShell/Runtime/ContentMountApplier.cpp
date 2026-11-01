//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include "DemoShell/Runtime/ContentMountApplier.h"
#include "DemoShell/Runtime/PathNormalization.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/DependencyRequirementError.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::examples::runtime {

auto ApplyContentMounts(content::IAssetLoader& loader,
  std::vector<content::ContentMount> mounts) -> co::Co<MountOutcome>
{
  auto outcome = MountOutcome {};
  // Each pass either commits or leaves out one refused pak, so the loop ends.
  while (true) {
    std::optional<content::DependencyRequirementError> refusal;
    try {
      auto prepared = co_await loader.PrepareMountsAsync(mounts);
      auto retirement = loader.CommitPreparedMounts(std::move(prepared));
      retirement.Finish();
      outcome.mounted = std::move(mounts);
      co_return outcome;
    } catch (const content::DependencyRequirementError& error) {
      refusal = error;
    } catch (const content::OperationCancelledException& error) {
      LOG_F(
        INFO, "Content mounts changed during preparation: {}", error.what());
      outcome.retry = true;
      co_return outcome;
    } catch (const std::system_error& error) {
      if (error.code() == std::errc::device_or_resource_busy) {
        outcome.retry = true;
      } else {
        outcome.error = error.what();
      }
      co_return outcome;
    } catch (const std::exception& error) {
      outcome.error = error.what();
      co_return outcome;
    }

    const auto refused_pak = NormalizePath(refusal->Pak());
    const auto removed = std::erase_if(
      mounts, [&refused_pak](const content::ContentMount& mount) -> bool {
        return mount.kind == content::ContentMount::Kind::kPakFile
          && NormalizePath(mount.path) == refused_pak;
      });
    if (removed == 0) {
      // A pak outside the list, the engine pak, refused everything.
      outcome.error = refusal->what();
      co_return outcome;
    }
    LOG_F(WARNING, "{}", refusal->what());
    outcome.refused.push_back(RefusedMount {
      .pak = refusal->Pak(),
      .failures = refusal->Failures(),
      .message = refusal->what(),
    });
  }
}

auto MountGeneration(content::IAssetLoader& loader,
  const GenerationPublication& publication) -> void
{
  const auto selected = NormalizePath(publication.cooked_root);
  const auto replaced = publication.previous_root
    ? std::optional { NormalizePath(*publication.previous_root) }
    : std::nullopt;
  std::optional<data::SourceKey> previous;
  std::vector<data::SourceKey> older;
  bool selected_is_mounted = false;
  for (const auto& source : loader.EnumerateMountedSources()) {
    if (source.source_kind
      != content::IAssetLoader::ContentSourceKind::kLooseCooked) {
      continue;
    }
    const auto mounted = NormalizePath(source.source_path);
    if (mounted == selected) {
      selected_is_mounted = true;
    } else if (mounted == replaced
      || mounted.parent_path() == selected.parent_path()) {
      // Generations of one model share their parent folder.
      previous = source.source_key;
      older.push_back(source.source_key);
    }
  }
  if (selected_is_mounted) {
    previous.reset();
  }
  static_cast<void>(loader.MountLooseCookedGeneration(selected, previous));
  for (const auto key : older) {
    if (!previous || key != *previous) {
      static_cast<void>(loader.RetireLooseCookedGeneration(key));
    }
  }
}

} // namespace oxygen::examples::runtime
