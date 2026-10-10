//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/AsyncImporter.cpp

#include <utility>

#include <Oxygen/Cooker/Import/Internal/AsyncImporter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen::content::import::detail;
using oxygen::co::Co;
using oxygen::co::kJoin;

namespace {

//=== Lifecycle Tests
//===---------------------------------------------------------//

class AsyncImporterLifecycleTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
  AsyncImporter::Config config_ { .channel_capacity = 8 };
};

NOLINT_TEST_F(AsyncImporterLifecycleTest, ConstructDestructSucceeds)
{
  {
    AsyncImporter importer(config_);
  }

  // No crash
  SUCCEED();
}

NOLINT_TEST_F(AsyncImporterLifecycleTest, IsRunningBeforeActivationReturnsFalse)
{
  AsyncImporter importer(config_);

  EXPECT_FALSE(importer.IsRunning());
}

NOLINT_TEST_F(
  AsyncImporterLifecycleTest, IsAcceptingJobsAfterConstructionReturnsTrue)
{
  AsyncImporter importer(config_);

  EXPECT_TRUE(importer.IsAcceptingJobs());
}

NOLINT_TEST_F(AsyncImporterLifecycleTest, ActivateRunStopFullLifecycleSucceeds)
{
  AsyncImporter importer(config_);

  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      // Activate the importer
      co_await n.Start(&AsyncImporter::ActivateAsync, &importer);
      EXPECT_TRUE(importer.IsRunning());

      // Start the processing loop
      importer.Run();

      // Stop the importer
      importer.Stop();

      co_return kJoin;
    };
  });

  EXPECT_FALSE(importer.IsRunning());
}

NOLINT_TEST_F(AsyncImporterLifecycleTest, StopClosesJobChannel)
{
  AsyncImporter importer(config_);

  oxygen::co::Run(loop_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AsyncImporter::ActivateAsync, &importer);
      importer.Run();

      EXPECT_TRUE(importer.IsAcceptingJobs());
      importer.Stop();
      EXPECT_FALSE(importer.IsAcceptingJobs());

      co_return kJoin;
    };
  });

  EXPECT_FALSE(importer.IsAcceptingJobs());
}

//=== TrySubmitJob Tests ===-------------------------------------------------//

class AsyncImporterTrySubmitTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

NOLINT_TEST_F(AsyncImporterTrySubmitTest, TrySubmitJobWhenSpaceReturnsTrue)
{
  AsyncImporter importer({ .channel_capacity = 4 });

  JobEntry entry;
  entry.job_id = ImportJobId { 1U };
  const bool result = importer.TrySubmitJob(std::move(entry));

  EXPECT_TRUE(result);
}

NOLINT_TEST_F(AsyncImporterTrySubmitTest, TrySubmitJobWhenFullReturnsFalse)
{
  AsyncImporter importer({ .channel_capacity = 2 });

  // Fill the channel
  for (uint64_t i = 0; i < 2; ++i) {
    JobEntry entry;
    entry.job_id = ImportJobId { i };
    EXPECT_TRUE(importer.TrySubmitJob(std::move(entry)));
  }

  JobEntry extra_entry;
  extra_entry.job_id = ImportJobId { 99U };
  const bool result = importer.TrySubmitJob(std::move(extra_entry));

  EXPECT_FALSE(result);
}

NOLINT_TEST_F(AsyncImporterTrySubmitTest, TrySubmitJobWhenClosedReturnsFalse)
{
  AsyncImporter importer({ .channel_capacity = 4 });
  importer.CloseJobChannel();

  JobEntry entry;
  entry.job_id = ImportJobId { 1U };
  const bool result = importer.TrySubmitJob(std::move(entry));

  EXPECT_FALSE(result);
}

} // namespace
