# Serio C++ API design

Status: proposal

Last updated: 2026-10-31

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
- [Devices](#devices)
- [Type erasure](#type-erasure)
- [Regions](#regions)
- [IO queue and backends](#io-queue-and-backends)
- [Streams](#streams)
- [Algorithms](#algorithms)
- [Serialization](#serialization)
- [Errors](#errors)
- [Platform code](#platform-code)
- [Test seams](#test-seams)
- [Extension points](#extension-points)
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
  the queue holds them in a `std::variant` and dispatches with `std::visit`.
  Nothing has a public virtual interface.
- **Capabilities are types, not runtime failures.** A device that cannot write
  does not satisfy `WritableDevice`; a device that cannot be awaited does not
  satisfy `AsyncReadableDevice`. Asking for what a type cannot do fails to
  compile, never with a run-time "unsupported" error.
- **Owners and views are distinct.** Devices, buffers, queues and streams own
  their resources and are move-only. Regions and `ByteReader` are views, cheap
  to copy, with `std::span`'s lifetime rule: the owner outlives them.
- **No exceptions cross the API.** Every fallible operation is `noexcept` and
  returns `oxygen::Result<T>` (Base) carrying a `std::error_code`.
- **Each layer includes only the layers below it.** Public headers are
  platform-neutral; implementation lives in `Detail/` and `Platform/<os>/`.
- **No global state.** No singleton queue, no backend registry, no default
  device. Whatever an operation needs is passed to it.

## Layers and headers

| Layer         | Headers (under `Oxygen/Serio/`)                                                                    | May include                     |
| ------------- | -------------------------------------------------------------------------------------------------- | ------------------------------- |
| Vocabulary    | `Types.h`, `Error.h`                                                                               | Base                            |
| Buffers       | `ByteBuffer.h`                                                                                     | Vocabulary                      |
| Devices       | `Device.h` (concepts), `NativeFile.h`, `MemoryFile.h`, `MappedFile.h`, `ByteView.h`, `AnyDevice.h` | Buffers, Vocabulary             |
| Regions       | `Region.h`                                                                                         | Devices                         |
| Async IO      | `IoQueue.h`, `AsyncFile.h`, `AsyncDevice.h` (concept and erasure)                                  | Regions, Devices, OxCo          |
| Streams       | `Stream.h` (concepts), `RegionStream.h`, `MemoryStream.h`, `AnyStream.h`, `CopyRegion.h`           | Regions, Async IO               |
| Algorithms    | `Hashing.h` (concept, hashing streams, region hashing), `VerifiedRead.h`                           | Streams, Regions, Base hashes   |
| Serialization | `Reader.h`, `Writer.h`, `Record.h`, `ByteReader.h`, `CountingSink.h`                               | Streams, Regions                |
| Publication   | `AtomicFileWriter.h`, `AtomicFile.h`, `FileLock.h`                                                 | Streams, Devices                |
| Test support  | `Testing/FaultInjectingDevice.h`, `Testing/DeviceConformance.h`, `Testing/IoBackends.h`            | Any layer; linked by tests only |

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

| Device       | Readable | Writable | Contiguous | Async |
| ------------ | -------- | -------- | ---------- | ----- |
| `NativeFile` | Yes      | Yes      | No         | No    |
| `AsyncFile`  | Yes      | Yes      | No         | Yes   |
| `MemoryFile` | Yes      | Yes      | Yes        | Yes   |
| `MappedFile` | Yes      | No       | Yes        | No    |
| `ByteView`   | Yes      | No       | Yes        | Yes   |

- **Contract for every device,** checked by the conformance suite: `const`
  operations are safe from any number of threads at once (a device has no
  cursor); `ReadAt` across the end returns the bytes that exist; a zero-length
  operation succeeds and does nothing; errors compare equal to the same
  `serio::Error` conditions on every device.
- **Free algorithms, not members,** build on the concepts once:
  `ReadExactlyAt(device, at, out)` (fails with `kEndOfData`) and
  `WriteAllAt(device, at, in)` work for every device.
- **Opening is a static factory** returning `Result<Device>`:
  `NativeFile::Open(path, OpenSpec)`, `MappedFile::Open(path)`. A constructor
  never fails.
- **`MappedFile` is not async.** A view is parsed in place; a consumer that
  also awaits payload reads from the same file opens it as an `AsyncFile`.

## Type erasure

Each open concept has one erased, move-only wrapper. It owns its device,
stores it inline when small (a `NativeFile` is a handle and a path) and on the
heap otherwise, and dispatches through a `static constexpr` table of function
pointers generated per type in `Detail/`, so there is one indirect call per
operation and no virtual base:

| Wrapper           | Wraps any                   | Adds                                                         |
| ----------------- | --------------------------- | ------------------------------------------------------------ |
| `AnyDevice`       | `ReadableDevice`            | `Writable()`, `Contiguous() -> optional<span>` from the type |
| `AnyAsyncDevice`  | `AsyncReadableDevice`       | Everything `AnyDevice` has, plus the awaitable operations    |
| `AnyInputStream`  | `InputStream`               |                                                              |
| `AnyOutputStream` | `OutputStream`              |                                                              |
| `AnyReader`       | `Reader<S>`, `ByteReader`   | The boundary a loader receives once per asset                |
| `AnyWriter`       | `Writer<S>`, `CountingSink` | The boundary an encoder receives once per artifact           |

`AnyDevice` itself satisfies `ReadableDevice` (and `ContiguousDevice` is a
run-time query on it, because the erased type is no longer known), so every
generic algorithm works on it unchanged. Shared ownership, when a consumer
needs it (a mounted pak shared by its sources), is that consumer's
`std::shared_ptr<const AnyDevice>`; Serio does not impose it.

## Regions

```cpp
//! A bounded, non-owning view of a device. Bounds are checked against the
//! region, not the device.
template <ReadableDevice D> class Region {
public:
  [[nodiscard]] static constexpr auto Make(const D& device, ByteRange range)
    noexcept -> Result<Region>;
  [[nodiscard]] auto Device() const noexcept -> const D&;
  [[nodiscard]] auto Range() const noexcept -> ByteRange;
  [[nodiscard]] auto Sub(ByteRange relative) const noexcept -> Result<Region>;
  [[nodiscard]] auto ReadAt(ByteOffset relative, std::span<std::byte> out)
    const noexcept -> Result<ByteCount>;
  [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>
    requires ContiguousDevice<D>;
};
using AnyRegion = Region<AnyDevice>;
```

A region satisfies `ReadableDevice` itself, so a region of a region, a stream
over a region and a hash of a region are the same code as over a device.
`WritableRegion<D>` is the same over a `WritableDevice`.

## IO queue and backends

```cpp
//! One per event loop. Owns the backend and every file registered with it.
class IoQueue {
public:
  [[nodiscard]] static auto Create(asio::io_context& loop) noexcept
    -> Result<IoQueue>;
  [[nodiscard]] auto Open(const std::filesystem::path& path, OpenSpec spec)
    noexcept -> Result<AsyncFile>;
  [[nodiscard]] auto AllocateBuffer(ByteCount size) noexcept
    -> Result<ByteBuffer>;
};

template <class D>
concept AsyncReadableDevice = ReadableDevice<D>
  && requires(const D& d, ByteOffset at, std::span<std::byte> out) {
       { d.AsyncReadAt(at, out) } -> co::Awaitable<Result<ByteCount>>;
     };
template <class D>
concept AsyncWritableDevice = AsyncReadableDevice<D> && WritableDevice<D>
  && requires(D& d, ByteOffset at, std::span<const std::byte> in) {
       { d.AsyncWriteAt(at, in) } -> co::Awaitable<Result<void>>;
     };
```

- **An `AsyncFile` is a file bound to a queue.** It has the blocking
  operations of a `NativeFile` and the awaitable ones, which submit to its
  queue. A file opened without a queue is a `NativeFile` and has no awaitable
  form, so nothing awaits a file that cannot complete asynchronously.
- **Memory devices are async for free:** their awaitables are `co::Just`
  results, so code generic over `AsyncReadableDevice` reads memory and files
  alike (R4, P4).
- **Operations are plain awaiters,** objects holding the operation's
  arguments and its backend slot, implementing OxCo's protocol:
  `await_suspend` submits, `await_cancel` asks the backend to cancel,
  `await_must_resume` holds the coroutine until the OS has released the
  buffer. No coroutine frame is allocated per operation.
- **The backend set is closed.** An internal concept states what a backend
  provides, and the queue holds one alternative of a variant:

  ```cpp
  namespace oxygen::serio::detail {
  template <class B>
  concept IoBackend = requires(B& b, NativeHandle h, IoOperation& op) {
    { b.Register(h) } noexcept -> std::same_as<Result<BackendFileSlot>>;
    { b.Unregister(BackendFileSlot{}) } noexcept -> std::same_as<void>;
    { b.Submit(op) } noexcept -> std::same_as<void>;   // batched per turn
    { b.Cancel(op) } noexcept -> std::same_as<void>;
    { b.AllocateBuffer(ByteCount{}) } noexcept -> std::same_as<Result<ByteBuffer>>;
  };
  using Backend = std::variant<IocpBackend, IoRingBackend>; // per platform
  } // namespace oxygen::serio::detail
  ```

  `Create` selects the alternative from the OS's capabilities. Adding or
  deleting a backend changes the variant and its platform file, nothing else.
  Tests and benchmarks force one through `Testing/IoBackends.h`; product code
  cannot name one.

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
  number of hashes and the caller's skip ranges.
- `Hash<H...>(region)` reads a region once with one 1 MiB buffer and returns a
  tuple of digests; `ComputeSha256(region)` and `ComputeCrc32(region)` are its
  one-hash spellings.
- `VerifiedRead(region, expected, out)` is `Hash<base::Sha256>` fused with the
  read into `out`, failing with `kDigestMismatch`.

A new hash is a type that satisfies the concept; nothing in Serio changes. A
codec or a signature scheme is not an algorithm here: they live in their own
modules ([scope and layering](README.md#scope-and-layering)).

## Serialization

- **`Reader<S>` and `Writer<S>`** are templates over a stream; `AnyReader` and
  `AnyWriter` erase them at a loader's or an encoder's boundary.
- **One customization point per direction:** the `serio::Load(reader, value)`
  and `serio::Store(writer, value)` function objects dispatch, in order:
  1. **Records:** a type declared a record by
     `template <> inline constexpr bool kIsRecord<T> = true;`, checked at
     compile time for `trivially_copyable`,
     `has_unique_object_representations` and a little-endian target, moves as
     one copy (`ReadRecord<T>`, `WriteRecord<T>`).
  2. **Sequences:** a contiguous range of records or arithmetic elements moves
     as its count and one copy; other ranges element by element.
  3. **Built-ins:** arithmetic types, enums (as their underlying type),
     `bool`, `std::string`, `std::optional`, `std::array`.
  4. **A type's own hook,** found by ADL: `Load(AnyReader&, T&)` and
     `Store(AnyWriter&, const T&)`, compiled once in the type's own source
     file.

  Records are opt-in because a trivially copyable type can still hold a
  pointer or padding that must never reach a file.

- **`ByteReader`** is concrete, not erased: a cursor over a span with
  `ReadRecord<T>` (returns a copy, so no object is aliased from raw bytes),
  `ReadBytes(n)` and `ReadStringView(n)` (views of bytes and characters only).
- **`CountingSink`** satisfies `OutputStream` and counts, so measuring a record
  is writing it to a sink.
- **Alignment** is the reader's or writer's state, set by an RAII
  `ScopedAlignment`, and free when it is 1.

## Errors

- `Result<T>` is Base's; Serio adds no result type.
- `enum class serio::Error` with `make_error_code`, `make_error_condition`, its
  category and the `std::is_error_code_enum` and `std::is_error_condition_enum`
  specializations: Serio's own codes (`kEndOfData`, `kOutOfBounds`,
  `kTooLarge`, `kDigestMismatch`) and the conditions OS codes map to
  (`kNotFound`, `kAccessDenied`, `kSharingViolation`, `kNoSpace`, `kIo`).
- OS failures stay `std::system_category()` codes from
  `base::LastSystemError()`, and compare equal to the conditions.

## Platform code

- `NativeHandle` is an opaque value type (`void*` on Windows, `int` on POSIX)
  declared in a public header; nothing public includes `<windows.h>`.
- `Platform/Win32/` holds `NativeFile`, `MappedFile`, `IocpBackend` and
  `IoRingBackend`; `Platform/Posix/` holds their POSIX counterparts and the
  io_uring backend. CMake selects the directory; a public header carries no
  `#if` on the platform, and `detail::Backend` is the one alias that differs.

## Test seams

- `FaultInjectingDevice<D>` is a device over a device: it satisfies the same
  concepts as `D` and fails the operations a test schedules (short reads,
  failed writes, a full disk). Every stream, region and serializer suite runs
  through it.
- `DeviceConformance.h` is a typed GoogleTest suite over a list of device
  factories; a new device joins it with one line, and so does any device
  another module defines.
- `IoBackends.h` lets tests and benchmarks create a queue on a named backend,
  so every suite that touches an `AsyncFile` runs once per backend.

## Extension points

| To add             | Write                                                      | Changes elsewhere                             |
| ------------------ | ---------------------------------------------------------- | --------------------------------------------- |
| A device           | A type satisfying the device concepts it can honour        | One line in the conformance suite's type list |
| An IO backend      | A type satisfying `detail::IoBackend`, in `Platform/<os>/` | The variant alternative and the backend list  |
| A stream decorator | A template over `InputStream` or `OutputStream`            | None                                          |
| A hash             | A type satisfying `IncrementalHash`                        | None                                          |
| A record format    | The packed struct and its `kIsRecord` specialization       | None                                          |
| A variable format  | `Load`/`Store` hooks for the type, beside it               | None                                          |

## Decisions

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
