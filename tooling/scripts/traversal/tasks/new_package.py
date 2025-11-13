from __future__ import annotations

import os
import shlex
import subprocess
from pathlib import Path

from ..task_registry import TraversalContext, task


@task(
    "New-Package",
    aliases=["new-package", "package"],
    description="Create an AppX package using dotnet msbuild for projects with Package.appxmanifest.",
)
def new_package(project: Path, context: TraversalContext) -> None:
    manifest = project.parent / "Package.appxmanifest"
    if not manifest.exists():
        context.logger.debug("Skipping project without Package.appxmanifest: %s", project)
        return

    params = context.forwarded_arguments

    certificate_value = params.get("packagecertificatekeyfile")
    if not certificate_value or certificate_value is True:
        raise ValueError("New-Package requires --PackageCertificateKeyFile <path> to be provided")

    certificate_path = Path(str(certificate_value)).expanduser()
    if not certificate_path.is_absolute():
        raise ValueError("Certificate file path must be absolute")
    if not certificate_path.exists():
        raise FileNotFoundError(f"Certificate file not found: {certificate_path}")

    configuration = str(params.get("configuration", "Debug"))
    platform = str(params.get("platform", "x64"))
    verbosity = str(params.get("verbosity", "minimal"))

    package_dir_value = params.get("packagelocation")
    if package_dir_value and package_dir_value is not True:
        package_dir = Path(str(package_dir_value)).expanduser()
    else:
        package_dir = project.parent / "Packages"

    package_dir_with_sep = str(package_dir.resolve())
    if not package_dir_with_sep.endswith(os.sep):
        package_dir_with_sep += os.sep

    if not context.dry_run:
        package_dir.mkdir(parents=True, exist_ok=True)

    properties = {
        "Configuration": configuration,
        "Platform": platform,
        "UapAppxPackageBuildMode": "SideloadOnly",
        "AppxBundle": "Never",
        "GenerateAppxPackageOnBuild": "true",
        "BuildProjectReferences": "false",
        "PackageCertificateKeyFile": str(certificate_path),
        "AppxPackageDir": package_dir_with_sep,
    }

    cmd = ["dotnet", "msbuild", str(project), "-nologo", f"-verbosity:{verbosity}"]
    for key, value in properties.items():
        cmd.append(f"-property:{key}={value}")

    command_preview = " ".join(shlex.quote(part) for part in cmd)
    if context.console:
        context.console.print(f"[bold magenta]New-Package[/]: {command_preview}")
    else:
        print(f"New-Package: {command_preview}")

    if context.dry_run:
        return

    result = subprocess.run(cmd, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"dotnet msbuild failed with exit code {result.returncode} for {project}")
