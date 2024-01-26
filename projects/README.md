# Projects

## Naming

Project names are important. They must follow certain conventions, which are
used to deduce things such as the `AssemblyName`, and to identify test projects
from non-test projects.

- Do not use spaces in project names.
- Test projects must have a name that ends with `.Tests`.
- UI Tests, which require a visual interface must have a name that ends with
  `.UI.Tests`.

## Layout

Inside a project folder, sources must be under `src`, tests under `tests`, and
sample code under `samples`.

## Running tests

```shell
dotnet test
```

Run and produce coverage report using `lcov` format, exploitable by
`ReportGenerator` or VsCode `Coverage Gutters`:

```shell
dotnet test /p:CollectCoverage=true /p:CoverletOutputFormat=lcov -p:CoverletOutput=./coverage.lcov.info
```
