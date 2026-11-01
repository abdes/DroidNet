# IO boundary

Which file IO goes through Serio, and the limits every whole-file read names.

## The rule

A file a program parses, and a file it publishes for another reader, goes
through Serio. A whole-file read names a limit for its family of files, and a
larger file is refused with `kTooLarge` before anything is allocated, so a
corrupt or hostile file never sizes an allocation.

## Limits

| Files                                                  | Limit                                    |
| ------------------------------------------------------ | ---------------------------------------- |
| Import sources                                         | `kDefaultMaxSourceBytes`, set per import |
| Manifests, mounts files, tool requests, import records | `kMaxImportMetadataBytes`                |
| Cooked indexes, descriptors, resource tables, catalogs | `cooked::kMaxMetadataBytes`              |
| Cooked resource data files                             | `cooked::kMaxDataFileBytes`              |
| Scripts                                                | `kMaxScriptBytes`                        |
| Console archive and history                            | `kMaxPersistedFileBytes`                 |
| Player launch files                                    | `kMaxLaunchFileBytes`                    |
| ShaderBake sources, module artifacts, manifest, state  | `shader_bake::kMaxReadBytes`             |

A new family of files gets its own named limit beside the code that owns the
format, never an inline number at the read.

## What stays outside

Iostreams and C file IO remain only where Serio adds nothing:

- **Text written for people:** reports, traces, crash dumps and benchmark
  output. A torn file is rewritten whole on the next run.
- **Console scripts,** read line by line as a person wrote them.
- **The logger's own files.**
- **Tests and benchmarks,** which write and check their own fixtures.
