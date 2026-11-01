# Integrity

Hashing and verifying bytes as they pass, so no producer or reader reads a
file a second time to check it.

## Rules

- **The hashes are Base's.** `base::Crc32` and `base::Sha256` use the CPU's
  hashing instructions where it has them, so on typical storage reading the
  bytes, not hashing them, dominates the cost of a check. Serio feeds them
  and owns no hash of its own; any type that satisfies `IncrementalHash` works
  the same way.
- **One pass.** A producer computes its digests while it writes; a reader
  verifies while it reads. Several hashes ride the same pass.
- **Skip ranges.** A field that holds a checksum is not part of what it
  covers. Hashing streams and region hashing take the ranges to leave out.

## Where each tool fits

| Bytes                                | Tool                                         |
| ------------------------------------ | -------------------------------------------- |
| A whole device or region, blocking   | `Hash`, `ComputeSha256`, `ComputeCrc32`      |
| A whole device or region, awaited    | `AsyncHash`, `AsyncComputeCrc32`             |
| A file by path                       | `ComputeFileSha256`                          |
| Bytes being written                  | `HashingWriteStream`                         |
| Bytes being read in order            | `HashingReadStream`                          |
| A region read against a known digest | `VerifiedRead`, fails with `kDigestMismatch` |

Region hashing reads through a buffer of at most 1 MiB. The awaited form
keeps one read in flight while it hashes the other buffer, so a verified pak
mount checks its CRC on the loop without a thread-pool worker reading the
file.

A hashing read stream only moves forward: a seek ahead hashes the bytes it
passes, and a seek back fails. A hashing write stream patches only inside a
skip range, since bytes already hashed cannot change.

## Example: a file that carries its own CRC

A producer writes a header whose CRC covers the whole file except the CRC
field itself, streams the content once, patches the CRC in, and publishes
the file atomically:

```cpp
struct BlobHeader {
  uint32_t magic;
  uint32_t crc; // the whole file's CRC, this field excluded
  uint64_t size;
};
template <> inline constexpr bool serio::kIsRecord<BlobHeader> = true;

auto PublishBlob(const std::filesystem::path& path,
  const std::span<const std::byte> payload) -> Result<FileReplacement>
{
  auto file = AtomicFileWriter::Create(path);
  if (!file) {
    return Err(file.error());
  }
  const auto crc_field = ByteRange {
    .offset = ByteOffset { offsetof(BlobHeader, crc) },
    .count = ByteCount { sizeof(uint32_t) },
  };
  auto hashing = HashingWriteStream<RegionWriteStream<NativeFile>,
    base::Crc32> { file->Stream(), std::span { &crc_field, 1 } };
  auto writer = Writer { hashing };
  CHECK_RESULT(writer.Write(
    BlobHeader { .magic = kBlobMagic, .crc = 0, .size = payload.size() }));
  CHECK_RESULT(writer.WriteBlob(payload));

  const auto [crc] = hashing.Digests();
  CHECK_RESULT(
    hashing.PatchAt(crc_field.offset, std::as_bytes(std::span { &crc, 1 })));
  return std::move(*file).Commit();
}
```

The bytes are read from `payload` once and written once; the CRC never
requires reading the file back. The pak writer computes the pak's CRC with a
hashing stream in the same pass that writes the pak.

## Decisions

- **Integrity is computed in passing.** No producer or reader reads bytes
  twice to hash them.
- **Formats choose what to hash.** Which bytes a digest covers and where it
  is stored belong to the format's owner; Serio hashes the ranges it is
  given.
