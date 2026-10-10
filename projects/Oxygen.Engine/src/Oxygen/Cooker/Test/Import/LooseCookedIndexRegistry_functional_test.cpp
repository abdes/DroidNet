//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedIndexRegistry.cpp

#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace co = oxygen::co;
namespace imp = oxygen::content::import;

NOLINT_TEST(
  LooseCookedIndexRegistryTest, IndexParticipantsAwaitPublicationAndCanReenter)
{
  imp::ImportEventLoop loop;
  imp::LooseCookedIndexRegistry indexes;
  const oxygen::cooker::test::ScopedTempDir temp;
  const auto root = temp.Path() / "oxygen_index_publication";
  std::filesystem::create_directories(root);
  auto first = indexes.BeginSession(root, std::nullopt);
  auto last = indexes.BeginSession(root, std::nullopt);
  bool first_completed = false;
  oxygen::data::SourceKey first_key {};
  oxygen::data::SourceKey last_key {};
  co::Event next_turn;
  co::Run(loop, [&] -> co::Co<> {
    OXCO_WITH_NURSERY(tasks)
    {
      tasks.Start([&] -> co::Co<> {
        const auto publication = co_await indexes.EndSession(first);
        first_completed = true;
        first_key = publication.source_key;
        EXPECT_FALSE(publication.write_result.has_value());
        EXPECT_TRUE(std::filesystem::exists(root / "container.index.bin"));
        // Publication wakes callbacks outside the root mutex, so they can admit
        // the next cohort without observing or overwriting the old completion.
        auto next = indexes.BeginSession(root, std::nullopt);
        const auto next_publication = co_await indexes.EndSession(next);
        EXPECT_TRUE(next_publication.write_result.has_value());
      });
      loop.Post([&] -> void { next_turn.Trigger(); });
      co_await next_turn;
      EXPECT_FALSE(first.IsActive());
      EXPECT_FALSE(first_completed);
      const auto publication = co_await indexes.EndSession(last);
      EXPECT_TRUE(publication.write_result.has_value());
      last_key = publication.source_key;
      co_return co::kJoin;
    };
  });
  EXPECT_TRUE(first_completed);
  EXPECT_EQ(first_key, last_key);
  EXPECT_FALSE(first.IsActive());
  EXPECT_FALSE(last.IsActive());
}

NOLINT_TEST(LooseCookedIndexRegistryTest,
  ConflictingSourceIdentityCannotJoinAnIndexCohort)
{
  imp::ImportEventLoop loop;
  imp::LooseCookedIndexRegistry indexes;
  const oxygen::cooker::test::ScopedTempDir temp;
  const auto root = temp.Path() / "oxygen_source_identity_cohort";
  std::filesystem::create_directories(root);
  const auto expected = oxygen::data::SourceKey { oxygen::Uuid::Generate() };
  const auto conflict = oxygen::data::SourceKey { oxygen::Uuid::Generate() };
  auto participation = indexes.BeginSession(root, expected);
  EXPECT_THROW(static_cast<void>(indexes.BeginSession(root, conflict)),
    std::invalid_argument);
  const auto published = co::Run(loop, indexes.EndSession(participation));
  EXPECT_EQ(published.source_key, expected);
  const auto index = oxygen::content::lc::LooseCookedIndex::LoadFromRoot(root);
  EXPECT_EQ(index.Guid(), expected);
}

NOLINT_TEST(
  LooseCookedIndexRegistryTest, FailedIndexPublicationReachesEveryParticipant)
{
  imp::ImportEventLoop loop;
  imp::LooseCookedIndexRegistry indexes;
  const oxygen::cooker::test::ScopedTempDir temp;
  const auto root = temp.Path() / "oxygen_index_publication_failure";
  std::filesystem::create_directories(root);
  auto first = indexes.BeginSession(root, std::nullopt);
  auto last = indexes.BeginSession(root, std::nullopt);
  std::filesystem::create_directories(root / "container.index.bin");
  uint32_t failures = 0;
  co::Event next_turn;
  co::Run(loop, [&] -> co::Co<> {
    OXCO_WITH_NURSERY(tasks)
    {
      tasks.Start([&] -> co::Co<> {
        try {
          static_cast<void>(co_await indexes.EndSession(first));
          ADD_FAILURE() << "A failed publication reached a waiting participant";
        } catch (const std::exception&) {
          ++failures;
        }
      });
      loop.Post([&] -> void { next_turn.Trigger(); });
      co_await next_turn;
      EXPECT_FALSE(first.IsActive());
      EXPECT_EQ(failures, 0U);
      try {
        static_cast<void>(co_await indexes.EndSession(last));
        ADD_FAILURE() << "Replacing a directory with the index must fail";
      } catch (const std::exception&) {
        ++failures;
      }
      co_return co::kJoin;
    };
  });
  EXPECT_EQ(failures, 2U);
  EXPECT_FALSE(first.IsActive());
  EXPECT_FALSE(last.IsActive());
}

NOLINT_TEST(
  LooseCookedIndexRegistryTest, LastIndexAbortWakesWaitersAndAllowsRetry)
{
  imp::ImportEventLoop loop;
  imp::LooseCookedIndexRegistry indexes;
  const oxygen::cooker::test::ScopedTempDir temp;
  const auto root = temp.Path() / "oxygen_index_publication_abort";
  std::filesystem::create_directories(root);
  auto first = indexes.BeginSession(root, std::nullopt);
  auto last = indexes.BeginSession(root, std::nullopt);
  bool rejected = false;
  co::Event next_turn;
  co::Run(loop, [&] -> co::Co<> {
    OXCO_WITH_NURSERY(tasks)
    {
      tasks.Start([&] -> co::Co<> {
        try {
          static_cast<void>(co_await indexes.EndSession(first));
          ADD_FAILURE() << "An aborted publication cannot succeed";
        } catch (const std::runtime_error&) {
          rejected = true;
        }
      });
      loop.Post([&] -> void { next_turn.Trigger(); });
      co_await next_turn;
      EXPECT_FALSE(first.IsActive());
      EXPECT_FALSE(rejected);
      indexes.AbortSession(last);
      auto retry = indexes.BeginSession(root, std::nullopt);
      const auto publication = co_await indexes.EndSession(retry);
      EXPECT_TRUE(publication.write_result.has_value());
      co_return co::kJoin;
    };
  });
  EXPECT_TRUE(rejected);
  EXPECT_FALSE(first.IsActive());
  EXPECT_FALSE(last.IsActive());
}

NOLINT_TEST(
  LooseCookedIndexRegistryTest, CancelledIndexWaiterDoesNotRetireTwice)
{
  imp::ImportEventLoop loop;
  imp::LooseCookedIndexRegistry indexes;
  const oxygen::cooker::test::ScopedTempDir temp;
  const auto root = temp.Path() / "oxygen_index_waiter_cancel";
  std::filesystem::create_directories(root);
  auto first = indexes.BeginSession(root, std::nullopt);
  auto last = indexes.BeginSession(root, std::nullopt);
  co::Event cancel;
  co::Event cancelled;
  co::Event next_turn;
  co::Run(loop, [&] -> co::Co<> {
    OXCO_WITH_NURSERY(tasks)
    {
      tasks.Start([&] -> co::Co<> {
        static_cast<void>(
          co_await co::AnyOf(indexes.EndSession(first), cancel));
        cancelled.Trigger();
      });
      loop.Post([&] -> void { next_turn.Trigger(); });
      co_await next_turn;
      EXPECT_FALSE(first.IsActive());
      cancel.Trigger();
      co_await cancelled;
      const auto publication = co_await indexes.EndSession(last);
      EXPECT_TRUE(publication.write_result.has_value());
      co_return co::kJoin;
    };
  });
  EXPECT_FALSE(last.IsActive());
}

} // namespace
