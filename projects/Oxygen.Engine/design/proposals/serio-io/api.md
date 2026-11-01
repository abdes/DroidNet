# Serio C++ API design

Status: proposal

Last updated: 2026-11-02

The C++23 shape of the [Serio IO and serialization proposal](README.md): the
layers as headers, the concepts every device, stream, backend and algorithm
satisfies, the few type-erased wrappers, and how each kind of extension plugs
in. The README states what Serio does; this document states how its API is
built so that adding a device, a backend or a hash never touches the code
around it.

## Contents

- [Principles](#principles)
- [Layers and headers](#layers-and-headers)
- [Vocabulary types](#vocabulary-types)
- [Buffers](#buffers)
- [Devices](#devices)
- [Type erasure](#type-erasure)
- [Regions](#regions)
- [IO driver and backends](#io-driver-and-backends)
- [Streams](#streams)
- [Algorithms](#algorithms)
- [Serialization](#serialization)
- [Publication](#publication)
- [Errors](#errors)
- [Platform code](#platform-code)
- [Test seams](#test-seams)
- [Extension points](#extension-points)
- [IO outside Serio](#io-outside-serio)
- [Decisions](#decisions)

## Principles

- **Concepts define the contracts.** A device, a stream, a backend and a hash
  are concepts with `noexcept` operations and exact result types. Generic code
  is constrained by them; a type that satisfies one is a device, stream,
  backend or hash, with no base class to derive from.
- **Open sets are erased once; closed sets are variants.** Devices, streams
  and readers are open (tests, tools and other modules add their own), so each
  concept has one value-semantic erased wrapper used at coarse boundaries: once
  per file, region or asset. IO backends are a closed, platform-known set, so
  the driver holds them in a `std::variant` and dispatches with `std::visit`.
  The serialization boundary is the one virtual interface: `AnyReader` and
  `AnyWriter` are abstract bases that `Reader<S>` and `Writer<S>` implement,
  because every type's hooks take them by reference and a loader holds one
  per asset, so their cost is the one the erasure rule already pays.
- **Capabilities are types, not runtime failures.** A device that cannot write
  does not satisfy `WritableDevice`; a device that cannot be awaited does not
  satisfy `AsyncReadableDevice`. Asking for what a type cannot do fails to
  compile, never with a run-time "unsupported" error.
- **Owners and views are distinct.** Devices, buffers, drivers and streams own
  their resources and are move-only. Regions and `ByteReader` are views, cheap
  to copy, with `std::span`'s lifetime rule: the owner outlives them.
- **No exceptions cross the API.** Every fallible operation is `noexcept` and
  returns `oxygen::Result<T>` (Base) carrying a `std::error_code`.
- **Each layer includes only the layers below it.** Public headers are
  platform-neutral; implementation lives in `Detail/` and `Platform/<os>/`.
- **No global state.** No singleton driver, no backend registry, no default
  device. Whatever an operation needs is passed to it.

## Layers and headers

| Layer         | Headers (under `Oxygen/Serio/`)                                                                                                | May include                     |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------ | ------------------------------- |
| Vocabulary    | `Types.h`, `Error.h`                                                                                                           | Base                            |
| Buffers       | `ByteBuffer.h` (`CopyOf`, `AsUnsigned`)                                                                                        | Vocabulary                      |
| Devices       | `Device.h` (concepts, `ReadExactlyAt`, `ReadAll`), `NativeFile.h`, `MemoryFile.h`, `MappedFile.h`, `ByteView.h`, `AnyDevice.h` | Buffers, Vocabulary             |
| Regions       | `Region.h`                                                                                                                     | Devices                         |
| Async IO      | `IoDriver.h`, `AsyncFile.h`, `AsyncDevice.h` (`AsyncReadExactlyAt`, `AsyncReadAll`)                                            | Regions, Devices, OxCo          |
| Streams       | `Stream.h` (concepts), `RegionStream.h`, `MemoryStream.h`, `AnyStream.h`, `CopyRegion.h`                                       | Regions, Async IO               |
| Algorithms    | `Integrity.h` (concept, hashing streams, region and file hashing, `AsyncHash`, `VerifiedRead`)                                 | Streams, Regions, Base hashes   |
| Serialization | `Reader.h`, `Writer.h`, `Record.h`, `ByteReader.h`, `CountingSink.h`, `RegionReader.h`, `RegionWriter.h`                       | Streams, Regions                |
| Publication   | `AtomicFile.h` (`AtomicFileWriter`, `WriteFileAtomically`, `AsyncWriteFileAtomically`, `MoveFileOver`), `FileLock.h`           | Streams, Async IO, Devices      |
| Test kit      | `Testing/FaultInjectingDevice.h`, `Testing/TestBytes.h` (the conformance suites are Serio's own tests)                         | Any layer; linked by tests only |

`Detail/` holds the erasure tables, the backend variant and buffer internals;
no header outside Serio includes it. A layer's tests include only that layer's
headers, so a forbidden include fails to compile in the test that owns it.

## Vocabulary types

```cpp
namespace oxygen::serio {

//! A position in a device and a number of bytes. Offset + count is an
//! offset; offset - offset is a count; nothing else mixes them.
using ByteOffset = NamedType<uint64_t, struct ByteOffsetTag,
  Comparable, Hashable, Printable>;
using ByteCount = NamedType<uint64_t, struct ByteCountTag,
  Comparable, Hashable, Printable, Addable, Subtractable>;

[[nodiscard]] constexpr auto operator+(ByteOffset, ByteCount) noexcept
  -> ByteOffset;
[[nodiscard]] constexpr auto operator-(ByteOffset, ByteOffset) noexcept
  -> ByteCount;

//! A half-open byte range. `Make` rejects an end past UINT64_MAX.
struct ByteRange {
  ByteOffset offset;
  ByteCount count;
  [[nodiscard]] static constexpr auto Make(ByteOffset, ByteCount) noexcept
    -> Result<ByteRange>;
  [[nodiscard]] constexpr auto End() const noexcept -> ByteOffset;
  [[nodiscard]] constexpr auto Contains(ByteRange) const noexcept -> bool;
};

enum class Durability : uint8_t { kBuffered, kStable };

//! Everything an open needs, by name (designated initializers).
struct OpenSpec {
  Access access = Access::kRead;
  Creation creation = Creation::kOpenExisting;
  Sharing sharing = Sharing::kReadDelete;
  AccessHint hint = AccessHint::kRandom;
  bool unbuffered = false;
};

} // namespace oxygen::serio
```

## Buffers

`ByteBuffer` is the one owner of bytes read from or for a device: an aligned,
move-only allocation whose bytes are indeterminate until written. Every
module that holds a payload holds one, so a loader hands the buffer it read
to the resource that keeps it, and nothing copies it into a `std::vector`.

```cpp
class ByteBuffer {
public:
  [[nodiscard]] static auto Allocate(ByteCount size,
    size_t alignment = kIoAlignment) noexcept -> Result<ByteBuffer>;
  //! A buffer holding a copy of `bytes`; `not_enough_memory` when the
  //! allocation fails.
  [[nodiscard]] static auto CopyOf(std::span<const std::byte> bytes,
    size_t alignment = kIoAlignment) noexcept -> Result<ByteBuffer>;
  [[nodiscard]] auto Bytes() noexcept -> std::span<std::byte>;
  [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>;
  //! `Bytes()` as unsigned bytes, for APIs that take `uint8_t`.
  [[nodiscard]] auto Unsigned() const noexcept -> std::span<const uint8_t>;
  // Size, Empty, Truncate as today.
};

//! Any byte span as unsigned bytes: the one place the cast is written.
[[nodiscard]] auto AsUnsigned(std::span<const std::byte> bytes) noexcept
  -> std::span<const uint8_t>;
[[nodiscard]] auto AsUnsigned(std::span<std::byte> bytes) noexcept
  -> std::span<uint8_t>;
```

- **One copy, one cast.** `CopyOf` is the only way bytes held elsewhere enter
  a buffer, and `AsUnsigned` the only `std::byte` to `uint8_t` view; both
  return or view, never throw. Accessing `std::byte` storage through
  `unsigned char` (`uint8_t`) is allowed aliasing, so the view is defined
  behaviour.
- **Resources own the buffer they were read into.** Texture, buffer, script
  and physics resources take a `ByteBuffer` and expose it as `uint8_t` with
  `Unsigned()`; none has a span constructor. A producer whose bytes are not
  a loader's read (a collision shape job, PakDump, the Scripting source
  resolver, a synchronous script query) copies them into a buffer with
  `CopyOf` or reads them into one with `ReadAll`.

## Devices

```cpp
template <class D>
concept ReadableDevice = std::movable<D>
  && requires(const D& d, ByteOffset at, std::span<std::byte> out) {
       { d.Size() } noexcept -> std::same_as<Result<ByteCount>>;
       { d.ReadAt(at, out) } noexcept -> std::same_as<Result<ByteCount>>;
     };

template <class D>
concept WritableDevice = ReadableDevice<D>
  && requires(D& d, ByteOffset at, std::span<const std::byte> in,
       ByteCount size, Durability durability) {
       { d.WriteAt(at, in) } noexcept -> std::same_as<Result<void>>;
       { d.Reserve(size) } noexcept -> std::same_as<Result<void>>;
       { d.Flush(durability) } noexcept -> std::same_as<Result<void>>;
     };

//! The whole device is one span of memory: parsers read views of it.
template <class D>
concept ContiguousDevice = ReadableDevice<D> && requires(const D& d) {
  { d.Bytes() } noexcept -> std::same_as<std::span<const std::byte>>;
};
```

| Device                    | Readable | Writable | Contiguous | Awaitable                                        |
| ------------------------- | -------- | -------- | ---------- | ------------------------------------------------ |
| `NativeFile`              | Yes      | Yes      | No         | No: open the file through a driver               |
| `AsyncFile`               | Yes      | Yes      | No         | Yes, through its driver (a loop or inline)       |
| `MemoryFile`              | Yes      | Yes      | Yes        | Yes, by itself: operations are ready at once     |
| `MappedFile`              | Yes      | No       | Yes        | Never: a read is a page fault on the caller      |
| `ByteView`                | Yes      | No       | Yes        | Yes, by itself: operations are ready at once     |
| `Region<D>`               | Yes      | No       | As `D`     | As `D`                                           |
| `FaultInjectingDevice<D>` | As `D`   | As `D`   | No         | As `D`                                           |
| `AnyDevice`               | Yes      | Run time | Run time   | No: an `AnyAsyncDevice` is the awaitable erasure |

- **Contract for every device,** checked by the conformance suite: `const`
  operations are safe from any number of threads at once (a device has no
  cursor); `ReadAt` across the end returns the bytes that exist; a zero-length
  operation succeeds and does nothing; errors compare equal to the same
  `serio::Error` conditions on every device.
- **Free algorithms, not members,** build on the concepts once:
  `ReadExactlyAt(device, at, out)` (fails with `kEndOfData`) works for every
  device. `WriteAt` already writes all of its bytes or fails, so it needs no
  `WriteAllAt` counterpart. `ReadAll(device, limit)` reads a whole device or
  region into a new `ByteBuffer` of its size, failing with `kTooLarge`
  before allocating when the size passes the caller's `limit` and with
  `kEndOfData` when the device ends first, and `AsyncReadAll` is its awaitable form; they replace every hand-written allocate-and-read.
- **Opening is a static factory** returning `Result<Device>`:
  `NativeFile::Open(path, OpenSpec)`, `MappedFile::Open(path)`. A constructor
  never fails.
- **A mapping can share writes.** `MappedFile::Open(path, sharing)` maps a
  file another handle holds open for writing when `sharing` is
  `kReadWriteDelete` (an import's resource data file, which writers append
  to); the default refuses one.
- **A folder is not a file.** `NativeFile::Open` of a folder fails with
  `kIsDirectory` on every platform: Windows refuses it with a denied access
  and POSIX opens it read-only, and Serio reports both alike.
- **`MappedFile` is never awaitable, not even through an adapter.** A view
  is parsed in place, and a read of it blocks the reader on page faults that
  no operation can report. A consumer that also awaits payload reads from
  the same file opens it a second time, through its driver, as an
  `AsyncFile`: a mounted pak's `PakFileSource` does, while `PakFile` parses
  the mapping.

## Type erasure

Each open concept has one erased, move-only wrapper. It owns its device,
stores it inline when small (a `NativeFile` is a handle and a path) and on the
heap otherwise, and dispatches through a `static constexpr` table of function
pointers generated per type in `Detail/`, so there is one indirect call per
operation and no virtual base:

| Wrapper           | Wraps any                                      | Adds                                                         |
| ----------------- | ---------------------------------------------- | ------------------------------------------------------------ |
| `AnyDevice`       | `ReadableDevice`                               | `Writable()`, `Contiguous() -> optional<span>` from the type |
| `AnyAsyncDevice`  | `AsyncReadableDevice`                          | Everything `AnyDevice` has, plus the awaitable operations    |
| `AnyInputStream`  | `InputStream`                                  |                                                              |
| `AnyOutputStream` | `OutputStream`                                 |                                                              |
| `AnyReader`       | `Reader<S>`, `RegionReader<D>` (abstract base) | The boundary a loader receives once per asset                |
| `AnyWriter`       | `Writer<S>`, `RegionWriter<D>` (abstract base) | The boundary an encoder receives once per artifact           |

`AnyDevice` itself satisfies `ReadableDevice` (and `ContiguousDevice` is a
run-time query on it, because the erased type is no longer known), so every
generic algorithm works on it unchanged. It satisfies `WritableDevice` too:
`Writable()` reports whether the erased device writes, and a write to one that
does not fails with `operation_not_supported`. This is the one run-time
capability check, and it exists only where the type has been erased. The
erased stream wrappers require their stream's `Position()` and `Size()` to be
`const`. Shared ownership, when a consumer
needs it (a mounted pak shared by its sources), is that consumer's
`std::shared_ptr<const AnyDevice>`; Serio does not impose it.

## Regions

```cpp
//! A bounded, non-owning view of a device. Bounds are checked against the
//! region, not the device.
template <ReadableDevice D> class Region {
public:
  [[nodiscard]] static auto Make(const D& device, ByteRange range) noexcept
    -> Result<Region>;
  [[nodiscard]] static auto Whole(const D& device) noexcept -> Result<Region>;
  [[nodiscard]] auto Device() const noexcept -> const D&;
  [[nodiscard]] auto Range() const noexcept -> ByteRange;
  [[nodiscard]] auto Sub(ByteRange relative) const noexcept -> Result<Region>;
  [[nodiscard]] auto ReadAt(ByteOffset relative, std::span<std::byte> out)
    const noexcept -> Result<ByteCount>;
  [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>
    requires ContiguousDevice<D>;
  //! The view of a contiguous device, known from the type or, for an
  //! `AnyDevice`, at run time; empty otherwise.
  [[nodiscard]] auto Contiguous() const noexcept
    -> std::optional<std::span<const std::byte>>;
};
using AnyRegion = Region<AnyDevice>;
```

A region satisfies `ReadableDevice` itself, so a region of a region, a stream
over a region and a hash of a region are the same code as over a device.
`Make` checks the range against the device's size when the region is made,
which suits read regions. A writable device grows as it is written, so a
fixed bound means nothing for it: there is no writable region, and a
`RegionWriteStream` takes a device and a start offset instead.

## IO driver and backends

```cpp
//! One per event loop, or one inline driver for code without a loop. Owns
//! the backend and every file registered with it.
class IoDriver {
public:
  [[nodiscard]] static auto Create(asio::io_context& loop) noexcept
    -> Result<IoDriver>;
  //! A driver whose operations complete on the awaiting thread, which
  //! blocks for each of them: for tools and tests that have no loop.
  [[nodiscard]] static auto CreateInline() noexcept -> Result<IoDriver>;
  [[nodiscard]] auto Inline() const noexcept -> bool;
  [[nodiscard]] auto Open(const std::filesystem::path& path, OpenSpec spec)
    noexcept -> Result<AsyncFile>;
  [[nodiscard]] auto AllocateBuffer(ByteCount size) noexcept
    -> Result<ByteBuffer>;
};

// Device.h, beside the blocking concepts; Operation.h defines the two
// operation types every device returns.
template <class D>
concept AsyncReadableDevice = ReadableDevice<D>
  && requires(const D& d, ByteOffset at, std::span<std::byte> out) {
       { d.AsyncReadAt(at, out) } noexcept -> std::same_as<ReadOperation>;
     };
template <class D>
concept AsyncWritableDevice = AsyncReadableDevice<D> && WritableDevice<D>
  && requires(D& d, ByteOffset at, std::span<const std::byte> in) {
       { d.AsyncWriteAt(at, in) } noexcept -> std::same_as<WriteOperation>;
     };
```

- **An `AsyncFile` is a file bound to a driver.** It has the blocking
  operations of a `NativeFile` and the awaitable ones, which submit to its
  driver. A file opened without a driver is a `NativeFile` and has no awaitable
  form, so nothing awaits a file that cannot complete asynchronously.
- **One operation type per direction.** Every device returns a
  `ReadOperation` or a `WriteOperation`: a memory device's is ready at once
  (`ReadOperation::Ready`, nothing suspends), a file's is bound to its driver.
  Code generic over `AsyncReadableDevice` reads memory and files alike (R4,
  P4), and `AnyAsyncDevice` erases the device without a coroutine frame,
  because its table returns the same type for every device.
- **Operations are plain awaiters,** objects holding the operation's
  arguments and its target (driver, handle, backend slot), implementing
  OxCo's protocol: `await_suspend` submits, `await_cancel` asks the backend to
  cancel that operation alone, `await_must_resume` holds the coroutine until
  the OS has released the buffer. No coroutine frame is allocated per
  operation; `AsyncReadExactlyAt` and `CopyRegion` are coroutines over them.
- **The backend set is closed.** An internal concept states what a backend
  provides, and the driver holds one alternative of a variant:

  ```cpp
  namespace oxygen::serio::detail {
  template <class B>
  concept IoBackend = requires(B& b, NativeHandle h, BackendFileSlot slot,
    IoRequest& request) {
    { b.Register(h) } noexcept -> std::same_as<Result<BackendFileSlot>>;
    { b.Unregister(slot) } noexcept -> std::same_as<void>;
    // false: completed at once, the waiter is not resumed
    { b.Submit(request) } noexcept -> std::same_as<bool>;
    { b.Cancel(request) } noexcept -> std::same_as<void>;
    { b.AllocateBuffer(ByteCount{}) } noexcept -> std::same_as<Result<ByteBuffer>>;
  };
  // Every platform: InlineBackend; Windows: IocpBackend; Linux: UringBackend.
  using Backend = std::variant<InlineBackend, IocpBackend>;
  } // namespace oxygen::serio::detail
  ```

  `Create` selects the loop alternative from the OS's capabilities, and
  `CreateInline` the inline one. Adding or deleting a backend changes the
  variant and its platform file, nothing else. A backend is trivially
  copyable: its state is the loop it serves, or nothing. Every async suite
  runs once per backend of its platform, inline included.

- **Inline** (`Detail/InlineBackend`, platform-neutral): `Register` keeps the
  handle, `Submit` performs the request with the file's blocking `ReadAt` or
  `WriteAt` and returns `false` (completed at once, so the awaiter never
  suspends), and `Cancel` has nothing to cancel. `AllocateBuffer` is
  `ByteBuffer::Allocate`.

- **IOCP** (`Platform/Windows/IocpBackend`): a registered file is associated
  with the loop's completion port through an asio handle owning a duplicate
  of the file's handle; each chunk is a `ReadFile`/`WriteFile` on an
  `asio::windows::overlapped_ptr`, and a cancellation is `CancelIoEx` of that
  request. Blocking operations on the same file wait on a per-thread event
  whose low bit keeps their completion off the port.
- **io_uring** (`Platform/Posix/UringBackend`): OxCo builds asio with
  `ASIO_HAS_IO_URING` and liburing, so the loop's context owns a ring; a
  registered file is an `asio::random_access_file` over a duplicate
  descriptor, and each request carries its own cancellation signal.

### Awaiting a blocking device

A consumer that awaits reads (a content source, a pak's payloads, an import
job) always holds a driver, and opens every file it awaits through it. Code
with a loop holds a loop driver; a tool or a unit test without one holds an
inline driver. The consumer is the same code in both cases: it never asks
whether a driver exists, and never wraps a device to make it look
awaitable.

| Device a consumer holds           | How it is awaited                                      |
| --------------------------------- | ------------------------------------------------------ |
| A file it opens by path           | `driver.Open(path, spec)`, an `AsyncFile`              |
| Memory (`MemoryFile`, `ByteView`) | By itself; its operations are ready at once            |
| A region of either                | `Region<D>::AsyncReadAt`, as its device                |
| A mapped view (`MappedFile`)      | Never; the same file is also opened through the driver |

- **Inline mode, not an adapter.** An adapter over an open device (the
  `BlockingReads` that content wrote) makes any device look awaitable,
  mapped views included, and leaves every consumer branching on whether it
  has a driver. The inline driver keeps the one rule above: only a driver
  makes a file awaitable, and which kind of driver is decided once, by the
  code that owns the loop or knows it has none.
- **Everything else is unchanged.** An inline `AsyncFile` is an `AsyncFile`:
  it reads and writes, erases into `AnyAsyncDevice`, has regions,
  `Contiguous()` empty, and `CopyRegion` and `AsyncHash` run over it, each
  operation completing before the awaiter would suspend.
- **Owners decide the mode once.** The asset loader creates a loop driver
  from its configured loop, and an inline driver, logged at INFO, when it is
  given none; its sources receive that driver, never a null one, and a
  loose source that reads payloads refuses to open without one. Only the
  loader awaits pak payloads, so the payload file belongs to its
  `PakFileSource`: `PakFile` itself, which the cooker also reads, holds only
  the mapping and needs no driver. The path resolver, which reads no
  payload, holds an inline driver.
- **A synchronous query reads blocking.** `ReadScriptResourceForAsset` is a
  synchronous API by contract: it reads the payload with `ReadAll` of the
  source's payload region, which is an `AsyncFile`'s blocking read, into the
  `ByteBuffer` the script resource then owns.

## Streams

```cpp
template <class S>
concept InputStream = requires(S& s, std::span<std::byte> out, ByteOffset at) {
  { s.ReadSome(out) } noexcept -> std::same_as<Result<ByteCount>>;
  { s.Position() } noexcept -> std::same_as<ByteOffset>;
  { s.Size() } noexcept -> std::same_as<ByteCount>;
  { s.Seek(at) } noexcept -> std::same_as<Result<void>>;
};
template <class S>
concept OutputStream = requires(S& s, std::span<const std::byte> in) {
  { s.Write(in) } noexcept -> std::same_as<Result<void>>;
  { s.Flush() } noexcept -> std::same_as<Result<void>>;
  { s.Position() } noexcept -> std::same_as<ByteOffset>;
};
```

- Streams are templates over their source: `RegionReadStream<D>`,
  `RegionWriteStream<D>`, `MemoryStream`, `ReadOnlyMemoryStream`.
  `Position()` and `Size()` cannot fail: a stream knows both.
  `OpenReadStream(device)` is a read stream over a whole device, in one call.
- **A read stream's buffer never exceeds its region.** `RegionReadStream`
  allocates `min(buffer_size, region count)`, so reading a 200-byte
  descriptor allocates 200 bytes, not 64 KiB, and no caller sizes the buffer
  itself. A write stream has no region, so its buffer stays the size asked.
- **A contiguous region is read through its device.** A read stream over
  memory or a mapped file has no buffer, and each read is one `ReadAt` of the
  device into the caller's span, never a copy from a view the stream kept: a
  mapped file copies under its fault guard, and a memory file that grew or
  shrank since the stream was made is read as it is now. A write stream
  refuses (`value_too_large`) a write whose device offsets would pass the
  largest offset, as `CountingSink` refuses a count that would.
- **Patching is positional, not a seek.** An output stream has no `Seek`. A
  writer that completes a header once its content is known overwrites bytes
  it already wrote with `PatchAt(ByteOffset, bytes)`: buffered bytes in the
  buffer, flushed ones with `WriteAt`. The cursor never moves, so the patch
  cannot leave it in the wrong place, and a patch past `Position()` fails
  with `kOutOfBounds`. `RegionWriteStream` and `MemoryStream` provide it, and
  callers that patch are constrained on `PatchableOutputStream`, which
  refines `OutputStream` with `PatchAt`.
- `ReadExactly(stream, out)` and the rest of the stream algorithms are free
  functions over the concepts, written once.
- **Decorators are streams over streams,** templates over their inner stream,
  so they compose without erasure: `HashingReadStream<RegionReadStream<D>,
base::Crc32, base::Sha256>` is one pass for two hashes.
- `CopyRegion(from, to)` is the double-buffered awaitable copy, constrained on
  `AsyncReadableDevice` and `AsyncWritableDevice`.

## Algorithms

Pluggable computations over bytes are concepts too:

```cpp
//! An incremental digest. base::Crc32 and base::Sha256 satisfy it.
template <class H>
concept IncrementalHash = std::default_initializable<H>
  && requires(H& h, std::span<const std::byte> bytes) {
       { h.Update(bytes) } noexcept -> std::same_as<void>;
       { std::move(h).Finalize() } noexcept;
     };
```

- `HashingReadStream<S, H...>` and `HashingWriteStream<S, H...>` take any
  number of hashes and the caller's skip ranges, and hand back `Digests()` in
  the order of `H...`. A hashing read stream only moves forward: a seek
  forward hashes the bytes it passes, a seek back fails. A hashing write
  stream patches only inside a skip range, since hashed bytes cannot change.
- `Hash<H...>(region)` reads a device or a region once through a buffer of at
  most 1 MiB and returns a tuple of digests; `ComputeSha256(region)` and
  `ComputeCrc32(region)` are its one-hash spellings, and
  `ComputeFileSha256(path)` opens a file and hashes it. All return errors,
  never throw.
- `AsyncHash<H...>(region, skip)` is `Hash` over an `AsyncReadableDevice`:
  a coroutine, double-buffered like `CopyRegion`, that keeps one read in
  flight while it hashes the other buffer, and returns the same tuple.
  `AsyncComputeCrc32` and `AsyncComputeSha256` are its one-hash spellings.
  Over an inline driver it is `Hash` with one more buffer.
- `VerifiedRead(region, expected, out)` is `Hash<base::Sha256>` fused with the
  read into `out`, failing with `kDigestMismatch`.
- **A verified pak mount hashes through the driver.** The loader opens the
  pak through its driver, reads the footer, and awaits `AsyncComputeCrc32`
  of the file, skipping the footer's CRC field, on the loop; only parsing the
  directory runs on the thread pool, and no pool worker reads the file to
  hash it. The footer rules (a zero CRC refused or skipped, a mismatch
  reported as corrupt) are one content function, shared with the blocking
  verified open that tools without a loop keep, which hashes with
  `ComputeCrc32`.

A new hash is a type that satisfies the concept; nothing in Serio changes. A
codec or a signature scheme is not an algorithm here: they live in their own
modules ([scope and layering](README.md#scope-and-layering)).

## Serialization

- **`Reader<S>` and `Writer<S>`** are templates over a stream; `AnyReader` and
  `AnyWriter` erase them at a loader's or an encoder's boundary.
- **One customization point per direction:** the `serio::Load(reader, value)`
  and `serio::Store(writer, value)` function objects dispatch, in order:
  1. **Records:** a type declared a record by
     `template <> inline constexpr bool kIsRecord<T> = true;` moves as one
     copy, aligned like any value. Its layout is checked at compile time
     (`RecordLayout`): trivially copyable, without padding (packed to one
     byte, or with a unique object representation, which a struct with
     floating-point members never has) and a little-endian target.
     `LoadRecord` and `StoreRecord` are the copy itself. A record is a raw
     wire layout: Serio never checks its values, alone or in a sequence, and
     the code that uses a field decides whether to check it. A type that must
     be checked on every read is not declared a record; its own hook calls
     `LoadRecord`, then checks. The layout check does not reach nested
     members, so a nested struct is itself packed or has a unique object
     representation.
  2. **Sequences:** a `vector`, `array` or `span` of records or arithmetic
     elements (`BulkElement`) moves as its count, one alignment and one copy;
     other elements one by one. The count is checked against the bytes that
     remain before anything is allocated.
  3. **Built-ins:** arithmetic types, enums (as their underlying type),
     `bool` (one byte, 0 or 1), `std::string`, `std::optional` (a `bool`,
     then the value), `std::array` (stored like a vector).

  A caller reads and writes every kind the same way: `reader.Read<T>()` and
  `writer.Write(value)`. 4. **A type's own hook,** found by ADL: `Load(AnyReader&, T&)` and
  `Store(AnyWriter&, const T&)`, compiled once in the type's own source
  file.

  Records are opt-in because a trivially copyable type can still hold a
  pointer or padding that must never reach a file.

- **`ByteReader`** is concrete, not erased: a cursor over a span with
  `Read<T>` for records and arithmetic values (returns a copy, so no object
  is aliased from raw bytes), `ReadBytes(n)` and `ReadStringView(n)` (views
  of bytes and characters only), `Seek`, `Skip` and `AlignTo`.
  `CopyAt<T>(bytes, at)` is its one-read form, for a value whose offset is
  known.
- **`CountingSink`** satisfies `PatchableOutputStream` and counts, so
  measuring a value is writing it to a sink through a `Writer`.
- **Region readers and writers** are the `AnyReader` and `AnyWriter` a
  consumer hands on, with the stream and the reader they need inside them:

  ```cpp
  //! Reads a region; positions are relative to it and every read is
  //! bounded by it. A contiguous region is read through its device.
  template <ReadableDevice D> class RegionReader final : public AnyReader {
  public:
    //! Views `region`, whose device outlives the reader.
    explicit RegionReader(Region<D> region) noexcept;
    //! Owns `device` and reads all of it.
    [[nodiscard]] static auto Owning(D device) noexcept
      -> Result<std::unique_ptr<RegionReader>>;
  };

  //! Writes a device from a start offset, through a `RegionWriteStream`.
  template <WritableDevice D> class RegionWriter final : public AnyWriter {
  public:
    explicit RegionWriter(D& device, ByteOffset start = {}) noexcept;
    [[nodiscard]] static auto Owning(D device, ByteOffset start = {})
      noexcept -> Result<std::unique_ptr<RegionWriter>>;
    [[nodiscard]] auto Stream() noexcept -> RegionWriteStream<D>&;
    [[nodiscard]] auto Device() noexcept -> D&;
  };
  ```

  Both are neither copyable nor movable, since the reader or writer inside
  refers to the stream beside it, so an owning one is returned in a
  `unique_ptr`. It declares its device before its stream, so the stream is
  destroyed, and flushed, first. `Owning` reports the device's size error
  instead of throwing. Content's descriptor and table readers,
  `AtomicFileWriter`'s state and the cooker's in-memory file system's
  writer, each of which kept a device, a stream and its member order by
  hand, use them.

- **Bounded reads:** `AnyReader::Remaining()` is the bytes a reader may still
  read; length prefixes and `ReadBlob` fail with `kTooLarge` past it.
- **Alignment** is the reader's or writer's state, set by an RAII
  `ScopedAlignment`, and free when it is 1.

## Publication

```cpp
//! What a replacement leaves: the name moved; `durability_error` holds the
//! failure to make the move stable, if any.
struct FileReplacement {
  std::error_code durability_error;
};

//! Moves the file `from` over `to` in one step, replacing the file `to`
//! names, if any.
[[nodiscard]] auto MoveFileOver(const std::filesystem::path& from,
  const std::filesystem::path& to) noexcept -> Result<FileReplacement>;
```

- **One replacing rename.** `MoveFileOver` is the rename `AtomicFileWriter`
  commits with, exposed: POSIX semantics on Windows (readers that share
  deletion keep the file they opened), a replacing move where the file
  system has none, and `rename(2)` elsewhere, each made durable. Both paths
  are on one volume. `AtomicFileWriter::Commit` and `WriteFileAtomically`
  return the same `FileReplacement`; `AtomicFileCommit` is deleted.
- **Errors come from the move, not from checks before it.** A missing `from`
  fails with `kNotFound`; a folder at `from` or `to` fails with
  `kIsDirectory`, which the platform code classifies after the move failed.
  The one check before the move is POSIX's: `rename(2)` moves a folder to a
  free name, so a folder source is refused first. The source is the caller's
  own file, so nothing replaces it between the check and the move.
  `CookerStorage::Move` is `MoveFileOver`, and its contract names these
  conditions.
- **A stable move flushes every folder it changes.** The destination's
  folder, and the source's when the move crosses folders. The permission copy
  is made through a handle that flushes it too, so the metadata changed after
  the data flush is as durable as the data.
- **The code is platform code.** The Windows rename moves out of
  `AtomicFile.cpp` into `Platform/Windows/MoveFileOver.cpp`, and the POSIX
  one into `Platform/Posix/`; `AtomicFile.cpp` has no `#if` on the platform.

- **Whole files publish atomically, awaited or not.**
  `WriteFileAtomically(path, bytes, durability)` and its awaitable form
  `AsyncWriteFileAtomically(driver, path, bytes, durability)` write a unique
  sibling (through the driver for the awaitable one), flush it to the
  durability asked, and `MoveFileOver` it over `path`. `kStable` is the
  default and waits for the disk before the rename; `kBuffered` leaves the
  bytes to the OS cache and renames at once, for outputs a stable record
  elsewhere validates (cooked descriptors and tables, whose digests the
  stable loose index holds).
- **`FileLock::Acquire` waits.** Beside `TryAcquire`, `Acquire(path, mode,
open_mode)` blocks until the lock is granted (`LockFileEx` without
  `LOCKFILE_FAIL_IMMEDIATELY`, `flock` without `LOCK_NB`). Each acquisition
  opens its own handle, so two threads of one process exclude each other as
  two processes do.

## Errors

- `Result<T>` is Base's; Serio adds no result type.
- `enum class serio::Error` with `make_error_code`, `make_error_condition`, its
  category and the `std::is_error_condition_enum` specialization: Serio's own
  codes (`kEndOfData`, `kOutOfBounds`, `kTooLarge`, `kDigestMismatch`) and the
  conditions OS codes map to (`kNotFound`, `kAccessDenied`,
  `kSharingViolation`, `kNoSpace`, `kIsDirectory`, `kPathTooLong`, `kIo`).
- `Error` is a condition enumeration only. With `std::is_error_code_enum` as
  well, `code == Error::kNotFound` would be ambiguous between the code and the
  condition comparison. Serio makes its own codes explicitly with
  `make_error_code(Error::kEndOfData)`, and every comparison goes through the
  condition.
- OS failures stay `std::system_category()` codes from
  `base::LastSystemError()`, and compare equal to the conditions.

## Platform code

- `NativeHandle` is an opaque value type (`void*` on Windows, `int` on POSIX)
  declared in a public header; nothing public includes `<windows.h>`.
- `Platform/Windows/` holds `NativeFile`, `MappedFile`, `MoveFileOver` and
  `IocpBackend`; `Platform/Posix/` holds their POSIX counterparts and the
  io_uring backend; `Detail/InlineBackend` is the same on every platform. CMake selects the directory; a public header carries no
  `#if` on the platform, and `detail::Backend` is the one alias that differs.

## Test seams

- `FaultInjectingDevice<D>` is a device over a device: it satisfies the same
  concepts as `D`, except `ContiguousDevice` (it never exposes a view, so
  every read takes the copying path where the faults are). It fails the
  operations a test schedules (short reads, failed writes, a full disk) and
  counts the calls it receives, so a test can check how a buffered stream
  batches its IO. Every stream, region and serializer suite runs through it.
- `DeviceConformance.h` is a typed GoogleTest suite over a list of device
  factories; a new device joins it with one line, and so does any device
  another module defines. `StreamConformance.h` does the same for input and
  output streams, and `TestBytes.h` holds deterministic test bytes and a
  byte-range matcher for large buffers.

## Extension points

| To add             | Write                                                      | Changes elsewhere                             |
| ------------------ | ---------------------------------------------------------- | --------------------------------------------- |
| A device           | A type satisfying the device concepts it can honour        | One line in the conformance suite's type list |
| An IO backend      | A type satisfying `detail::IoBackend`, in `Platform/<os>/` | The variant alternative and the backend list  |
| A stream decorator | A template over `InputStream` or `OutputStream`            | None                                          |
| A hash             | A type satisfying `IncrementalHash`                        | None                                          |
| A record format    | The packed struct and its `kIsRecord` specialization       | None                                          |
| A variable format  | `Load`/`Store` hooks for the type, beside it               | None                                          |

## IO outside Serio

Every IO helper written outside `src/Oxygen/Serio` is either a gap this API
closes, a duplicate of what Serio already has, or IO that is not Serio's.

**Gaps closed, the helper deleted:**

| Helper (module)                                                                                                                        | Replaced by                                                  |
| -------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------ |
| `BlockingReads` and its no-driver branches (Content: `LooseCookedSource`, `PakFile::ReadsOf`)                                          | The inline driver; `PakFileSource` opens payloads through it |
| `RegionReader` (Content: pak and loose sources, `ResourceLoadPipeline`)                                                                | `serio::RegionReader`                                        |
| Device, stream and member order kept by hand (`AtomicFileWriter`, cooker `MemoryFileSystem`)                                           | `serio::RegionWriter`                                        |
| `CopyOf` twice, span constructors (Data: `TextureResource`, `BufferResource`)                                                          | `ByteBuffer::CopyOf`; the constructors are deleted           |
| `reinterpret_cast` to `uint8_t` (Data: texture and buffer resources)                                                                   | `ByteBuffer::Unsigned`, `AsUnsigned`                         |
| `std::vector<uint8_t>` payloads (Data: `ScriptResource`, `PhysicsResource`; their loaders copy)                                        | A `ByteBuffer` the loader's read fills, one copy fewer       |
| Allocate-and-read (Content: `ScriptQueryService`; Cooker: `PakSourceDevices::Read`; Scripting: `ExternalFileResolver` with `ifstream`) | `ReadAll`                                                    |
| `memcpy` of headers and layouts out of a payload (Data: `TextureResource`)                                                             | `ByteReader`                                                 |
| `StagePayload` (Content test fixtures)                                                                                                 | `ByteBuffer::CopyOf` of the payload's subspan                |
| `RealCookerStorage::Move` (Cooker: `fs::rename` after existence checks)                                                                | `MoveFileOver`                                               |
| Blocking CRC pass on a pool worker (Content: verified pak mount)                                                                       | `AsyncComputeCrc32` on the loop                              |
| `ReadPublishedFile` returning `std::vector<std::byte>` (Serio)                                                                         | Returns a `ByteBuffer`                                       |

**Duplicates of existing Serio facilities, migrated onto it:**

| Helper (module)                                                                                                    | Becomes                                                                                                                                                                                                                                                |
| ------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `WindowsFileReader`, `WindowsFileWriter`, `IAsyncFileWriter`, `FileError` (Cooker import)                          | [Import IO](#import-io)                                                                                                                                                                                                                                |
| Bytecode cache (Engine: `ScriptCompilationService`)                                                                | Header and index entries are records; the snapshot is an `AtomicFileWriter` through a `Writer`, the header patched last; reads are `OpenPublishedFile` and `ReadExactlyAt`. `PublishCacheFile`, the temporary name, its guard and `put()` are deleted. |
| `ReadDescriptorBytes`, `TryReadAssetHeader`, `ReadStructAt`, unchecked `ReadRecordAt`, six buffer copies (PakDump) | A `ByteReader` over `PakFile::DescriptorBytes`, bounds checked, with no copy                                                                                                                                                                           |
| `<pid>.json.tmp`, then an unchecked `std::filesystem::rename` (Control: `ControlServer`)                           | `WriteFileAtomically` of the discovery file, which holds the control token                                                                                                                                                                             |
| CVar archive and history truncated in place, read with `ifstream` (Console: `Registry`)                            | `WriteFileAtomically` and `ReadPublishedFile`; a script file stays a text stream, written by people                                                                                                                                                    |
| In-process path-keyed mutex around the loose index (Content: `LooseCookedIndexImpl`, Cooker: `LooseCookedWriter`)  | A blocking `FileLock::Acquire` of the sibling `container.index.lock`, held by a writer for its read, merge and publication; readers take no lock, since the replacement gives them a whole index                                                       |
| `ScriptSourceBlob` owning a `std::vector<uint8_t>` (Engine: Scripting)                                             | Owns a `ByteBuffer`; `BytesView()` is its `Unsigned()`                                                                                                                                                                                                 |
| Record tables copied with `memcpy` (Cooker: `PakPlanBuilder`)                                                      | `ReadAll` of the region and a `ByteReader`                                                                                                                                                                                                             |

The in-process mutex excluded only one process's writers, so a cooker and
an editor merging the same cooked root lost each other's records; the file
lock closes that, and the test that held the mutex holds the lock instead.

### Import IO

The import reads its sources and writes its cooked files through its own
asio stack, beside Serio: `WindowsFileReader` and `WindowsFileWriter` open
raw overlapped handles on the import loop, map errors to their own
`FileError`, write whole files truncating them in place, and poll a pending
count to flush. The cooked files it writes are the ones the loose cooked
writer then publishes atomically through `CookerStorage`, so one cooked root
has two writers with two error vocabularies.

- **One loop, one driver.** The import thread's `ImportEventLoop` owns a
  `serio::IoDriver` over its `asio::io_context`, and every import read and
  cooked write goes through it. `ImportManifest::AnalyzeSources`, which
  builds its own loop, builds its own driver with it.
- **Source reads stay a port.** `IAsyncFileReader` is the import's seam for
  sources: `ImportSourceSnapshot` decorates it to record and replay reads,
  and tests substitute it. Its one implementation, `FileSourceReader`, opens
  each source through the driver and awaits `AsyncReadAll` or, for a range,
  `AsyncReadExactlyAt`. `ReadFile` returns a `ByteBuffer`; `ReadOptions`
  keeps `offset` and `max_bytes` and loses the ignored `size_hint` and
  `alignment`. `GetFileInfo` and `Exists` are file system metadata, not
  bytes, and stay `std::filesystem` queries.
- **Cooked writes go through the storage port, by role.** `IAsyncFileWriter`
  and its implementation are deleted. The two awaited writes the emitters
  need form `ImportStorage`, a `CookerStorage` that the real and the
  in-memory storages implement, so the in-memory test storage covers the
  import too. A pak build and script sealing publish through `CookerStorage`
  alone, so the pak tool's staging overlay implements no import write. Both
  roles hold `LockExclusive`, which serializes the loose index's writers:
  - `PublishAsync(path, bytes)`: `AsyncWriteFileAtomically` with
    `kBuffered`, for asset and resource descriptors and resource tables.
    They become atomic replacements instead of files truncated in place, so
    a cancelled or failed import never leaves a torn descriptor.
  - `OpenData(path) -> AnyAsyncDevice`: a data file (`buffers.data`,
    `textures.data`, `physics.data`, `scripts.data`) opened once through the
    driver, sharing writes, and held by its table aggregator, which already
    reserves each write's range. Emitters await `AsyncWriteAt` at the
    reserved offset, padding included, instead of opening a handle per
    write.
- **Writes in flight are a nursery, not a counter.** Each emitter starts its
  writes in the import session's nursery, keeps the first error, and
  `Finalize` joins them. The callbacks, `WriteAsync`, `WriteAtAsync`,
  `CancelAll`, `PendingCount` and the zero-delay polling `Flush` are
  deleted; cancelling the session cancels its writes through the driver.
  The per-job `generation_writer` is gone, as there is no writer object.
- **Errors are `std::error_code`.** `FileError`, `FileErrorInfo`,
  `MapSystemError` and `MapWindowsError` are deleted. Ports return
  `Result<T>` with Serio's conditions (`kNotFound`, `kIsDirectory`, ...)
  and `std::errc::operation_canceled`; a caller that reports an error names
  the path in its diagnostic, which `FileErrorInfo` carried before. The
  branches on `kNotFound` and `kCancelled` compare against those
  conditions.

**Parsed inputs, read through Serio with a limit:** the import manifest, the
ImportTool and Inspector requests, `LaunchFile`, Scripting
`WriteDefinitions` (which compares and republishes its output) and
ShaderBake's sources, module artifacts, manifest and build state. Each
whole-file read names the limit of its family of files, listed in the
[Serio README](../../../src/Oxygen/Serio/README.md#io-outside-serio), and
`cooked::ReadWholeFile` takes one too.

**Not Serio's, they stay:** text written for people through iostreams,
where a torn file is rewritten whole on the next run (Inspector and
ImportTool JSON reports, the Vortex GPU profiler traces, crash dumps,
benchmark CSV and JSON), console scripts read line by line, the logger's
own `fopen`, tests and benchmarks with their fixtures (the Cooker test
kit's `FileIo`), and pixel buffers cast for image codecs, which are not
`ByteBuffer`s.

## Decisions

- **Inline driver over an awaiting adapter.** Only a driver makes a file
  awaitable, so no consumer branches on a missing driver and a mapped view
  can never pass as an awaitable device.
- **`ByteBuffer` is the one payload owner.** Copies and the unsigned view
  are written once in Serio; resources take the buffer their loader read.
- **Region readers and writers live in Serio,** so no consumer keeps a
  device, a stream and a reader in the right member order by hand.
- **One replacing rename,** with errors from the move itself and the
  platform code under `Platform/`.
- **`AnyReader` and `AnyWriter` stay abstract bases.** Hooks take them by
  reference and the cost is once per asset; the principle states it.
- **The import is a Serio consumer like any other.** One driver on its
  loop, one source-read port, cooked writes through `CookerStorage` only,
  errors as `std::error_code`.
- **Cooked files publish atomically with buffered durability;** the loose
  index, which holds their digests, publishes with stable durability.
- **The loose index is guarded by a file lock,** which excludes writers in
  other processes as well as threads.
- **Concepts over base classes.** Contracts are checked at compile time,
  extension needs no inheritance, and hot paths are direct calls.
- **One erasure per concept, used at boundaries.** Erasure costs one indirect
  call per operation; it is paid once per file, region or asset, never per
  field.
- **Closed sets are variants.** Backends are known per platform; a variant
  keeps them out of the public API and out of virtual dispatch.
- **Capabilities in types.** Writable, contiguous and awaitable are concepts,
  so an unsupported operation is a compile error, not a run-time error code.
- **Views follow `std::span`.** Regions and readers do not own; owners are
  move-only; shared ownership is a consumer's choice.
- **Records are opt-in.** Only a type declared a record is copied as bytes.
