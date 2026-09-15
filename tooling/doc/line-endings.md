# Line endings

Text uses LF in both Git and working copies on Windows, macOS and Linux.
Windows batch scripts (`.bat` and `.cmd`, case-insensitive) use CRLF in the
working copy and LF in Git. `.gitattributes` is authoritative; `.editorconfig`
uses the same policy for editors. Explicit `eol` attributes override the
machine's `core.autocrlf` default, so no global Git configuration change is needed.

## Automatic enforcement

The root pre-commit configuration uses the standard
[`mixed-line-ending` hook](https://github.com/pre-commit/pre-commit-hooks#mixed-line-ending)
with `--fix=lf` for text and `--fix=crlf` for batch scripts. Binary and LFS
extensions are excluded; keep those exclusions aligned with `.gitattributes`.
The hooks run in the same commit check and repair affected files automatically. If they
repair files, review and stage the repairs, then retry the commit. They do not
stage files for you.

Editors and coding agents should write the required line endings directly.
There is no per-edit script requirement. To repair a known mismatch manually,
run from the repository root:

```shell
pre-commit run mixed-line-ending --files path/to/file
```

Install hooks in each clone using `pre-commit install` from the repository root.
All hooks run from the Git repository root. If a clone previously installed
hooks using a project-specific configuration, run `pre-commit install` again
to switch it to the root configuration.

## Existing checkouts

New clones use the correct endings automatically. For an existing checkout,
pull the policy change, close affected editor documents and preserve current
work before a repository-wide repair:

```shell
pre-commit run mixed-line-ending --all-files
git add --renormalize .
git diff --cached
pre-commit run mixed-line-ending --all-files
```

The first hook run reports failure if it repairs files; the second should pass.
`git add --renormalize .` stages all tracked modifications, so use it only with
an otherwise clean checkout or when intentionally staging those changes.
Review and commit any normalization changes, then reopen editor documents.
Git renormalization alone does not rewrite existing working-copy bytes.
