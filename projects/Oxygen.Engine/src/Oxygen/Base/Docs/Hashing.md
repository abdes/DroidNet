# Hashes, hex and system errors

Base holds one implementation of each byte primitive the engine shares. These
work on memory spans and read no files.

## Hashes

| Hash                                     | Use                                    |
| ---------------------------------------- | -------------------------------------- |
| [`Sha256`](../Sha256.h), `ComputeSha256` | Content identity and integrity digests |
| [`Crc32`](../Crc32.h), `ComputeCrc32`    | The IEEE checksum of the pak footer    |

Both are incremental: `Update` takes the data in any number of pieces and gives
the same result as one call over all of it. `Crc32::Finalize` keeps the state,
so a caller can read the checksum so far and keep feeding bytes. Skipping a
range, such as the pak footer's own CRC field, is the caller's concern: it
feeds the bytes around the range.

`Crc32` is the reflected IEEE polynomial `0xEDB88320` with initial value and
final XOR `0xFFFFFFFF` (zlib, PNG). It picks the fastest path the CPU has at
run time:

1. AVX-512 VPCLMULQDQ folding, sixteen 16-byte lanes at a time, from 256 bytes.
2. PCLMULQDQ folding, four lanes at a time, from 64 bytes.
3. Slicing-by-8 tables for shorter inputs and for the tail.

The tests check every path against the table path at every length and
alignment, and `Oxygen.Base.Hashing.Benchmarks` measures them; benchmark only
Release builds.

## Hex text

[`Hex.h`](../Hex.h) is the one codec for bytes as text. `ToHexString` writes
two lowercase digits per byte; `FromHexString` reads exactly two digits of
either case per output byte and refuses any other text. `ToHexString` of a
`Sha256Digest` and `Sha256DigestFromHex` spell digests through it, and `Uuid`
parses its digit groups through it. Formatting one integer as hex is number
formatting: use `fmt` (`{:#x}`).

## System errors

[`LastSystemError`](../SystemError.h) converts the calling thread's last OS
error, `GetLastError()` on Windows and `errno` elsewhere, into a
`std::error_code` in `std::system_category()`. Call it right after the failed
call. Compare results portably through `std::errc` conditions, for example
`error == std::errc::no_such_file_or_directory`.
