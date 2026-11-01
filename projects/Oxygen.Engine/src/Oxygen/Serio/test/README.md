# Serio tests

How the Serio tests are organized, how to run them, and where a new test
goes.

## Run

| What                     | Command                                                           |
| ------------------------ | ----------------------------------------------------------------- |
| Fast loop (unit)         | `ctest --preset oxygen-ninja-debug -R "^Oxygen\.Serio\." -L unit` |
| Before a commit          | `ctest --preset oxygen-ninja-debug -R "^Oxygen\.Serio\."`         |
| One executable, filtered | `Oxygen.Serio.Functional.Tests --gtest_filter=MappedFile*`        |

Build `Oxygen.Serio.AllTests` first; it builds every Serio test program.

## Tiers

Each executable holds tests of one tier. The tier is a CTest label.

- **unit**: memory only, one thread, no files. Devices are `MemoryFile`,
  `ByteView` and `FaultInjectingDevice` over them.
- **functional**: anything that opens files, maps them, starts threads or
  starts a process. Every case that runs on a `NativeFile` or a `MappedFile`
  is functional, and so is the device conformance suite, which reads
  concurrently.

When in doubt, choose functional.

## Executables

| Executable (`Oxygen.Serio.` …) | Tier       | Sources                                      |
| ------------------------------ | ---------- | -------------------------------------------- |
| `Unit.Tests`                   | unit       | `*_test.cpp`                                 |
| `Functional.Tests`             | functional | `*_functional_test.cpp`                      |
| `FileLock.CrossProcess`        | functional | `file_lock_process_test.py`, `FileLockProbe` |
| `LinkTest`                     | link       | `Link_test.cpp`                              |

## Where is the test for X?

`Serio/<Stem>.h` is tested in `test/<Stem>_test.cpp` (unit) and
`test/<Stem>_functional_test.cpp` (functional). A header with no behavior of
its own beyond a concept, such as `Device.h` and `Stream.h`, is covered by the
conformance suites. Every test file names the production files it covers on a
`// Covers:` line after the license block:

```sh
rg "Covers:.*<Stem>" src/Oxygen/Serio/test
```

## Shared test code

Test code is held to the same standard as the library: one owner per
mechanism, no ad-hoc setup in test bodies, no copies between files.

| Where                                       | What                                                                    | Used by             |
| ------------------------------------------- | ----------------------------------------------------------------------- | ------------------- |
| `Serio/Testing/FaultInjectingDevice.h`      | A device over a device that fails scheduled operations and counts calls | Any module's tests  |
| `Serio/Testing/TestBytes.h`                 | Deterministic test bytes and a byte-range matcher                       | Any module's tests  |
| `test/Conformance/DeviceConformance.h`      | Typed suite every device passes                                         | Serio tests         |
| `test/Conformance/AsyncDeviceConformance.h` | Typed suite every awaitable device passes                               | Serio tests         |
| `test/Conformance/StreamConformance.h`      | Typed suites every input and output stream passes                       | Serio tests         |
| `test/Support/DeviceFactories.h`            | Conformance factories for Serio's own devices                           | Serio tests         |
| `test/Support/StreamFactories.h`            | Conformance factories for Serio's own streams                           | Serio tests         |
| `Oxygen/Testing/ScopedTempDir.h`            | One temporary directory per test, removed afterwards                    | Every engine module |

`Serio/Testing/` is the `oxygen::serio::testing` interface target: the kit
the tests of any module that reads or writes through Serio link, holding no
test cases. `test/Conformance/` holds the contract suites: each header
defines a typed fixture, its cases and their registration, and the
`*Conformance*_test.cpp` files instantiate them over concrete types, in the
unit or the functional tier. `test/Support/` holds the factories and setup
helpers those instantiations use. Both are private to Serio's tests.

## Conformance before specifics

A behavior every device or stream shares is a conformance case, never a
per-type test: the suite runs it on every type, so memory and files cannot
drift apart. Per-type files test only what the type adds (`MemoryFile::Take`,
`MappedFile` replacement under a view, `NativeFile` open errors, stream
buffering).

## Faults

Error paths run through `FaultInjectingDevice`, not mocks: a test schedules a
failed read, a short read or a full disk, then checks the code that comes
back. Codes are asserted against `serio::Error` conditions, so the same
assertion holds for a memory and a file device.

## Adding a test

1. Put it in the file the mapping rule gives; create the file with its
   `// Covers:` line if it does not exist, and add it to the tier's source list
   in `test/CMakeLists.txt`.
2. If the behavior holds for every device or stream, add a conformance case
   instead, and register it in the suite's `REGISTER_TYPED_TEST_SUITE_P`.
3. A new Serio device or stream joins the conformance suites with a factory in
   `test/Support/` and one entry in the factory list.
