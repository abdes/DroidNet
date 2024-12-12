//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include "oxygen/editor-api/api.h"

// NOLINTNEXTLINE
TEST(LinkedEditorApiTest, CanUseApi) {
  OxygenTransformCreateInfo transform_create_info{
      .position = {0, 0, 0},
      .rotation = {0, 0, 0},
      .scale = {1, 1, 1},
  };
  OxygenGameEntityCreateInfo entity_create_info{
      .transform = &transform_create_info,
  };

  CreateGameEntity(&entity_create_info);
}
