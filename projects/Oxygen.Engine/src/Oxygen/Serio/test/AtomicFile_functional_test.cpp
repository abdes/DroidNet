//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/AtomicFile.h, Serio/AtomicFile.cpp, Serio/Integrity.h (files)

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <asio/io_context.hpp>

#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/asio.h> // IWYU pragma: keep (EventLoopTraits)
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Integrity.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/Testing/AtomicFiles.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

#ifdef OXYGEN_WINDOWS
#  include <Windows.h> // IWYU pragma: keep

#  include <sddl.h>
#endif

namespace {

//! The most bytes a test reads back from a file it published.
constexpr auto kReadLimit = oxygen::serio::ByteCount { 1024ULL * 1024 };

//! Reads a file through a handle that stays open, sharing it as readers of
//! published files do: reads and deletion, never writes.
class SharingReader final {
public:
  explicit SharingReader(const std::filesystem::path& path)
#ifdef OXYGEN_WINDOWS
    : handle_(CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr))
#else
    : input_(path, std::ios::binary)
#endif
  {
  }
  ~SharingReader()
  {
#ifdef OXYGEN_WINDOWS
    if (IsOpen()) {
      CloseHandle(handle_);
    }
#endif
  }
  SharingReader(const SharingReader&) = delete;
  auto operator=(const SharingReader&) -> SharingReader& = delete;
  SharingReader(SharingReader&&) = delete;
  auto operator=(SharingReader&&) -> SharingReader& = delete;

  [[nodiscard]] auto IsOpen() const -> bool
  {
#ifdef OXYGEN_WINDOWS
    return handle_ != INVALID_HANDLE_VALUE;
#else
    return input_.is_open();
#endif
  }

  auto ReadAll() -> std::string
  {
#ifdef OXYGEN_WINDOWS
    auto text = std::string {};
    auto chunk = std::vector<char>(256);
    DWORD read = 0;
    while (ReadFile(handle_, chunk.data(), static_cast<DWORD>(chunk.size()),
             &read, nullptr)
        != FALSE
      && read != 0U) {
      text.append(chunk.data(), read);
    }
    return text;
#else
    return { std::istreambuf_iterator<char>(input_), {} };
#endif
  }

private:
#ifdef OXYGEN_WINDOWS
  HANDLE handle_;
#else
  std::ifstream input_;
#endif
};

class AtomicFileTest : public testing::Test {
protected:
  static auto Bytes(const std::string& text) -> std::span<const std::byte>
  {
    return std::as_bytes(std::span(text.data(), text.size()));
  }
  static auto Read(const std::filesystem::path& path) -> std::string
  {
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), {} };
  }
  static auto Text(const std::span<const std::byte> bytes) -> std::string
  {
    auto text = std::string(bytes.size(), '\0');
    std::ranges::transform(bytes, text.begin(),
      [](const std::byte byte) -> char { return static_cast<char>(byte); });
    return text;
  }
  [[nodiscard]] auto EntryCount() const -> std::ptrdiff_t
  {
    return std::distance(std::filesystem::directory_iterator(root_),
      std::filesystem::directory_iterator {});
  }
  //! The folder each test writes in.
  [[nodiscard]] auto Root() const -> const std::filesystem::path&
  {
    return root_;
  }

private:
  oxygen::testing::ScopedTempDir dir_;
  std::filesystem::path root_ = dir_.Path();
};

//! The driver `AsyncWriteFileAtomically` publishes through.
enum class DriverKind : uint8_t { kLoop, kInline };

//! Publishes through a driver of the kind under test.
class AsyncAtomicFileTest : public AtomicFileTest,
                            public ::testing::WithParamInterface<DriverKind> {
protected:
  auto Publish(const std::filesystem::path& path,
    const std::span<const std::byte> bytes,
    const oxygen::serio::Durability durability)
    -> oxygen::Result<oxygen::serio::FileReplacement>
  {
    using oxygen::serio::IoDriver;
    auto driver = GetParam() == DriverKind::kInline ? IoDriver::CreateInline()
                                                    : IoDriver::Create(loop_);
    if (!driver) {
      throw std::runtime_error("driver creation failed");
    }
    return oxygen::co::Run(loop_,
      oxygen::serio::AsyncWriteFileAtomically(
        *driver, path, bytes, durability));
  }

private:
  asio::io_context loop_;
};

INSTANTIATE_TEST_SUITE_P(Serio, AsyncAtomicFileTest,
  ::testing::Values(DriverKind::kLoop, DriverKind::kInline),
  [](const ::testing::TestParamInfo<DriverKind>& info) {
    return info.param == DriverKind::kInline ? std::string("Inline")
                                             : std::string("Loop");
  });

NOLINT_TEST_F(AtomicFileTest, ReplacesCompleteBytesAndLeavesNoTemporaryFiles)
{
  const auto path = Root() / "record.json";
  const std::string before = "old-record";
  const std::string after = "new-record-with-more-bytes";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(before)));
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(after)));
  EXPECT_EQ(Read(path), after);
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(Root()),
              std::filesystem::directory_iterator {}),
    1);
}

NOLINT_TEST_F(AtomicFileTest, RefusesDirectoryDestinationWithoutChangingIt)
{
  const auto directory = Root() / "record";
  std::filesystem::create_directory(directory);
  const std::string data = "value";
  EXPECT_FALSE(oxygen::serio::WriteFileAtomically(directory, Bytes(data)));
  EXPECT_TRUE(std::filesystem::is_directory(directory));
  EXPECT_TRUE(std::filesystem::is_empty(directory));
}

NOLINT_TEST_F(AtomicFileTest, MissingParentDoesNotCreatePartialOutput)
{
  const auto path = Root() / "missing" / "record.json";
  const std::string data = "value";
  EXPECT_FALSE(oxygen::serio::WriteFileAtomically(path, Bytes(data)));
  EXPECT_FALSE(std::filesystem::exists(path));
}

//! A reader of the published file never blocks its replacement: the
//! publication succeeds, the reader keeps the bytes it opened, and new reads
//! see the new bytes.
NOLINT_TEST_F(AtomicFileTest, ReplacesDestinationWhileASharingReaderHoldsIt)
{
  const auto path = Root() / "container.index.bin";
  const std::string before = "published-before";
  const std::string after = "published-after-with-more-bytes";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(before)));

  auto reader = SharingReader(path);
  ASSERT_TRUE(reader.IsOpen());
  const auto replaced = oxygen::serio::WriteFileAtomically(path, Bytes(after));

  ASSERT_TRUE(replaced) << replaced.error().message();
  EXPECT_EQ(reader.ReadAll(), before);
  const auto current = oxygen::serio::ReadPublishedFile(path, kReadLimit);
  ASSERT_TRUE(current) << current.error().message();
  EXPECT_EQ(Text(current->Bytes()), after);
}

//! A reader opening the destination while it is replaced again and again
//! always opens it, and every publication succeeds: a publication leaves no
//! handle on it that a reader conflicts with, and a reader sharing deletion
//! never blocks the replacement.
NOLINT_TEST_F(AtomicFileTest, ReaderOpensTheDestinationDuringPublications)
{
  constexpr auto kPublications = 200U;
  const auto path = Root() / "container.index.bin";
  const std::string first = "published-first";
  const std::string second = "published-second";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(first)));
  auto done = std::atomic<bool> { false };
  auto published = 0U;
  auto publish_failure = std::string {};
  auto publisher = std::thread([&] -> void {
    for (auto i = 0U; i < kPublications; ++i) {
      const auto replaced = oxygen::serio::WriteFileAtomically(
        path, Bytes((i % 2U) != 0U ? first : second));
      if (replaced) {
        ++published;
      } else if (publish_failure.empty()) {
        publish_failure = replaced.error().message();
      }
    }
    done = true;
  });

  auto failure = std::string {};
  auto reads = 0U;
  while (!done || reads == 0U) {
    const auto bytes = oxygen::serio::ReadPublishedFile(path, kReadLimit);
    if (!bytes) {
      failure = bytes.error().message();
    } else if (const auto text = Text(bytes->Bytes());
      text != first && text != second) {
      failure = "not one of the published versions: " + text;
    }
    ++reads;
  }
  publisher.join();

  EXPECT_TRUE(failure.empty()) << failure;
  EXPECT_EQ(published, kPublications) << publish_failure;
}

//! The destination is named exactly as asked, whatever its length: the
//! replacement never reads past the name or names a different file.
NOLINT_TEST_F(AtomicFileTest, ReplacesDestinationsOfEveryNameLength)
{
  auto name = std::string("i");
  for (auto length = 1; length <= 8; ++length, name += 'n') {
    const auto path = Root() / name;
    const std::string before = "before";
    const std::string after = "after";
    ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(before)));
    const auto replaced
      = oxygen::serio::WriteFileAtomically(path, Bytes(after));

    ASSERT_TRUE(replaced) << name << ": " << replaced.error().message();
    EXPECT_EQ(Read(path), after) << name;
  }
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(Root()),
              std::filesystem::directory_iterator {}),
    8);
}

NOLINT_TEST_F(AtomicFileTest, ReadPublishedFileReturnsEveryPublishedByte)
{
  const auto path = Root() / "record.json";
  const auto text = std::string(70000, 'x') + "tail";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(text)));

  const auto bytes = oxygen::serio::ReadPublishedFile(path, kReadLimit);

  ASSERT_TRUE(bytes) << bytes.error().message();
  EXPECT_EQ(Text(bytes->Bytes()), text);
}

//! A published file one byte larger than the limit is refused with
//! kTooLarge; at the limit it is read.
NOLINT_TEST_F(AtomicFileTest, ReadPublishedFileRefusesAFileLargerThanTheLimit)
{
  const auto path = Root() / "large.bin";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes("0123456789")));

  const auto refused
    = oxygen::serio::ReadPublishedFile(path, oxygen::serio::ByteCount { 9 });
  const auto read
    = oxygen::serio::ReadPublishedFile(path, oxygen::serio::ByteCount { 10 });

  ASSERT_FALSE(refused);
  EXPECT_EQ(refused.error(), oxygen::serio::Error::kTooLarge);
  ASSERT_HAS_VALUE(read);
  EXPECT_EQ(Text(read->Bytes()), "0123456789");
}

//! A missing published file fails with its error.
NOLINT_TEST_F(AtomicFileTest, ReadPublishedFileReportsAMissingFile)
{
  EXPECT_FALSE(
    oxygen::serio::ReadPublishedFile(Root() / "missing.bin", kReadLimit));
}

//! A writer streams the content, patches a header once the content is known,
//! and publishes all of it on commit.
NOLINT_TEST_F(AtomicFileTest, WriterStreamsPatchesAndPublishes)
{
  const auto path = Root() / "index.bin";
  auto writer = oxygen::serio::AtomicFileWriter::Create(path);
  ASSERT_HAS_VALUE(writer);
  const std::string body = "body-of-the-index";
  const std::string header = "HEAD";

  ASSERT_TRUE(writer->Stream().Write(Bytes("????")));
  ASSERT_TRUE(writer->Stream().Write(Bytes(body)));
  ASSERT_TRUE(
    writer->Stream().PatchAt(oxygen::serio::ByteOffset { 0 }, Bytes(header)));
  const auto commit = std::move(*writer).Commit();

  ASSERT_TRUE(commit) << commit.error().message();
  EXPECT_FALSE(commit->durability_error);
  EXPECT_EQ(Read(path), header + body);
}

//! Until commit, the destination keeps its bytes; a writer dropped without
//! commit removes its sibling and leaves the destination as it was.
NOLINT_TEST_F(AtomicFileTest, AbandonedWriterLeavesTheDestination)
{
  const auto path = Root() / "record.json";
  const std::string before = "before";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(before)));
  {
    auto writer = oxygen::serio::AtomicFileWriter::Create(path);
    ASSERT_HAS_VALUE(writer);
    ASSERT_TRUE(writer->Stream().Write(Bytes("never-published")));
    ASSERT_TRUE(writer->Stream().Flush());
    EXPECT_EQ(Read(path), before);
  }

  EXPECT_EQ(Read(path), before);
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(Root()),
              std::filesystem::directory_iterator {}),
    1);
}

//! A published file opened for reading keeps the bytes it opened while a
//! publication replaces it.
NOLINT_TEST_F(AtomicFileTest, OpenPublishedFileKeepsTheBytesItOpened)
{
  const auto path = Root() / "container.index.bin";
  const std::string before = "published-before";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(before)));
  const auto file = oxygen::serio::OpenPublishedFile(path);
  ASSERT_HAS_VALUE(file);

  const auto replaced
    = oxygen::serio::WriteFileAtomically(path, Bytes("published-after"));
  auto bytes = std::vector<std::byte>(before.size());
  const auto read = oxygen::serio::ReadExactlyAt(
    *file, oxygen::serio::ByteOffset { 0 }, std::span { bytes });

  ASSERT_TRUE(replaced) << replaced.error().message();
  ASSERT_TRUE(read);
  EXPECT_EQ(Text(bytes), before);
}

//! The SHA-256 of a file is the SHA-256 of its bytes; a missing file is an
//! error, not an exception.
NOLINT_TEST_F(AtomicFileTest, ComputeFileSha256HashesTheFile)
{
  const auto path = Root() / "payload.bin";
  const auto text = std::string(70000, 'x') + "tail";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes(text)));

  const auto digest = oxygen::serio::ComputeFileSha256(path);
  const auto missing = oxygen::serio::ComputeFileSha256(Root() / "missing.bin");

  ASSERT_HAS_VALUE(digest);
  EXPECT_EQ(*digest, oxygen::base::ComputeSha256(Bytes(text)));
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error(), oxygen::serio::Error::kNotFound);
}

//! MoveFileOver moves the file over an existing destination; the source name
//! is gone afterwards.
NOLINT_TEST_F(AtomicFileTest, MoveFileOverReplacesAnExistingDestination)
{
  const auto from = Root() / "from.bin";
  const auto to = Root() / "to.bin";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(from, Bytes("new-bytes")));
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(to, Bytes("old")));

  const auto replaced = oxygen::serio::MoveFileOver(from, to);

  ASSERT_TRUE(replaced) << replaced.error().message();
  EXPECT_EQ(Read(to), "new-bytes");
  EXPECT_FALSE(std::filesystem::exists(from));
}

//! MoveFileOver moves the file to a name that does not exist yet.
NOLINT_TEST_F(AtomicFileTest, MoveFileOverMovesToANewName)
{
  const auto from = Root() / "from.bin";
  const auto to = Root() / "to.bin";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(from, Bytes("payload")));

  const auto replaced = oxygen::serio::MoveFileOver(
    from, to, oxygen::serio::Durability::kBuffered);

  ASSERT_TRUE(replaced) << replaced.error().message();
  EXPECT_EQ(Read(to), "payload");
  EXPECT_FALSE(std::filesystem::exists(from));
}

//! A reader that shares deletion keeps the bytes it opened while MoveFileOver
//! moves a new file over the name.
NOLINT_TEST_F(AtomicFileTest, MoveFileOverKeepsASharingReadersBytes)
{
  const auto from = Root() / "from.bin";
  const auto to = Root() / "to.bin";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(to, Bytes("before")));
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(from, Bytes("after-bytes")));
  auto reader = SharingReader(to);
  ASSERT_TRUE(reader.IsOpen());

  const auto replaced = oxygen::serio::MoveFileOver(from, to);

  ASSERT_TRUE(replaced) << replaced.error().message();
  EXPECT_EQ(reader.ReadAll(), "before");
  EXPECT_EQ(Read(to), "after-bytes");
}

//! A stable move between two folders publishes the file under its new name
//! and reports no durability error: both folders are flushed.
NOLINT_TEST_F(AtomicFileTest, MoveFileOverBetweenFoldersIsStable)
{
  const auto source_folder = Root() / "source";
  const auto target_folder = Root() / "target";
  std::filesystem::create_directory(source_folder);
  std::filesystem::create_directory(target_folder);
  const auto from = source_folder / "from.bin";
  const auto to = target_folder / "to.bin";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(from, Bytes("moved")));

  const auto replaced = oxygen::serio::MoveFileOver(from, to);

  ASSERT_TRUE(replaced) << replaced.error().message();
  EXPECT_FALSE(replaced->durability_error)
    << replaced->durability_error.message();
  EXPECT_EQ(Read(to), "moved");
  EXPECT_FALSE(std::filesystem::exists(from));
}

//! A read-only destination is replaced, as `rename(2)` replaces it, and the
//! new file is read-only too.
NOLINT_TEST_F(AtomicFileTest, ReplacesAReadOnlyDestinationAndKeepsItReadOnly)
{
  using std::filesystem::perms;
  const auto path = Root() / "kept.bin";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes("old")));
  constexpr auto kWrite
    = perms::owner_write | perms::group_write | perms::others_write;
  std::filesystem::permissions(
    path, kWrite, std::filesystem::perm_options::remove);
  const auto before = std::filesystem::status(path).permissions();

  const auto replaced
    = oxygen::serio::WriteFileAtomically(path, Bytes("new-bytes"));

  ASSERT_TRUE(replaced) << replaced.error().message();
  EXPECT_EQ(Read(path), "new-bytes");
  EXPECT_EQ(std::filesystem::status(path).permissions(), before);
  EXPECT_FALSE(oxygen::serio::testing::HasTemporarySibling(Root()));
  std::filesystem::permissions(
    path, perms::owner_write, std::filesystem::perm_options::add);
}

#ifdef OXYGEN_WINDOWS
//! Gives `path` a DACL from its SDDL text; the test's own file, so its owner
//! may always rewrite the DACL.
auto SetAccess(const std::filesystem::path& path, const wchar_t* sddl) -> bool
{
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl, SDDL_REVISION_1, &descriptor, nullptr)
    == FALSE) {
    return false;
  }
  const auto applied
    = SetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, descriptor);
  LocalFree(descriptor);
  return applied != FALSE;
}

//! A hidden destination stays hidden once replaced.
NOLINT_TEST_F(AtomicFileTest, ReplacementKeepsAHiddenDestinationHidden)
{
  const auto path = Root() / "hidden.bin";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes("old")));
  ASSERT_NE(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN), FALSE);

  const auto replaced
    = oxygen::serio::WriteFileAtomically(path, Bytes("new-bytes"));

  ASSERT_TRUE(replaced) << replaced.error().message();
  EXPECT_EQ(Read(path), "new-bytes");
  EXPECT_NE(GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_HIDDEN, 0U);
}

//! A read-only destination a sharing reader holds is replaced; the reader
//! keeps the bytes it opened.
NOLINT_TEST_F(AtomicFileTest, ReplacesAReadOnlyDestinationASharingReaderHolds)
{
  const auto path = Root() / "held.bin";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes("before")));
  ASSERT_NE(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY), FALSE);
  auto reader = SharingReader(path);
  ASSERT_TRUE(reader.IsOpen());

  const auto replaced
    = oxygen::serio::WriteFileAtomically(path, Bytes("after-bytes"));

  ASSERT_TRUE(replaced) << replaced.error().message();
  EXPECT_EQ(reader.ReadAll(), "before");
  EXPECT_EQ(Read(path), "after-bytes");
  ASSERT_NE(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL), FALSE);
}

//! A read-only destination whose attributes the caller may not change is
//! not replaced: the failure is kAccessDenied and the destination keeps its
//! bytes. Nothing clears the attribute in place.
NOLINT_TEST_F(AtomicFileTest, ReadOnlyDestinationWithoutAttributeRightsIsKept)
{
  const auto path = Root() / "denied.bin";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes("before")));
  ASSERT_NE(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY), FALSE);
  // Everyone may do anything but change the attributes.
  ASSERT_TRUE(SetAccess(path, L"D:P(D;;0x100;;;WD)(A;;FA;;;WD)"));

  const auto replaced
    = oxygen::serio::WriteFileAtomically(path, Bytes("after-bytes"));
  const auto kept = Read(path);
  const auto attributes = GetFileAttributesW(path.c_str());
  for (const auto& entry : std::filesystem::directory_iterator(Root())) {
    static_cast<void>(SetAccess(entry.path(), L"D:P(A;;FA;;;WD)"));
    static_cast<void>(
      SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL));
  }

  ASSERT_FALSE(replaced);
  EXPECT_EQ(replaced.error(), oxygen::serio::Error::kAccessDenied);
  EXPECT_EQ(kept, "before");
  EXPECT_NE(attributes & FILE_ATTRIBUTE_READONLY, 0U);
}
#endif

//! A missing source fails with kNotFound.
NOLINT_TEST_F(AtomicFileTest, MoveFileOverOfAMissingSourceIsNotFound)
{
  const auto replaced
    = oxygen::serio::MoveFileOver(Root() / "missing.bin", Root() / "to.bin");

  ASSERT_FALSE(replaced);
  EXPECT_EQ(replaced.error(), oxygen::serio::Error::kNotFound);
}

//! A folder at the source or at the destination fails with kIsDirectory.
NOLINT_TEST_F(AtomicFileTest, MoveFileOverWithAFolderIsADirectoryError)
{
  const auto file = Root() / "file.bin";
  const auto folder = Root() / "folder";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(file, Bytes("payload")));
  std::filesystem::create_directory(folder);

  const auto folder_source = oxygen::serio::MoveFileOver(folder, Root() / "to");
  const auto folder_target = oxygen::serio::MoveFileOver(file, folder);

  ASSERT_FALSE(folder_source);
  EXPECT_EQ(folder_source.error(), oxygen::serio::Error::kIsDirectory);
  ASSERT_FALSE(folder_target);
  EXPECT_EQ(folder_target.error(), oxygen::serio::Error::kIsDirectory);
  EXPECT_TRUE(std::filesystem::is_directory(folder));
  EXPECT_EQ(Read(file), "payload");
}

//! Buffered durability publishes the bytes like the stable default.
NOLINT_TEST_F(
  AtomicFileTest, WriteFileAtomicallyPublishesWithBufferedDurability)
{
  const auto path = Root() / "record.bin";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes("old")));

  const auto written = oxygen::serio::WriteFileAtomically(
    path, Bytes("buffered-bytes"), oxygen::serio::Durability::kBuffered);

  ASSERT_TRUE(written) << written.error().message();
  EXPECT_EQ(Read(path), "buffered-bytes");
  EXPECT_FALSE(oxygen::serio::testing::HasTemporarySibling(Root()));
}

//! An awaited publication writes the bytes, replaces an existing destination
//! and leaves no sibling, under either driver and either durability.
NOLINT_TEST_P(AsyncAtomicFileTest, PublishesAndLeavesNoSibling)
{
  const auto path = Root() / "record.bin";
  const auto bytes = oxygen::serio::testing::PatternBytes(200'000);
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, Bytes("old")));

  const auto stable = Publish(path, bytes, oxygen::serio::Durability::kStable);
  const auto after_stable = Read(path);
  const auto buffered
    = Publish(path, Bytes("short"), oxygen::serio::Durability::kBuffered);

  ASSERT_TRUE(stable) << stable.error().message();
  EXPECT_EQ(after_stable, Text(bytes));
  ASSERT_TRUE(buffered) << buffered.error().message();
  EXPECT_EQ(Read(path), "short");
  EXPECT_FALSE(oxygen::serio::testing::HasTemporarySibling(Root()));
  EXPECT_EQ(EntryCount(), 1);
}

//! An awaited publication to a new name creates the file.
NOLINT_TEST_P(AsyncAtomicFileTest, PublishesANewFile)
{
  const auto path = Root() / "new.bin";

  const auto written
    = Publish(path, Bytes("fresh"), oxygen::serio::Durability::kStable);

  ASSERT_TRUE(written) << written.error().message();
  EXPECT_EQ(Read(path), "fresh");
}

//! A failed awaited publication reports the error and leaves no sibling: a
//! missing parent is kNotFound.
NOLINT_TEST_P(AsyncAtomicFileTest, MissingParentFailsAndLeavesNothing)
{
  const auto path = Root() / "missing" / "record.bin";

  const auto written
    = Publish(path, Bytes("value"), oxygen::serio::Durability::kStable);

  ASSERT_FALSE(written);
  EXPECT_EQ(written.error(), oxygen::serio::Error::kNotFound);
  EXPECT_FALSE(std::filesystem::exists(path));
  EXPECT_EQ(EntryCount(), 0);
}

//! A folder at the destination fails the awaited publication, keeps the
//! folder and leaves no sibling.
NOLINT_TEST_P(AsyncAtomicFileTest, FolderDestinationFailsAndLeavesNoSibling)
{
  const auto folder = Root() / "record";
  std::filesystem::create_directory(folder);

  const auto written
    = Publish(folder, Bytes("value"), oxygen::serio::Durability::kStable);

  ASSERT_FALSE(written);
  EXPECT_TRUE(std::filesystem::is_directory(folder));
  EXPECT_FALSE(oxygen::serio::testing::HasTemporarySibling(Root()));
}

} // namespace
