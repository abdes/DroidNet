# Extending Serio

Serio's contracts are concepts, so most extensions are a new type and nothing
else. The conformance suites hold every implementation to the same behaviour.

## Extension points

| To add             | Write                                                               | Changes elsewhere                              |
| ------------------ | ------------------------------------------------------------------- | ---------------------------------------------- |
| A device           | A type satisfying the device concepts it can honour                 | A factory in the conformance suites' lists     |
| An IO backend      | A type satisfying the internal backend concept, in `Platform/<os>/` | The backend variant and the async suites' list |
| A stream decorator | A template over `InputStream` or `OutputStream`                     | None                                           |
| A hash             | A type satisfying `IncrementalHash`                                 | None                                           |
| A record format    | The struct and its `kIsRecord` declaration                          | None                                           |
| A variable format  | `Load` and `Store` hooks beside the type                            | None                                           |

A device satisfies only the concepts it can honour: a read-only device does
not provide writes, and a device that cannot complete without blocking does
not provide awaited operations. A codec or a signature scheme is not a Serio
extension; it lives in its own module and uses Serio to read and write its
bytes.

## Adding a device

A device is a movable type with two `const`, `noexcept` operations: its size,
and a positional read that returns how many bytes it read. It keeps the
[device contract](devices.md#the-contract):

- **No cursor, no lock.** `const` operations run from any number of threads
  at once, so the device keeps no mutable read state.
- **Ends are not errors.** A read at or past the end returns zero bytes; a
  read that crosses the end returns the bytes before it. A device may return
  fewer bytes than asked anywhere, since exact reads loop.
- **Errors keep their cause.** An OS error passes through as it is; a
  condition only Serio knows uses a `serio::Error` code.
- **Only the capabilities it has.** It adds writes, `Reserve` and `Flush` only
  if it can write, `Bytes()` only if it is one span of memory, and
  `AsyncReadAt` only if it completes without blocking.

### Example: two devices read as one

A pak split in two files, read as one device:

```cpp
//! The bytes of `first`, then the bytes of `second`, as one device.
template <ReadableDevice A, ReadableDevice B> class ConcatDevice {
public:
  ConcatDevice(A first, B second) noexcept
    : first_(std::move(first))
    , second_(std::move(second))
  {
  }

  [[nodiscard]] auto Size() const noexcept -> Result<ByteCount>
  {
    const auto first = first_.Size();
    if (!first) {
      return Err(first.error());
    }
    const auto second = second_.Size();
    if (!second) {
      return Err(second.error());
    }
    return Ok(*first + *second);
  }

  //! Reads from the device `at` falls in. A read that crosses from the first
  //! device into the second returns the first one's bytes only.
  [[nodiscard]] auto ReadAt(const ByteOffset at,
    const std::span<std::byte> out) const noexcept -> Result<ByteCount>
  {
    const auto first = first_.Size();
    if (!first) {
      return Err(first.error());
    }
    const auto split = ByteOffset {} + *first;
    if (at < split) {
      const auto room = static_cast<size_t>((split - at).get());
      return first_.ReadAt(at, out.first((std::min)(out.size(), room)));
    }
    return second_.ReadAt(ByteOffset {} + (at - split), out);
  }

private:
  A first_;
  B second_;
};

static_assert(ReadableDevice<ConcatDevice<MemoryFile, ByteView>>);
static_assert(!ContiguousDevice<ConcatDevice<MemoryFile, ByteView>>);
```

The device then joins the conformance suites in Serio's tests: a factory in
`test/Support/DeviceFactories.h` builds it holding the bytes a case gives,
and the device goes into the type list of each suite whose capability it has,
in `test/DeviceConformance_functional_test.cpp`:

```cpp
struct ConcatFactory {
  using Device = ConcatDevice<MemoryFile, MemoryFile>;

  auto Make(const std::span<const std::byte> bytes) -> Device
  {
    const auto half = bytes.size() / 2;
    return Device {
      Required(MemoryFile::FromBytes(bytes.first(half)), "first half"),
      Required(MemoryFile::FromBytes(bytes.subspan(half)), "second half"),
    };
  }
};
```

## Conformance suites

| Suite                    | Holds to one contract                                                     |
| ------------------------ | ------------------------------------------------------------------------- |
| Device conformance       | Every device: bounds, zero-length operations, concurrent reads, errors    |
| Async device conformance | Every awaitable device, once per backend of the platform, inline included |
| Stream conformance       | Every input and output stream                                             |

A suite is a typed GoogleTest suite over a list of factories, part of Serio's
own tests; a new device or stream joins it with one factory and one entry in
each list that fits.

[Testing with Serio](testing.md) covers the test kit and how consumers test
their own code; the [test guide](../test/README.md) covers Serio's own tiers,
executables and where a new test goes.
