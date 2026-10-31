//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// Covers: Content/Internal/PakFileSource.h, Content/PakFile.h (payloads)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <asio/io_context.hpp>

#include <Oxygen/Content/Internal/PakFileSource.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/asio.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/AsyncDevice.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::content::PakFile;
using oxygen::content::internal::PakFileSource;
using oxygen::cooker::testing::CookedBytes;
using oxygen::cooker::testing::CookedPak;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::SeedBytes;
using oxygen::data::TextureResource;
using oxygen::data::pak::core::ResourceIndexT;
using oxygen::data::pak::core::TextureResourceDesc;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::testing::BytesEqual;

constexpr auto kMount = "Tests";
constexpr auto kTextureSeed = uint8_t { 7U };
constexpr auto kTexturePayloadSize = size_t { 256U };
//! The row of the first texture a root adds: row zero is reserved.
constexpr auto kTextureRow = ResourceIndexT { 1U };

//! A pak whose material references one texture of `payload`, the pak's
//! texture row `kTextureRow`.
auto TexturePak(const std::span<const std::byte> payload)
  -> std::vector<std::byte>
{
  auto root = LooseCookedRoot { kMount };
  const auto texture = root.AddTexture(payload);
  EXPECT_EQ(texture.get(), kTextureRow.get());
  static_cast<void>(root.Material("A.omat", texture));
  return CookedPak { root }.Encode();
}

//! `bytes` as a memory device.
auto InMemory(const std::vector<std::byte>& bytes) -> oxygen::serio::AnyDevice
{
  return oxygen::serio::AnyDevice(
    oxygen::serio::MemoryFile::FromBytes(bytes).value());
}

//! The pak `bytes` hold, mounted from memory.
auto Mount(const std::vector<std::byte>& bytes, const bool verify = false)
  -> PakFileSource
{
  return { InMemory(bytes), "test.pak", verify };
}

//! The descriptor of texture `index` of `source`.
auto TextureRow(const PakFileSource& source, const ResourceIndexT index)
  -> TextureResourceDesc
{
  const auto row
    = source.ResourceDescriptorBytes(TextureResource::ClassTypeId(), index);
  EXPECT_TRUE(row.has_value());
  auto desc = TextureResourceDesc {};
  if (row) {
    EXPECT_EQ(row->size(), sizeof(desc));
    std::memcpy(&desc, row->data(), sizeof(desc));
  }
  return desc;
}

//! Reads `out.size()` bytes at `at` of `device`, whose awaited reads must
//! complete at once.
template <class D>
auto ReadAtOnce(const D& device, const ByteOffset at,
  const std::span<std::byte> out) -> oxygen::Result<ByteCount>
{
  auto read = device.AsyncReadAt(at, out);
  EXPECT_TRUE(read.await_ready());
  return read.await_resume();
}

//! The pak's descriptor reader reads the entry's bytes, from its start.
NOLINT_TEST(PakFileSourceTest, DescriptorReaderReadsTheEntry)
{
  const auto bytes = TexturePak(SeedBytes(kTextureSeed));
  const auto source = Mount(bytes);
  const auto entry = source.Pak().Directory().front();

  const auto reader = source.CreateAssetDescriptorReader(entry.asset_key);

  ASSERT_NE(reader, nullptr);
  EXPECT_EQ(reader->Position().value(), 0U);
  EXPECT_EQ(reader->Remaining(), ByteCount { entry.desc_size });
  const auto read = reader->ReadBlob(entry.desc_size);
  ASSERT_HAS_VALUE(read);
  EXPECT_THAT(*read,
    BytesEqual(std::span(bytes).subspan(
      static_cast<size_t>(entry.desc_offset), entry.desc_size)));
}

//! A resource descriptor is the table row it names, a view of the pak.
NOLINT_TEST(PakFileSourceTest, ResourceDescriptorIsItsTableRow)
{
  const auto bytes = TexturePak(SeedBytes(kTextureSeed));
  const auto source = Mount(bytes);
  const auto table = source.Pak().Footer().texture_table;
  const auto index = kTextureRow;

  const auto row
    = source.ResourceDescriptorBytes(TextureResource::ClassTypeId(), index);

  ASSERT_TRUE(row.has_value());
  EXPECT_THAT(*row,
    BytesEqual(std::span(bytes).subspan(
      static_cast<size_t>(table.offset + (index.get() * table.entry_size)),
      table.entry_size)));
}

//! A row past the table is not a descriptor.
NOLINT_TEST(PakFileSourceTest, RefusesARowPastTheTable)
{
  const auto source = Mount(TexturePak(SeedBytes(kTextureSeed)));
  const auto count = source.Pak().Footer().texture_table.count;

  EXPECT_FALSE(source
      .ResourceDescriptorBytes(
        TextureResource::ClassTypeId(), ResourceIndexT { count })
      .has_value());
}

//! The payload region is the footer's texture region, and a descriptor's
//! data offset finds its payload in it.
NOLINT_TEST(PakFileSourceTest, PayloadRegionHoldsTheTexturePayload)
{
  const auto payload = SeedBytes(kTextureSeed, kTexturePayloadSize);
  const auto source = Mount(TexturePak(payload));
  const auto desc = TextureRow(source, kTextureRow);

  const auto region = source.PayloadRegion(TextureResource::ClassTypeId());

  ASSERT_TRUE(region.has_value());
  const auto footer = source.Pak().Footer().texture_region;
  EXPECT_EQ(region->Range().offset, ByteOffset { footer.offset });
  EXPECT_EQ(region->Range().count, ByteCount { footer.size });
  auto out = std::vector<std::byte>(desc.size_bytes);
  const auto read = ReadAtOnce(*region,
    ByteOffset { desc.data_offset - region->Range().offset.get() }, out);
  ASSERT_HAS_VALUE(read);
  EXPECT_EQ(*read, ByteCount { desc.size_bytes });
  EXPECT_THAT(out, BytesEqual(payload));
}

//! A pak holds no payloads of a kind it has no table of.
NOLINT_TEST(PakFileSourceTest, HoldsNoPayloadsWithoutATable)
{
  const auto source = Mount(TexturePak(SeedBytes(kTextureSeed)));

  EXPECT_FALSE(source.PayloadRegion(oxygen::data::BufferResource::ClassTypeId())
      .has_value());
  EXPECT_FALSE(source
      .ResourceDescriptorBytes(
        oxygen::data::BufferResource::ClassTypeId(), ResourceIndexT { 0U })
      .has_value());
}

//! A verified mount refuses a pak whose bytes changed after it was cooked.
NOLINT_TEST(PakFileSourceTest, VerifiedMountRefusesACorruptPak)
{
  auto bytes = TexturePak(SeedBytes(kTextureSeed));
  CookedBytes::CorruptCrc(bytes);

  NOLINT_EXPECT_THROW(
    std::ignore = Mount(bytes, /*verify=*/true), oxygen::data::CookedOpenError);
}

//! A verified mount of an intact pak opens it, as an unverified one does.
NOLINT_TEST(PakFileSourceTest, VerifiedMountAcceptsAnIntactPak)
{
  const auto bytes = TexturePak(SeedBytes(kTextureSeed));

  const auto source = Mount(bytes, /*verify=*/true);

  EXPECT_TRUE(source.PayloadRegion(TextureResource::ClassTypeId()).has_value());
}

//! A verified pak refuses a corrupt directory as a failed check, before the
//! directory is parsed.
NOLINT_TEST(PakFilePayloadsTest, VerifiedOpenChecksBeforeParsingTheDirectory)
{
  auto bytes = TexturePak(SeedBytes(kTextureSeed));
  CookedBytes::MoveDirectoryOutside(bytes);

  try {
    std::ignore
      = PakFile(InMemory(bytes), "test.pak", PakFile::Verification::kCrc32);
    ADD_FAILURE() << "the pak was accepted";
  } catch (const oxygen::data::CookedOpenError& error) {
    EXPECT_EQ(error.Failure(), oxygen::data::CookedOpenFailure::kCorrupt);
    EXPECT_STREQ(error.what(), "Pak CRC32 mismatch");
  }
}

//! A pak whose payload region is off its payload boundary is refused when it
//! opens: its payloads could not be read unbuffered.
NOLINT_TEST(PakFilePayloadsTest, PayloadRegionOffItsBoundaryIsRefused)
{
  auto bytes = TexturePak(SeedBytes(kTextureSeed));
  CookedBytes::MisalignTextureRegion(bytes);

  try {
    std::ignore = PakFile(InMemory(bytes), "test.pak");
    ADD_FAILURE() << "the pak was accepted";
  } catch (const oxygen::data::CookedOpenError& error) {
    EXPECT_EQ(error.Failure(), oxygen::data::CookedOpenFailure::kCorrupt);
    EXPECT_THAT(
      error.what(), ::testing::HasSubstr("does not lie on payload boundaries"));
  }
}

//! A verified open refuses a pak that holds no CRC32 to verify; an open that
//! does not verify accepts it.
NOLINT_TEST(PakFilePayloadsTest, VerifiedOpenRefusesAPakWithoutCrc)
{
  auto bytes = TexturePak(SeedBytes(kTextureSeed));
  CookedBytes::SetCrc(bytes, 0U);

  try {
    std::ignore
      = PakFile(InMemory(bytes), "test.pak", PakFile::Verification::kCrc32);
    ADD_FAILURE() << "the pak was accepted";
  } catch (const oxygen::data::CookedOpenError& error) {
    EXPECT_EQ(error.Failure(), oxygen::data::CookedOpenFailure::kUnverifiable);
  }
  EXPECT_NO_THROW(std::ignore = PakFile(InMemory(bytes), "test.pak"));
}

//! A pak in memory awaits its payloads from the bytes it is read from.
NOLINT_TEST(PakFilePayloadsTest, PakInMemoryServesPayloadsFromItsBytes)
{
  const auto bytes = TexturePak(SeedBytes(kTextureSeed));
  const auto pak = PakFile(InMemory(bytes), "test.pak");
  auto out = std::vector<std::byte>(64);

  const auto read = ReadAtOnce(pak.Payloads(), ByteOffset { 16 }, out);

  ASSERT_HAS_VALUE(read);
  EXPECT_THAT(out, BytesEqual(std::span(bytes).subspan(16, out.size())));
}

//! A pak file opened without a driver serves its payloads from its mapping,
//! with awaited reads that complete at once.
NOLINT_TEST(PakFilePayloadsTest, PakFileWithoutADriverReadsItsMapping)
{
  const auto dir = oxygen::testing::ScopedTempDir {};
  const auto path = dir.Path() / "test.pak";
  const auto bytes = TexturePak(SeedBytes(kTextureSeed));
  Materialize(bytes, path);
  const auto pak = PakFile(path);
  auto out = std::vector<std::byte>(64);

  const auto read = ReadAtOnce(pak.Payloads(), ByteOffset { 16 }, out);

  ASSERT_HAS_VALUE(read);
  EXPECT_THAT(out, BytesEqual(std::span(bytes).subspan(16, out.size())));
}

//! A pak file opened with a driver awaits its payloads through it: the read
//! suspends and completes on the driver's loop.
NOLINT_TEST(PakFilePayloadsTest, PakFileWithADriverAwaitsItsPayloads)
{
  const auto dir = oxygen::testing::ScopedTempDir {};
  const auto path = dir.Path() / "test.pak";
  const auto bytes = TexturePak(SeedBytes(kTextureSeed));
  Materialize(bytes, path);
  auto loop = asio::io_context {};
  auto driver = oxygen::serio::IoDriver::Create(loop);
  ASSERT_HAS_VALUE(driver);
  const auto pak = PakFile(path, PakFile::Verification::kNone,
    std::make_shared<oxygen::serio::IoDriver>(std::move(*driver)));
  auto out = std::vector<std::byte>(64);
  EXPECT_FALSE(
    pak.Payloads().AsyncReadAt(ByteOffset { 16 }, out).await_ready());

  auto read = oxygen::Result<void> {};
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
    read = co_await oxygen::serio::AsyncReadExactlyAt(
      pak.Payloads(), ByteOffset { 16 }, out);
  });

  ASSERT_HAS_VALUE(read);
  EXPECT_THAT(out, BytesEqual(std::span(bytes).subspan(16, out.size())));
}

} // namespace
