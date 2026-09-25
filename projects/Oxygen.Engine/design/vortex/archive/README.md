# Documentation migration reference

The subsequent [content review](content-review.md) records source corrections,
editorial decisions and the consolidated open-work inventory.

The pre-refactor Vortex package is preserved at Git revision
`6b90ed126b440acaca38571c8f45d3dc82fe9848`.

A complete local copy, including untracked evidence and captured source trees,
is stored at `H:/projects/.documentation-backups/vortex-20260925-6b90ed126b44/vortex`.
The adjacent `manifest.json` records SHA-256 hashes and sizes for all 2,135 files
(236,697,071 bytes). Every copied file was verified against its source before
editing began.

Use the Git revision to retrieve tracked originals on another checkout:

```powershell
git show 6b90ed126b440acaca38571c8f45d3dc82fe9848:projects/Oxygen.Engine/design/vortex/IMPLEMENTATION_STATUS.md
```

The [migration map](migration-map.json) records source ranges, destinations,
editorial consolidations and artifact hashes. It covers all 87 original documents:
286 retained sections and 51 sections consolidated into shared rules or navigation.
Historical evidence retains its recorded bytes; milestone READMEs own status.

Verification from the engine directory:

```powershell
python tools/vortex/CheckDocumentation.py --migration
```

The migration check compares retained content with the original Git revision and
verifies all 2,048 moved evidence files, their Git tracking, links and navigation.
The separate backup contains all 2,135 original files. Its hashes were rechecked
after the rewrite. The 104 unavailable local `out/` run artifacts remain identified
as historical references; they were already absent and were not regenerated.
