# Artifact locations

All project outputs use the repository `artifacts/` root:

| Content                | Location                                                           |
| ---------------------- | ------------------------------------------------------------------ |
| Binaries               | `artifacts/bin/<project>/<configuration>_<framework>[_<RID>]/`     |
| Intermediates          | `artifacts/obj/<project>/<configuration>_<framework>[_<RID>]/`     |
| NuGet packages         | `artifacts/package/<configuration>/`                               |
| Published applications | `artifacts/publish/<project>/<configuration>_<framework>[_<RID>]/` |

Framework and RID segments are present when specified. NuGet restore metadata
lives at the project's intermediate root, outside the configuration subdirectory.
The installed native engine SDK remains under `projects/Oxygen.Engine/out/install`.

Query actual evaluated paths instead of reproducing the naming logic:

```powershell
get-artifacts -p Oxygen.Editor -c Release -j
get-artifacts -p Collections --framework-all -j
get-artifacts -p Storage -l
get-artifacts --help
```

The helper uses Visual Studio MSBuild property evaluation without building or
restoring. Failed evaluation fails the command; it never substitutes guessed
paths. `-p` accepts absolute paths, repository-relative paths, or paths relative
to `projects/`. `.` means the current directory.

`ArtifactsPath` can be overridden for isolated verification. Managed and native
Interop outputs follow the same root. Each framework/RID/configuration has its
own output directory. See [build workflows](doc/build.md).
