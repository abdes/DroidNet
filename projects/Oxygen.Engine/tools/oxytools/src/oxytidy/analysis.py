"""Configuration snapshots, compiler dependency discovery, and reusable analysis."""

from __future__ import annotations

import json
import os
import threading
import uuid
from dataclasses import dataclass
from pathlib import Path

import oxytools
from oxytools.common import (
    ToolError,
    absolute,
    digest,
    fingerprint,
    unchanged,
    validate,
    write_json,
    yaml_documents,
)
from oxytools.compilation import Context, expand_responses

from .diagnostics import DIAGNOSTIC_SCHEMA, read_export
from .execution import Runner, checked
from .scope import Scope

SCAN_SCHEMA = {
    "type": "object",
    "required": ["translation-units", "modules"],
    "properties": {
        "modules": {
            "type": "array",
            "items": {
                "type": "object",
                "required": ["file-deps"],
                "properties": {
                    "file-deps": {"type": "array", "items": {"type": "string"}}
                },
            },
        },
        "translation-units": {
            "type": "array",
            "minItems": 1,
            "items": {
                "type": "object",
                "required": ["commands"],
                "properties": {
                    "commands": {
                        "type": "array",
                        "minItems": 1,
                        "items": {
                            "type": "object",
                            "required": ["file-deps"],
                            "properties": {
                                "file-deps": {
                                    "type": "array",
                                    "minItems": 1,
                                    "items": {"type": "string"},
                                }
                            },
                        },
                    },
                },
            },
        },
    },
}


@dataclass
class Prepared:
    context: Context
    folder: Path
    config: dict
    dependencies: list[Path]
    snapshot: dict[str, str]
    cache_key: str


class Analyzer:
    def __init__(
        self,
        runner: Runner,
        tidy: str,
        scanner: str,
        versions: dict,
        run_dir: Path,
        config_file: Path | None,
        checks: str | None,
        scope: Scope,
        cache_dir: Path,
        incremental: bool,
        force: bool,
        quiet: bool,
    ) -> None:
        self.runner = runner
        self.tidy = tidy
        self.scanner = scanner
        self.versions = versions
        self.run_dir = run_dir
        self.config_file = config_file
        self.checks = checks
        self.scope = scope
        self.cache_dir = cache_dir
        self.incremental = incremental
        self.force = force
        self.quiet = quiet
        self._config_lock = threading.Lock()
        self._configs: dict[str, dict] = {}
        self.code_fingerprint = fingerprint(
            [
                *Path(__file__).parent.glob("*.py"),
                *Path(oxytools.__file__).parent.glob("*.py"),
            ]
        )

    def configuration(self, context: Context, folder: Path) -> dict:
        files = (
            [self.config_file]
            if self.config_file
            else [
                directory / ".clang-tidy"
                for directory in [context.file.parent, *context.file.parent.parents]
                if (directory / ".clang-tidy").is_file()
            ]
        )
        key = digest([fingerprint(files), self.checks])
        # Each effective configuration is dumped and verified only once per run.
        with self._config_lock:
            if key in self._configs:
                return dict(self._configs[key])
            command = [self.tidy, str(context.file), "-p", str(folder)]
            if self.config_file:
                command.append(f"--config-file={self.config_file}")
            if self.checks:
                command.append(f"--checks={self.checks}")
            result = self.runner.run(
                [*command, "--dump-config"], context.directory, folder / "dump-config"
            )
            documents = yaml_documents(
                checked(result, "Reading clang-tidy configuration")
            )
            if len(documents) != 1 or not isinstance(documents[0], dict):
                raise ToolError("clang-tidy did not return a configuration mapping")
            config = documents[0]
            validate(
                config,
                {
                    "type": "object",
                    "properties": {
                        "Checks": {"type": "string"},
                        "WarningsAsErrors": {"type": "string"},
                        "ExtraArgs": {"type": "array", "items": {"type": "string"}},
                        "ExtraArgsBefore": {
                            "type": "array",
                            "items": {"type": "string"},
                        },
                    },
                },
                "Effective clang-tidy configuration",
            )
            # A folded YAML scalar retains its final newline. Appending CLI
            # checks puts that newline before a comma in --dump-config output,
            # which --verify-config can interpret as an empty check. Preserve
            # every glob and its order, removing only surrounding whitespace.
            for option in ("Checks", "WarningsAsErrors"):
                if option in config:
                    config[option] = ",".join(
                        glob.strip() for glob in config[option].split(",")
                    )
            config["InheritParentConfig"] = False
            config["HeaderFilterRegex"] = self.scope.header_filter()
            config["SystemHeaders"] = False
            # Persist the exact effective config, including project exclusions.
            effective = folder / "effective.clang-tidy"
            self.runner.reporter.configuration(context.file, files, effective)
            write_json(effective, config)
            checked(
                self.runner.run(
                    [
                        self.tidy,
                        str(context.file),
                        "-p",
                        str(folder),
                        f"--config-file={effective}",
                        "--verify-config",
                    ],
                    context.directory,
                    folder / "verify-config",
                ),
                "Validating clang-tidy configuration",
            )
            self._configs[key] = dict(config)
            return config

    def prepare(self, context: Context, *, verification: bool = False) -> Prepared:
        folder = self.run_dir / "invocations" / context.identity
        if verification:
            folder = folder / "verification"
        folder.mkdir(parents=True, exist_ok=True)
        write_json(folder / "compile_commands.json", [context.entry()])
        config = self.configuration(context, folder)
        response_inputs = dict(context.response_inputs)
        for option in ("ExtraArgsBefore", "ExtraArgs"):
            config[option] = expand_responses(
                config.get(option, []), context.directory, inputs=response_inputs
            )
        write_json(folder / "effective.clang-tidy", config)
        scan_db = folder / "scan_commands.json"
        write_json(
            scan_db,
            [context.entry(config.get("ExtraArgsBefore"), config.get("ExtraArgs"))],
        )
        result = self.runner.run(
            [
                self.scanner,
                f"-compilation-database={scan_db}",
                "-format=experimental-full",
                "-j",
                "1",
            ],
            context.directory,
            folder / "dependencies",
        )
        data = json.loads(checked(result, f"Dependency scan for {context.file}"))
        validate(data, SCAN_SCHEMA, "Dependency scanner output")
        names = [str(context.file)]
        for unit in data["translation-units"]:
            for command in unit["commands"]:
                names.extend(command["file-deps"])
        for module in data["modules"]:
            names.extend(module["file-deps"])
        config["HeaderFilterRegex"] = self.scope.dependency_filter(
            names, context.directory
        )
        write_json(folder / "effective.clang-tidy", config)
        dependencies = sorted({absolute(name, context.directory) for name in names})
        snapshot = fingerprint(dependencies)
        snapshot.update(response_inputs)
        cache_key = digest(
            {
                "schema": 1,
                "implementation": self.code_fingerprint,
                "context": context.entry(),
                "config": config,
                "versions": self.versions,
                "dependencies": snapshot,
                "environment": {
                    key: os.environ.get(key)
                    for key in (
                        "INCLUDE",
                        "CPATH",
                        "CPLUS_INCLUDE_PATH",
                        "C_INCLUDE_PATH",
                        "SDKROOT",
                        "PATH",
                    )
                },
            }
        )
        prepared = Prepared(context, folder, config, dependencies, snapshot, cache_key)
        write_json(
            folder / "inputs.json", {"snapshot": snapshot, "cache_key": cache_key}
        )
        return prepared

    def analyze(self, prepared: Prepared, *, verification: bool = False) -> dict:
        context = prepared.context
        folder = prepared.folder
        folder.mkdir(parents=True, exist_ok=True)
        cache_path = self.cache_dir / f"{context.identity}.json"
        if (
            self.incremental
            and not self.force
            and not verification
            and cache_path.is_file()
        ):
            try:
                cached = json.loads(cache_path.read_text(encoding="utf-8"))
                validate(
                    cached,
                    {
                        "type": "object",
                        "required": ["key", "result"],
                        "properties": {
                            "key": {"type": "string"},
                            "result": {"type": "object"},
                        },
                    },
                    "Cached analysis envelope",
                )
                if cached["key"] == prepared.cache_key and unchanged(prepared.snapshot):
                    result = cached["result"]
                    validate(
                        result,
                        {
                            "type": "object",
                            "required": [
                                "id",
                                "file",
                                "folder",
                                "status",
                                "diagnostics",
                                "returncode",
                                "policy_failure",
                            ],
                            "properties": {
                                "id": {"const": context.identity},
                                "file": {"const": str(context.file)},
                                "folder": {"type": "string"},
                                "status": {"const": "completed"},
                                "returncode": {"enum": [0, 1]},
                                "policy_failure": {"type": "boolean"},
                                "diagnostics": {
                                    "type": "array",
                                    "items": DIAGNOSTIC_SCHEMA,
                                },
                            },
                        },
                        "Cached analysis",
                    )
                    result = {
                        **result,
                        "reused": True,
                        "duration": 0,
                        "folder": str(folder),
                        "cached_from": result["folder"],
                    }
                    write_json(folder / "result.json", result)
                    return result
            except (OSError, ValueError, KeyError, ToolError) as error:
                self.runner.reporter.rows(
                    [("Cache ignored", cache_path), ("Reason", str(error))]
                )
        if not unchanged(prepared.snapshot):
            raise ToolError(
                f"Inputs changed after dependency discovery: {context.file}"
            )
        exported = folder / "diagnostics.yaml"
        command = [
            self.tidy,
            str(context.file),
            "-p",
            str(prepared.folder),
            f"--config-file={prepared.folder / 'effective.clang-tidy'}",
            "--use-color=false",
            f"--export-fixes={exported}",
        ]
        if self.quiet:
            command.append("--quiet")
        process = self.runner.run(command, context.directory, folder / "analysis")
        result = {
            "id": context.identity,
            "file": str(context.file),
            "status": process.status,
            "returncode": process.returncode,
            "duration": process.duration,
            "reused": False,
            "folder": str(folder),
            "command": command,
            "diagnostics": read_export(exported, context.directory, context.identity),
        }
        errors = [
            record for record in result["diagnostics"] if record["level"] == "error"
        ]
        # WarningsAsErrors is a findings policy, not a failed compilation. Keep
        # its raw exit status and honor the configured policy after scoping.
        policy_failure = (
            process.returncode == 1
            and bool(prepared.config.get("WarningsAsErrors"))
            and bool(errors)
            and all(record["check"] != "clang-diagnostic-error" for record in errors)
        )
        result["policy_failure"] = policy_failure
        if policy_failure:
            result["status"] = "completed"
        if not unchanged(prepared.snapshot):
            result.update(
                status="failed", returncode=2, error="Inputs changed during analysis"
            )
        write_json(folder / "result.json", result)
        # Verification always executes freshly, but its post-fix input key and
        # diagnostics are valid for the next unchanged incremental invocation.
        if self.incremental and result["status"] == "completed":
            self.cache_dir.mkdir(parents=True, exist_ok=True)
            temporary = cache_path.with_suffix(f".{uuid.uuid4().hex}.tmp")
            try:
                write_json(temporary, {"key": prepared.cache_key, "result": result})
                os.replace(temporary, cache_path)
            finally:
                temporary.unlink(missing_ok=True)
        return result

    def invalidate(self, contexts: list[Prepared]) -> None:
        for context in contexts:
            (self.cache_dir / f"{context.context.identity}.json").unlink(
                missing_ok=True
            )
