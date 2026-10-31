//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include <Oxygen/Base/Crc32.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! An incremental digest: bytes fed in any number of pieces give the digest
//! of all of them. `base::Crc32` and `base::Sha256` satisfy it.
template <class H>
concept IncrementalHash = std::default_initializable<H> && std::copyable<H>
  && requires(H& h, std::span<const std::byte> bytes) {
       { h.Update(bytes) } noexcept -> std::same_as<void>;
       { std::move(h).Finalize() } noexcept;
     };

//! The digest an incremental hash produces.
template <IncrementalHash H>
using DigestOf = decltype(std::declval<H&>().Finalize());

//! The size of the buffer a region is hashed through.
inline constexpr ByteCount kHashBufferSize { 1024ULL * 1024 };

namespace detail {
  //! Several hashes fed the same bytes.
  template <IncrementalHash... H> class HashSet {
  public:
    auto Update(const std::span<const std::byte> bytes) noexcept -> void
    {
      std::apply(
        [bytes](auto&... hash) { (hash.Update(bytes), ...); }, hashes_);
    }

    //! The digests of the bytes so far; hashing may continue.
    [[nodiscard]] auto Digests() const -> std::tuple<DigestOf<H>...>
    {
      return std::apply(
        [](auto... copies) {
          return std::tuple<DigestOf<H>...> { copies.Finalize()... };
        },
        hashes_);
    }

  private:
    std::tuple<H...> hashes_;
  };

  //! Hashes `bytes`, which start at `at`, except the parts inside `skips`.
  template <IncrementalHash... H>
  auto UpdateExcept(HashSet<H...>& hashes, const ByteOffset at,
    std::span<const std::byte> bytes, const std::span<const ByteRange> skips)
    -> void
  {
    auto position = at.get();
    while (!bytes.empty()) {
      const auto end = position + bytes.size();
      // The skip range that starts first among those this piece reaches.
      auto next_skip = std::optional<ByteRange> {};
      for (const auto& skip : skips) {
        if (skip.count.get() == 0 || skip.End().get() <= position
          || skip.offset.get() >= end) {
          continue;
        }
        if (!next_skip || skip.offset < next_skip->offset) {
          next_skip = skip;
        }
      }
      if (!next_skip) {
        hashes.Update(bytes);
        return;
      }
      if (next_skip->offset.get() > position) {
        const auto hashed
          = static_cast<size_t>(next_skip->offset.get() - position);
        hashes.Update(bytes.first(hashed));
        bytes = bytes.subspan(hashed);
        position += hashed;
      }
      const auto skipped = static_cast<size_t>(
        (std::min)(next_skip->End().get(), end) - position);
      bytes = bytes.subspan(skipped);
      position += skipped;
    }
  }

  //! Reads all of `source` once through a buffer of at most
  //! `kHashBufferSize`, handing each piece to `consume`.
  template <ReadableDevice D, class Consume>
  auto ReadInPieces(const D& source, const Consume& consume) noexcept
    -> Result<void>
  {
    const auto size = source.Size();
    if (!size) {
      return ::oxygen::Err(size.error());
    }
    if (*size == ByteCount { 0 }) {
      return {};
    }
    auto buffer = ByteBuffer::Allocate((std::min)(*size, kHashBufferSize));
    if (!buffer) {
      return ::oxygen::Err(buffer.error());
    }
    auto at = ByteOffset { 0 };
    while (at.get() < size->get()) {
      const auto read = source.ReadAt(at, buffer->Bytes());
      if (!read) {
        return ::oxygen::Err(read.error());
      }
      if (read->get() == 0) {
        return ::oxygen::Err(make_error_code(Error::kEndOfData));
      }
      const auto piece
        = buffer->Bytes().first(static_cast<size_t>(read->get()));
      consume(std::span<const std::byte> { piece });
      at += *read;
    }
    return {};
  }
} // namespace detail

//! An input stream that hashes every byte read through it, except the skip
//! ranges its owner names (a field that holds the checksum itself).
/*!
 Every byte is hashed once, in order, so the stream only moves forward: a
 `Seek` forward reads and hashes the bytes it passes, and a `Seek` back fails
 with `not_supported`. Skip ranges are offsets of the inner stream.

 ```cpp
 auto hashing = HashingReadStream<decltype(stream), base::Crc32,
   base::Sha256> { stream, std::array { crc_field } };
 // ... read through `hashing` ...
 const auto [crc, sha] = hashing.Digests();
 ```
*/
template <InputStream S, IncrementalHash... H>
  requires(sizeof...(H) > 0)
class HashingReadStream {
public:
  explicit HashingReadStream(
    S& inner, const std::span<const ByteRange> skips = {})
    : inner_(&inner)
    , skips_(skips.begin(), skips.end())
  {
  }

  [[nodiscard]] auto ReadSome(const std::span<std::byte> out) noexcept
    -> Result<ByteCount>
  {
    const auto at = inner_->Position();
    const auto read = inner_->ReadSome(out);
    if (read) {
      detail::UpdateExcept(hashes_, at,
        std::span<const std::byte> {
          out.first(static_cast<size_t>(read->get())) },
        skips_);
    }
    return read;
  }

  [[nodiscard]] auto Position() const noexcept -> ByteOffset
  {
    return inner_->Position();
  }

  [[nodiscard]] auto Size() const noexcept -> ByteCount
  {
    return inner_->Size();
  }

  //! Moves forward by reading and hashing the bytes up to `at`.
  [[nodiscard]] auto Seek(const ByteOffset at) noexcept -> Result<void>
  {
    if (at < Position()) {
      return ::oxygen::Err(std::make_error_code(std::errc::not_supported));
    }
    if (at.get() > Size().get()) {
      return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
    }
    auto passed = std::array<std::byte, 4096> {};
    while (Position() < at) {
      const auto count = static_cast<size_t>(
        (std::min)(uint64_t { passed.size() }, at.get() - Position().get()));
      CHECK_RESULT(ReadExactly(*this, std::span { passed }.first(count)));
    }
    return {};
  }

  //! The digests of the bytes read so far, in the order of `H...`.
  [[nodiscard]] auto Digests() const -> std::tuple<DigestOf<H>...>
  {
    return hashes_.Digests();
  }

private:
  S* inner_;
  std::vector<ByteRange> skips_;
  detail::HashSet<H...> hashes_;
};

//! An output stream that hashes every byte written through it, except the
//! skip ranges its owner names (a field that holds the checksum itself).
/*!
 A patch may only overwrite bytes inside a skip range: bytes already hashed
 cannot change. Skip ranges are offsets of the inner stream.

 ```cpp
 auto hashing = HashingWriteStream<decltype(stream), base::Crc32> {
   stream, std::array { crc_field } };
 // ... write through `hashing` ...
 const auto [crc] = hashing.Digests();
 CHECK_RESULT(hashing.PatchAt(crc_field.offset, std::as_bytes(...)));
 ```
*/
template <OutputStream S, IncrementalHash... H>
  requires(sizeof...(H) > 0)
class HashingWriteStream {
public:
  explicit HashingWriteStream(
    S& inner, const std::span<const ByteRange> skips = {})
    : inner_(&inner)
    , skips_(skips.begin(), skips.end())
  {
  }

  [[nodiscard]] auto Write(const std::span<const std::byte> in) noexcept
    -> Result<void>
  {
    const auto at = inner_->Position();
    CHECK_RESULT(inner_->Write(in));
    detail::UpdateExcept(hashes_, at, in, skips_);
    return {};
  }

  [[nodiscard]] auto Flush() noexcept -> Result<void>
  {
    return inner_->Flush();
  }

  [[nodiscard]] auto Position() const noexcept -> ByteOffset
  {
    return inner_->Position();
  }

  //! Overwrites bytes inside one skip range; `kOutOfBounds` elsewhere.
  [[nodiscard]] auto PatchAt(const ByteOffset at,
    const std::span<const std::byte> in) noexcept -> Result<void>
    requires PatchableOutputStream<S>
  {
    const auto patch = ByteRange { .offset = at, .count = CountOf(in.size()) };
    const auto inside = std::ranges::any_of(
      skips_, [patch](const ByteRange skip) { return skip.Contains(patch); });
    if (!inside) {
      return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
    }
    return inner_->PatchAt(at, in);
  }

  //! The digests of the bytes written so far, in the order of `H...`.
  [[nodiscard]] auto Digests() const -> std::tuple<DigestOf<H>...>
  {
    return hashes_.Digests();
  }

private:
  S* inner_;
  std::vector<ByteRange> skips_;
  detail::HashSet<H...> hashes_;
};

//! Hashes all of `source`, a device or a region, reading it once, except the
//! bytes inside `skips` (a field that holds the checksum itself); the digests
//! come in the order of `H...`.
template <IncrementalHash... H, ReadableDevice D>
  requires(sizeof...(H) > 0)
[[nodiscard]] auto Hash(
  const D& source, const std::span<const ByteRange> skips = {}) noexcept
  -> Result<std::tuple<DigestOf<H>...>>
{
  auto hashes = detail::HashSet<H...> {};
  auto at = ByteOffset { 0 };
  CHECK_RESULT(detail::ReadInPieces(
    source, [&hashes, &at, skips](const std::span<const std::byte> piece) {
      detail::UpdateExcept(hashes, at, piece, skips);
      at += CountOf(piece.size());
    }));
  return ::oxygen::Ok(hashes.Digests());
}

//! The SHA-256 of all of `source`, a device or a region, except `skips`.
template <ReadableDevice D>
[[nodiscard]] auto ComputeSha256(
  const D& source, const std::span<const ByteRange> skips = {}) noexcept
  -> Result<base::Sha256Digest>
{
  auto digests = Hash<base::Sha256>(source, skips);
  if (!digests) {
    return ::oxygen::Err(digests.error());
  }
  return ::oxygen::Ok(std::get<0>(*digests));
}

//! The CRC32 of all of `source`, a device or a region, except `skips`.
template <ReadableDevice D>
[[nodiscard]] auto ComputeCrc32(const D& source,
  const std::span<const ByteRange> skips = {}) noexcept -> Result<uint32_t>
{
  auto digests = Hash<base::Crc32>(source, skips);
  if (!digests) {
    return ::oxygen::Err(digests.error());
  }
  return ::oxygen::Ok(std::get<0>(*digests));
}

//! The SHA-256 of the file at `path`, read once sequentially.
[[nodiscard]] inline auto ComputeFileSha256(const std::filesystem::path& path)
  -> Result<base::Sha256Digest>
{
  const auto file
    = NativeFile::Open(path, OpenSpec { .hint = AccessHint::kSequential });
  if (!file) {
    return ::oxygen::Err(file.error());
  }
  return ComputeSha256(*file);
}

//! Reads all of `source` into `out` and checks its SHA-256 against `expected`
//! as the bytes pass; `kDigestMismatch` when it differs, `kOutOfBounds` when
//! `out` is not the size of `source`.
/*!
 The bytes are hashed right after each piece lands in `out`, while they are
 in cache: verifying costs no second read.
*/
template <ReadableDevice D>
[[nodiscard]] auto VerifiedRead(const D& source,
  const base::Sha256Digest& expected, const std::span<std::byte> out) noexcept
  -> Result<void>
{
  const auto size = source.Size();
  if (!size) {
    return ::oxygen::Err(size.error());
  }
  if (CountOf(out.size()) != *size) {
    return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
  }
  auto hash = base::Sha256 {};
  constexpr auto kPieceSize = static_cast<size_t>(kHashBufferSize.get());
  for (size_t at = 0; at < out.size(); at += kPieceSize) {
    const auto piece = out.subspan(at, (std::min)(kPieceSize, out.size() - at));
    CHECK_RESULT(ReadExactlyAt(source, ByteOffset { at }, piece));
    hash.Update(piece);
  }
  if (hash.Finalize() != expected) {
    return ::oxygen::Err(make_error_code(Error::kDigestMismatch));
  }
  return {};
}

} // namespace oxygen::serio
