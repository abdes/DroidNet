# Filesystem paths

Use [the Base API](../Filesystem.h) at filesystem boundaries and retain ordinary
paths in asset identities, authored records and diagnostics.

`ToNativePath` leaves POSIX and empty paths unchanged. On Windows it resolves an
ordinary path to an absolute, lexically normalized path and uses the extended
drive or UNC namespace. Already extended/device paths retain their namespace.
The conversion does not depend on process manifests or machine long-path policy.
Individual filesystem component limits still apply.

Pass the result to file opens, metadata queries and directory operations.
`ToLogicalPath` removes extended drive/UNC prefixes from returned paths before
persisting them or comparing them with logical paths. It preserves device paths
that have no ordinary drive/UNC spelling.

Serio streams, atomic writes, locks, hashing and native importer I/O share this
conversion. Keep conversion out of rendering and identifier construction.
These functions may allocate and throw filesystem errors when resolving a path.

`PathIdentityKey` supplies absolute, lexically normalized UTF-8 keys for in-memory
path maps. It folds Unicode case with Windows invariant mapping on Windows and
preserves POSIX case. It does no metadata I/O or symlink resolution; callers that
need alias resolution canonicalize once before indexing. Use it for ordinary
Windows application directories, not opted-in case-sensitive trees. Preserve
the original path for file operations and diagnostics.
