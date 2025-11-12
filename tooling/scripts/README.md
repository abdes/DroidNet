# Tooling scripts

This folder contains Python helper scripts used by the DroidNet repo.

How the package works

- Each script is a Python module in `tooling/scripts/`. You can run a module directly using:

```pwsh
python -m tooling.scripts.<module>
```

For example (long flags):

```pwsh
python -m tooling.scripts.get_artifacts --project projects/Mvvm.Generators/src/Mvvm.Generators.csproj --configuration Debug --json
```

Short flags are also supported (recommended for CLI workflow):

```pwsh
python -m tooling.scripts.get_artifacts -p Mvvm.Generators/src/Mvvm.Generators.csproj -c Debug -l -n
```

Notes on path heuristics:

- If you pass a relative path (not starting with `projects`), it is interpreted relative to `repo-root/projects/`. For example `-p Mvvm.Generators/src/Mvvm.Generators.csproj` will be resolved to `repo-root/projects/Mvvm.Generators/src/Mvvm.Generators.csproj`.
- If you pass `-p projects/Whatever/...` it is interpreted as `repo-root/projects/Whatever/...`.
- If you pass an absolute path it is used as-is.
- Use `-p .` to point to the current working directory (legacy behavior).

- To add a new script, add a new file `tooling/scripts/<name>.py` and implement a `run()` function and a `__main__` block that calls `run()` so it can be executed both ways.

Example template

```python
# tooling/scripts/hello.py
import argparse
import sys


def run():
    parser = argparse.ArgumentParser(prog="hello")
    parser.add_argument("--name", default="world")
    args = parser.parse_args()
    print(f"Hello, {args.name}!")
    return 0


if __name__ == "__main__":
    sys.exit(run())
```

How to make scripts available on PATH

- If you'd like to run scripts as `get-project-artifacts` without `python -m`, you can install the package in editable mode and specify a console script entry point in `pyproject.toml`:

```toml
[project]
name = "droidnet-tooling-scripts"
...

[project.scripts]
get-artifacts = "get_artifacts:run"
hello-tooling = "hello:run"
```

Then install the package in your environment (developer mode is convenient):

```pwsh
cd tooling\scripts
python -m pip install -e .
```

- That creates console entry points in your virtualenv or Python install directory so you can run the script directly.
This creates two commands in your PATH when installed in editable mode:

```pwsh
get-artifacts --project projects\Mvvm.Generators\src\Mvvm.Generators.csproj --configuration Debug --list
hello-tooling --name Test
```

Dependencies

- `tooling/scripts/pyproject.toml` lists `rich` as an optional dependency. Run `pip install -e tooling/scripts` to install in editable mode and get `rich` installed.

Notes

- Use `python -m tooling.scripts.<module>` for cross-platform and idempotent invocations.
- Ensure each script calls `run()` in `if __name__ == '__main__'` to maintain consistent CLI behavior.
- Keep the `tooling/scripts` package small and focused to avoid install-time dependency issues.
