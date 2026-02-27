//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

#include "../Mocks/MockStream.h"

namespace oxygen::content::testing {

namespace pak7 = oxygen::data::pak::physics;

class PhysicsLoaderFixtureBase : public ::testing::Test {
protected:
  using MockStream = oxygen::content::testing::MockStream;
  using Reader = oxygen::serio::Reader<MockStream>;
  using Writer = oxygen::serio::Writer<MockStream>;

  template <typename T>
  static auto WriteAs(Writer& writer, const T& value) -> void
  {
    const auto result = writer.WriteBlob(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&value), sizeof(T)));
    ASSERT_TRUE(result) << result.error().message();
  }
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
      .work_offline = false,
    };
  }

  auto WriteDescriptorAndData(
    const oxygen::data::pak::physics::PhysicsResourceDesc& desc,
    const uint8_t fill) -> void
  {
    auto packed_desc = desc_writer_.ScopedAlignment(1);
    const auto desc_bytes = std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(&desc), sizeof(desc));
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

  MockStream desc_stream_;
  MockStream data_stream_;
  Writer desc_writer_;
  Writer data_writer_;
  Reader desc_reader_;
  Reader data_reader_;
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
    EXPECT_TRUE(stream_.Seek(0)) << "Failed to rewind stream";
    return oxygen::content::LoaderContext {
      .current_asset_key = oxygen::data::AssetKey {},
      .desc_reader = &reader_,
      .work_offline = true,
      .parse_only = true,
    };
  }

  auto MakeMinimalDesc() -> pak7::PhysicsSceneAssetDesc
  {
    pak7::PhysicsSceneAssetDesc desc {};
    desc.header.asset_type
      = static_cast<uint8_t>(oxygen::data::AssetType::kPhysicsScene);
    desc.header.version = pak7::kPhysicsSceneAssetVersion;
    desc.component_table_count = 0;
    desc.component_table_directory_offset = 0;
    return desc;
  }

  auto WriteOneRigidBodyTable(pak7::RigidBodyBindingRecord record = {}) -> void
  {
    pak7::PhysicsSceneAssetDesc desc = MakeMinimalDesc();
    desc.component_table_count = 1;

    constexpr uint32_t kDirOffset = sizeof(pak7::PhysicsSceneAssetDesc);
    constexpr uint32_t kTableOffset
      = kDirOffset + sizeof(pak7::PhysicsComponentTableDesc);

    desc.component_table_directory_offset = kDirOffset;

    pak7::PhysicsComponentTableDesc entry {};
    entry.binding_type = pak7::PhysicsBindingType::kRigidBody;
    entry.table.offset = kTableOffset;
    entry.table.count = 1;
    entry.table.entry_size = sizeof(pak7::RigidBodyBindingRecord);

    WriteAs(writer_, desc);
    WriteAs(writer_, entry);
    WriteAs(writer_, record);
    ASSERT_TRUE(writer_.Flush()) << "Flush failed";
  }

  MockStream stream_;
  Writer writer_;
  Reader reader_;
};

} // namespace oxygen::content::testing
