"""Run already-built tests using the runner selected by the project."""
from __future__ import annotations
import subprocess
from pathlib import Path
from ...msbuild import query_msbuild_properties, visual_studio_tool
from ..task_registry import TraversalContext, task
from ..tfm import discover_target_frameworks

@task("Invoke-Tests", aliases=["invoke-tests", "tests"],
      description="Run built managed, WinUI and native tests; forward runner arguments after --.")
def invoke_tests(project: Path, context: TraversalContext) -> None:
    if not project.stem.endswith((".Tests", ".NativeTests")):
        return
    options = context.forwarded_arguments
    unknown = set(options) - {"configuration", "framework", "timeout", "c", "f"}
    if unknown:
        raise ValueError(f"Unknown test options: {', '.join(sorted(unknown))}; runner arguments go after --")
    configuration = str(options.get("configuration", options.get("c", "Debug")))
    selected = options.get("framework", options.get("f"))
    frameworks = [str(selected)] if selected else discover_target_frameworks(project, configuration=configuration)
    timeout = float(str(options.get("timeout", 300)))
    if timeout <= 0:
        raise ValueError("--timeout must be greater than zero")
    for framework in frameworks:
        values, _ = query_msbuild_properties(
            project, ["IsTestProject", "TargetPath", "TargetDir", "TargetName", "EnableMSTestRunner",
                      "RunSettingsFilePath", "UseWinUI", "WindowsPackageType", "MSBuildProjectName"], configuration=configuration, target_framework=framework,
        )
        if values["IsTestProject"].lower() != "true":
            continue
        if values["EnableMSTestRunner"].lower() == "true":
            executable = Path(values["TargetDir"]) / (values["TargetName"] + ".exe")
            args = [str(executable), *context.extra_arguments]
        else:
            executable = Path(values["TargetPath"])
            if values.get("UseWinUI", "").lower() == "true":
                if values.get("WindowsPackageType", "").lower() == "none":
                    raise RuntimeError("Unpackaged WinUI tests require an MTP test host; build the packaged test configuration for VSTest.")
                executable = Path(values["TargetDir"]) / (values["MSBuildProjectName"] + ".build.appxrecipe")
            args = [str(visual_studio_tool("vstest")), str(executable), "/Platform:x64"]
            if values["RunSettingsFilePath"]:
                args.append("/Settings:" + values["RunSettingsFilePath"])
            args.extend(context.extra_arguments)
        context.logger.info("Invoke-Tests: %s", subprocess.list2cmdline(args))
        if context.dry_run:
            continue
        if not executable.is_file():
            raise FileNotFoundError(f"Build {project.name} ({configuration}, {framework}) before running tests: {executable}")
        # The traversal runner collects exceptions and returns failure.
        with subprocess.Popen(args, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP) as process:
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                # Tests can own native workers or UI processes. Stop the entire owned tree.
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
                process.wait()
                raise
            if code:
                raise subprocess.CalledProcessError(code, args)
