//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <stdexcept>
#include <utility>

#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/SourceToken.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

#include "../Mocks/MockStream.h"

namespace oxygen::content::testing {

class BinaryAssetLoaderFixtureBase : public ::testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Writer = oxygen::serio::Writer<MockStream>;
  using Reader = oxygen::serio::Reader<MockStream>;

  BinaryAssetLoaderFixtureBase()
    : desc_writer_(desc_stream_)
    , data_writer_(data_stream_)
    , desc_reader_(desc_stream_)
    , data_reader_(data_stream_)
  {
  }

  auto MakeLoaderContext(const bool work_offline = false,
    const bool parse_only = false) -> oxygen::content::LoaderContext
  {
    if (!desc_stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek desc_stream");
    }
    if (!data_stream_.Seek(0)) {
      throw std::runtime_error("Failed to seek data_stream");
    }

    return oxygen::content::LoaderContext {
      .current_asset_key = oxygen::data::AssetKey {},
      .desc_reader = &desc_reader_,
      .data_readers = std::make_tuple(
        &data_reader_, &data_reader_, &data_reader_, &data_reader_),
      .work_offline = work_offline,
      .parse_only = parse_only,
    };
  }

  auto MakeDecodeLoaderContext() -> std::pair<oxygen::content::LoaderContext,
    std::shared_ptr<oxygen::content::internal::DependencyCollector>>
  {
    auto context = MakeLoaderContext(true, false);
    auto collector
      = std::make_shared<oxygen::content::internal::DependencyCollector>();
    context.source_token = oxygen::content::internal::SourceToken(7U);
    context.dependency_collector = collector;
    context.source_pak = nullptr;
    return { std::move(context), std::move(collector) };
  }

  MockStream desc_stream_;
  MockStream data_stream_;
  Writer desc_writer_;
  Writer data_writer_;
  Reader desc_reader_;
  Reader data_reader_;
};

} // namespace oxygen::content::testing
