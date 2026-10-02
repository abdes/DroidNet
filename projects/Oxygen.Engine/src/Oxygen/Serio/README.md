# Serio file operations

Read: [file locks](#file-locks), [atomic replacement](#atomic-replacement). Stream and binary serialization interfaces are
defined by `FileStream`, `MemoryStream`, `Reader` and `Writer`.

## File locks

`FileLock::TryAcquire(path, mode, open_mode)` owns an operating-system lock until
destruction or move replacement. Shared locks coexist; an exclusive lock excludes
both shared and exclusive owners. Acquisition never waits. Contention returns
`std::errc::device_or_resource_busy`; missing files and other I/O failures retain
their error codes. Public interfaces use Oxygen `Result` and C++20 types.

Windows uses `LockFileEx`; POSIX uses `flock`. These are cooperative lifetime
locks: every reader and publisher must participate. The lock file is an ordinary
marker, separate from replaceable content. Replacing or recreating the marker
while it has owners breaks its identity and is forbidden by the caller protocol.
Handles permit deletion so an exclusive reclamation owner can remove a private
generation while retaining its lock through cleanup.

Content's [immutable generation lifetime](../Content/Docs/loose_cooked_content.md#published-generations)
uses shared writer/reader leases while a private generation is active and
exclusive reclamation leases on a separate marker. Content and Cooker own selection, publication and cleanup policy.

## Atomic replacement

`WriteFileAtomically(path, bytes)` writes and flushes a unique sibling file,
then replaces the destination atomically. The parent directory must exist;
existing access permissions are preserved. Callers own locking and revision
checks. An error result means replacement failed. A successful `AtomicFileCommit`
may carry `durability_error` when the replacement committed but the subsequent
POSIX directory synchronization failed; callers must acknowledge that commit.
The operation does not create a backup or a second authoritative record.
