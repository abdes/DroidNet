//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include "./SceneTest.h"

#include <Oxygen/Scene/Environment/SceneEnvironment.h>

namespace oxygen::scene::testing {

namespace {

  class SceneEnvironmentTest : public SceneTest { };

} // namespace

NOLINT_TEST_F(SceneEnvironmentTest, InitiallyAbsent)
{
  EXPECT_FALSE(scene_->HasEnvironment());
  EXPECT_EQ(scene_->GetEnvironment().get(), nullptr);
  EXPECT_EQ(std::as_const(*scene_).GetEnvironment().get(), nullptr);
}

NOLINT_TEST_F(SceneEnvironmentTest, SetTakesOwnership)
{
  auto environment = std::make_unique<SceneEnvironment>();
  const auto* const raw_ptr = environment.get();

  scene_->SetEnvironment(std::move(environment));

  EXPECT_TRUE(scene_->HasEnvironment());
  EXPECT_EQ(scene_->GetEnvironment().get(), raw_ptr);
  EXPECT_EQ(std::as_const(*scene_).GetEnvironment().get(), raw_ptr);
}

NOLINT_TEST_F(SceneEnvironmentTest, ClearRemovesEnvironment)
{
  scene_->SetEnvironment(std::make_unique<SceneEnvironment>());

  scene_->ClearEnvironment();

  EXPECT_FALSE(scene_->HasEnvironment());
  EXPECT_EQ(scene_->GetEnvironment().get(), nullptr);
}

NOLINT_TEST_F(SceneEnvironmentTest, ReplaceEnvironmentUpdatesPointer)
{
  scene_->SetEnvironment(std::make_unique<SceneEnvironment>());
  const auto* const first_ptr = scene_->GetEnvironment().get();

  auto second = std::make_unique<SceneEnvironment>();
  const auto* const second_ptr = second.get();
  scene_->SetEnvironment(std::move(second));

  EXPECT_TRUE(scene_->HasEnvironment());
  EXPECT_NE(scene_->GetEnvironment().get(), first_ptr);
  EXPECT_EQ(scene_->GetEnvironment().get(), second_ptr);
}

NOLINT_TEST_F(SceneEnvironmentTest, SetNullClearsEnvironment)
{
  scene_->SetEnvironment(std::make_unique<SceneEnvironment>());

  scene_->SetEnvironment(nullptr);

  EXPECT_FALSE(scene_->HasEnvironment());
  EXPECT_EQ(scene_->GetEnvironment().get(), nullptr);
}

} // namespace oxygen::scene::testing
