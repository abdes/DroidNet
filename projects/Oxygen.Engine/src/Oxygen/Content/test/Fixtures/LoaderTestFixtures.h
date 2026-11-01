//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {

//! The payload the resource descriptor at the start of `descriptor` names,
//! read from `data` as the pipeline reads it from a source: its `size_bytes`
//! at its `data_offset`. Empty when `data` does not hold it, as a payload the
//! pipeline could not read never reaches a loader.
template <class DescT>
auto StagePayload(const std::span<const std::byte> descriptor,
  const std::span<const std::byte> data) -> oxygen::serio::ByteBuffer
{
  const auto read = oxygen::serio::CopyAt<DescT>(descriptor);
  if (!read) {
    return {};
  }
  const auto& desc = *read;
  if (desc.data_offset > data.size()
    || desc.size_bytes > data.size() - desc.data_offset) {
    return {};
  }
  return oxygen::serio::ByteBuffer::CopyOf(
    data.subspan(static_cast<size_t>(desc.data_offset),
      static_cast<size_t>(desc.size_bytes)))
    .value();
}

class BinaryAssetLoaderFixtureBase : public ::testing::Test {
protected:
  using MemoryStream = oxygen::serio::MemoryStream;
  using Writer = oxygen::serio::Writer<MemoryStream>;
  using Reader = oxygen::serio::Reader<MemoryStream>;

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
    if (!desc_stream_.Seek(oxygen::serio::ByteOffset { 0 })) {
      throw std::runtime_error("Failed to seek desc_stream");
    }
    if (!data_stream_.Seek(oxygen::serio::ByteOffset { 0 })) {
      throw std::runtime_error("Failed to seek data_stream");
    }

    return oxygen::content::LoaderContext {
      .current_asset_key = oxygen::data::AssetKey {},
      .desc_reader = &desc_reader_,
      .work_offline = work_offline,
      .parse_only = parse_only,
    };
  }

  //! A context for a resource loader of `DescT` descriptors, handed the
  //! payload the descriptor names in the data stream.
  template <class DescT>
  auto MakeResourceContext() -> oxygen::content::LoaderContext
  {
    auto context = MakeLoaderContext(false, false);
    payload_ = StagePayload<DescT>(desc_stream_.Data(), data_stream_.Data());
    context.payload = observer_ptr { &payload_ };
    return context;
  }

  auto MakeDecodeLoaderContext() -> std::pair<oxygen::content::LoaderContext,
    std::shared_ptr<oxygen::content::internal::DependencyCollector>>
  {
    auto context = MakeLoaderContext(true, false);
    auto collector
      = std::make_shared<oxygen::content::internal::DependencyCollector>();
    context.source_instance = oxygen::data::SourceInstanceId(7U);
    context.dependency_collector = collector;
    return { std::move(context), std::move(collector) };
  }

  MemoryStream desc_stream_;
  MemoryStream data_stream_;
  Writer desc_writer_;
  Writer data_writer_;
  Reader desc_reader_;
  Reader data_reader_;
  oxygen::serio::ByteBuffer payload_;
};

} // namespace oxygen::content::testing
