# Virtual paths

A virtual path identifies an asset: `/<mount>/<segment>/.../<leaf>`, in UTF-8.
`AssetKey::FromVirtualPath` hashes its bytes, so the path is the identity. The
rules below are implemented by [VirtualPath.h](../VirtualPath.h) and, with the
same refusal codes, by the editor's `Oxygen.Managed.Core`.
[VirtualPathCases.json](../VirtualPathCases.json) states every rule with
accepted and refused inputs; both implementations run it in their tests, so what
the cooker accepts, the editor accepts.

## Why these rules

The pak identifies assets by key only; paths appear in string tables (the loose
index and the optional pak browse index) with `uint32` lengths. Nothing in a
format limits the length of a path, the number of segments or their characters.
The limits come from the loose cooked tree, where every segment becomes a file
or folder name: a segment must be a valid file name on every platform that holds
a cooked tree, Windows included.

## Segment

Checks run in this order, each over the whole segment; the first that fails
gives the refusal code.

| Order | Rule                                                                                                                       | Code                                  |
| ----- | -------------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| 1     | Not empty                                                                                                                  | `vpath.segment.empty`                 |
| 1     | Not `.` or `..`                                                                                                            | `vpath.segment.dot`                   |
| 2     | Valid UTF-8                                                                                                                | `vpath.segment.invalid_utf8`          |
| 3     | No control character (U+0000-U+001F, U+007F-U+009F)                                                                        | `vpath.segment.control_character`     |
| 4     | None of `\ / : * ? " < > \|`                                                                                               | `vpath.segment.reserved_character`    |
| 5     | Every code point assigned in Unicode 15.1                                                                                  | `vpath.segment.unassigned_code_point` |
| 6     | In NFC                                                                                                                     | `vpath.segment.not_nfc`               |
| 7     | No leading or trailing space                                                                                               | `vpath.segment.edge_space`            |
| 7     | No trailing dot                                                                                                            | `vpath.segment.trailing_dot`          |
| 8     | Not a Windows device name: `CON`, `PRN`, `AUX`, `NUL`, `COM1`-`COM9`, `LPT1`-`LPT9`, any ASCII case, alone or before a dot | `vpath.segment.device_name`           |
| 9     | At most 255 bytes, the portable file name bound (ext4 and APFS count bytes, NTFS 255 UTF-16 units)                         | `vpath.segment.too_long`              |

Any segment may contain dots: `Level.v2.oscene`, a folder `v1.2` and a container
segment `Player.input` are valid.

## Mount

The first segment names the mount: an identifier, `[A-Za-z_][A-Za-z0-9_-]*`,
optionally preceded by `.` (`vpath.mount.invalid`), at most 255 bytes
(`vpath.segment.too_long`). A mount starting with `.` is reserved by the engine,
as hidden names are by an operating system: `.cooked` is the mount of content
cooked without a project. A project names its mounts without the dot.

## Path

Checks run in this order:

1. not empty (`vpath.empty`), starts with `/` (`vpath.not_absolute`), no
   trailing `/` (`vpath.trailing_slash`), no empty segment
   (`vpath.segment.empty`);
2. the mount rule on the first segment;
3. at least one segment after the mount (`vpath.mount_only`);
4. for each further segment in order: the segment rule, the derived content rule
   for an authored path, and, for the leaf, an extension: a dot after its first
   character (`vpath.leaf.missing_extension`).

The leaf's last extension is the asset type; every other dot belongs to the
name.

## Derived content

A path being authored adds one rule: below the mount, a segment that starts with
`.` (`.cooked`, `.imported`, `.build`, `.pipeline`) or is `SourceMedia` names
derived content and is refused (`vpath.segment.derived_content`). A path the
engine writes or resolves accepts it.

## Case

Identity is case-sensitive. Two paths equal under Unicode 15.1 simple case
folding (`FoldCase`) are case-only siblings: a case-insensitive file system
holds them in one file, so one is refused where it is authored and the cooker
treats the second as a collision.

## Unicode data

Unicode 15.1 is pinned. Its stability policies guarantee that NFC and simple
case folding of assigned code points never change, so implementations on any
later Unicode version agree on every accepted segment. The engine normalizes
with utf8proc 2.9.0 (Unicode 15.1). Every implementation generates its assigned
ranges and simple case folding table from the one database in
[Unicode](../Unicode/README.md); the Content build turns it into
`Internal/Generated.UnicodeTables.h` through
[UnicodeCodeGen](../Tools/UnicodeCodeGen/README.md).
