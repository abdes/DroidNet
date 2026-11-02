//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "LoaderTestFixtures.h"

#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {

namespace pak7 = oxygen::data::pak::physics;

class PhysicsLoaderFixtureBase : public ::testing::Test {
protected:
  using MemoryStream = oxygen::serio::MemoryStream;
  using Reader = oxygen::serio::Reader<MemoryStream>;
  using Writer = oxygen::serio::Writer<MemoryStream>;
};

class PhysicsResourceFixtureBase : public PhysicsLoaderFixtureBase {
protected:
  PhysicsResourceFixtureBase()
    : desc_writer_(desc_stream_)
    , data_writer_(data_stream_)
    , desc_reader_(desc_stream_)
    , data_reader_(data_stream_)
  {
  }

  auto MakeContext() -> oxygen::content::LoaderContext
  {
    if (!desc_stream_.Seek(oxygen::serio::ByteOffset { 0 })) {
      throw std::runtime_error("Failed to seek desc_stream");
    }
    if (!data_stream_.Seek(oxygen::serio::ByteOffset { 0 })) {
      throw std::runtime_error("Failed to seek data_stream");
    }
    payload_ = StagePayload<oxygen::data::pak::physics::PhysicsResourceDesc>(
      desc_stream_.Data(), data_stream_.Data());
    return oxygen::content::LoaderContext {
      .current_asset_key = oxygen::data::AssetKey {},
      .desc_reader = &desc_reader_,
      .payload = observer_ptr { &payload_ },
      .work_offline = false,
    };
  }

  auto WriteDescriptorAndData(
    const oxygen::data::pak::physics::PhysicsResourceDesc& desc,
    const uint8_t fill) -> void
  {
    auto packed_desc = desc_writer_.ScopedAlignment(1);
    const auto desc_bytes = std::as_bytes(std::span { &desc, 1U });
    const auto desc_result = desc_writer_.WriteBlob(desc_bytes);
    if (!desc_result) {
      throw std::runtime_error(
        "failed to write descriptor: " + desc_result.error().message());
    }

    auto packed_data = data_writer_.ScopedAlignment(1);
    const auto current_pos = data_writer_.Position();
    if (!current_pos) {
      throw std::runtime_error(
        "failed to query data position: " + current_pos.error().message());
    }

    if (current_pos.value() < desc.data_offset) {
      std::vector<std::byte> pad(
        static_cast<size_t>(desc.data_offset - current_pos.value()),
        std::byte { 0 });
      const auto pad_result = data_writer_.WriteBlob(
        std::span<const std::byte>(pad.data(), pad.size()));
      if (!pad_result) {
        throw std::runtime_error(
          "failed to write pad bytes: " + pad_result.error().message());
      }
    }

    std::vector<std::byte> payload(desc.size_bytes, std::byte { fill });
    const auto payload_result = data_writer_.WriteBlob(
      std::span<const std::byte>(payload.data(), payload.size()));
    if (!payload_result) {
      throw std::runtime_error(
        "failed to write payload: " + payload_result.error().message());
    }
  }

  MemoryStream desc_stream_;
  MemoryStream data_stream_;
  Writer desc_writer_;
  Writer data_writer_;
  Reader desc_reader_;
  Reader data_reader_;
  oxygen::serio::ByteBuffer payload_;
};

class PhysicsSceneFixtureBase : public PhysicsLoaderFixtureBase {
protected:
  PhysicsSceneFixtureBase()
    : writer_(stream_)
    , reader_(stream_)
  {
  }

  auto MakeContext() -> oxygen::content::LoaderContext
  {
    EXPECT_TRUE(stream_.Seek(oxygen::serio::ByteOffset { 0 }))
      << "Failed to rewind stream";
    return oxygen::content::LoaderContext {
      .current_asset_key = oxygen::data::AssetKey {},
      .desc_reader = &reader_,
      .work_offline = true,
      .parse_only = true,
    };
  }

  //! The key of the scene the sidecars of this fixture pair with.
  static auto SceneKey() -> oxygen::data::AssetKey
  {
    return oxygen::data::AssetKey::FromVirtualPath("/test/PhysicsScene.oscene");
  }

  //! Adds `count` nodes to the scene the sidecars pair with.
  auto AddNodes(const uint32_t count) -> void
  {
    for (uint32_t i = 0; i < count; ++i) {
      nodes_.push_back(scene_.Node("node" + std::to_string(nodes_.size())));
    }
  }

  [[nodiscard]] auto NodeAt(const size_t index) const
    -> oxygen::cooker::testing::SceneNode
  {
    return nodes_.at(index);
  }

  //! The sidecar of the fixture's scene holding `bindings`.
  [[nodiscard]] auto SidecarBytes(
    const oxygen::cooker::testing::PhysicsSidecarBuilder& bindings = {}) const
    -> std::vector<std::byte>
  {
    return bindings.Bytes(SceneKey(), scene_.Bytes(), "PhysicsScene");
  }

  //! Writes `bytes` as the descriptor the loader reads, replacing any written
  //! before.
  auto WriteBytes(const std::span<const std::byte> bytes) -> void
  {
    stream_.Clear();
    const auto written = writer_.WriteBlob(bytes);
    ASSERT_TRUE(written) << written.error().message();
    ASSERT_TRUE(writer_.Flush()) << "Flush failed";
  }

  oxygen::cooker::testing::SceneBuilder scene_ { "PhysicsScene" };
  std::vector<oxygen::cooker::testing::SceneNode> nodes_;
  MemoryStream stream_;
  Writer writer_;
  Reader reader_;
};

} // namespace oxygen::content::testing
