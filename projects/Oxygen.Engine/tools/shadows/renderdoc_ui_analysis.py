"""Shared RenderDoc UI analysis helpers for shadow tooling.

This module is source-owned in `tools/shadows/` and is intended to work inside
RenderDoc's embedded Python interpreter. Keep the implementation conservative
and compatible with older embedded Python versions.
"""

import builtins
import os
import sys
import traceback
from pathlib import Path


REPORT_PATH_ENV = "OXYGEN_RENDERDOC_REPORT_PATH"
CAPTURE_PATH_ENV = "OXYGEN_RENDERDOC_CAPTURE_PATH"
PASS_NAME_ENV = "OXYGEN_RENDERDOC_PASS_NAME"
PASS_EVENT_LIMIT_ENV = "OXYGEN_RENDERDOC_PASS_EVENT_LIMIT"
EVENT_ID_ENV = "OXYGEN_RENDERDOC_EVENT_ID"
RESOURCE_NAME_ENV = "OXYGEN_RENDERDOC_RESOURCE_NAME"
RESOURCE_EVENT_LIMIT_ENV = "OXYGEN_RENDERDOC_RESOURCE_EVENT_LIMIT"

DEFAULT_PASS_EVENT_LIMIT = 24
DEFAULT_RESOURCE_EVENT_LIMIT = 24

KNOWN_PASS_NAMES = [
    "Vortex.Stage8.ShadowDepths",
    "Vortex.Stage9.BasePass.MainPass",
    "Vortex.Stage9.BasePass",
    "ConventionalShadowRasterPass.ShadowDepthWork",
    "DepthPrePass.SceneDepthWork",
    "AutoExposurePass",
    "ToneMapPass",
    "ConventionalShadowRasterPass",
    "ConventionalShadowReceiverAnalysisPass",
    "ConventionalShadowReceiverMaskPass",
    "DepthPrePass",
    "ScreenHzbBuildPass",
    "LightCullingPass",
    "VsmPageRequestGeneratorPass",
    "VsmInvalidationPass",
    "VsmPageManagementPass",
    "VsmPageFlagPropagationPass",
    "VsmPageInitializationPass",
    "VsmShadowRasterizerPass",
    "VsmStaticDynamicMergePass",
    "VsmHzbUpdaterPass",
    "VsmProjectionPass",
    "ShaderPass",
    "TransparentPass",
]

_RENDERDOC_MODULE = None


class ResourceRecord(object):
    def __init__(self, name, width, height, mips, arraysize, length, resource_type):
        self.name = name
        self.width = width
        self.height = height
        self.mips = mips
        self.arraysize = arraysize
        self.length = length
        self.resource_type = resource_type


class ActionRecord(object):
    def __init__(self, event_id, name, path, flags, pass_name):
        self.event_id = event_id
        self.name = name
        self.path = path
        self.flags = flags
        self.pass_name = pass_name


class ActionResources(object):
    def __init__(
        self,
        outputs,
        depth,
        pixel_ro,
        compute_ro,
        pixel_rw,
        compute_rw,
        raster_viewports,
        raster_scissors,
    ):
        self.outputs = outputs
        self.depth = depth
        self.pixel_ro = pixel_ro
        self.compute_ro = compute_ro
        self.pixel_rw = pixel_rw
        self.compute_rw = compute_rw
        self.raster_viewports = raster_viewports
        self.raster_scissors = raster_scissors

    def all_lines(self):
        lines = list(self.raster_viewports)
        lines.extend(self.raster_scissors)
        lines.extend(self.outputs)
        if self.depth is not None:
            lines.append(self.depth)
        lines.extend(self.pixel_ro)
        lines.extend(self.compute_ro)
        lines.extend(self.pixel_rw)
        lines.extend(self.compute_rw)
        return lines

    def writable_lines(self):
        lines = list(self.raster_viewports)
        lines.extend(self.raster_scissors)
        lines.extend(self.outputs)
        lines.extend(self.pixel_rw)
        lines.extend(self.compute_rw)
        return lines


class ReportWriter(object):
    def __init__(self, report_path):
        self.report_path = report_path
        self.lines = []

    def append(self, line):
        self.lines.append(line)

    def blank(self):
        self.lines.append("")

    def extend(self, lines):
        self.lines.extend(lines)

    def flush(self):
        if not self.lines or self.lines[0] != "analysis_result=success":
            if self.lines and self.lines[0].startswith("analysis_result="):
                self.lines[0] = "analysis_result=success"
            else:
                self.lines.insert(0, "analysis_result=success")
        write_report(self.report_path, self.lines)


def get_capture_context():
    main_module = sys.modules.get("__main__")
    if main_module is not None and hasattr(main_module, "pyrenderdoc"):
        return getattr(main_module, "pyrenderdoc")
    return getattr(builtins, "pyrenderdoc", None)


def ensure_ui_context():
    capture_context = get_capture_context()
    if capture_context is None:
        raise RuntimeError(
            "This script must run from RenderDoc UI via qrenderdoc.exe --ui-python <script.py> <capture.rdc>."
        )
    if not hasattr(capture_context, "Replay"):
        raise RuntimeError("RenderDoc UI replay context is unavailable")
    return capture_context


def renderdoc_module():
    global _RENDERDOC_MODULE

    ensure_ui_context()
    if _RENDERDOC_MODULE is not None:
        return _RENDERDOC_MODULE

    existing = sys.modules.get("renderdoc", sys.modules.get("_renderdoc"))
    if existing is not None:
        _RENDERDOC_MODULE = existing
        return _RENDERDOC_MODULE

    import renderdoc as bundled_renderdoc

    _RENDERDOC_MODULE = bundled_renderdoc
    return _RENDERDOC_MODULE


def safe_getattr(value, attribute, default=None):
    try:
        return getattr(value, attribute)
    except Exception:
        return default


def parse_optional_int_env(name):
    configured = os.environ.get(name, "").strip()
    if not configured:
        return None
    try:
        return int(configured)
    except ValueError:
        return None


def parse_int_env(name, default_value):
    configured = os.environ.get(name, "").strip()
    if not configured:
        return default_value
    try:
        parsed = int(configured)
        if parsed > 0:
            return parsed
        return default_value
    except ValueError:
        return default_value


def get_required_env(name, description):
    configured = os.environ.get(name, "").strip()
    if configured:
        return configured
    raise RuntimeError("Set {} to {}".format(name, description))


def get_process_args():
    argv = getattr(sys, "argv", None)
    if argv is None:
        argv = getattr(sys, "orig_argv", None)
    if argv is None:
        return []
    try:
        return list(argv)
    except TypeError:
        return []


def normalize_path(path_value):
    if path_value is None:
        return None
    return Path(path_value).resolve()


def paths_equal(lhs, rhs):
    if lhs is None or rhs is None:
        return False
    return str(normalize_path(lhs)).lower() == str(normalize_path(rhs)).lower()


def current_ui_capture_path():
    capture_context = get_capture_context()
    if capture_context is None:
        return None

    get_capture_filename = getattr(capture_context, "GetCaptureFilename", None)
    if not callable(get_capture_filename):
        return None

    try:
        capture_filename = get_capture_filename()
    except Exception:
        return None

    if not capture_filename:
        return None

    return normalize_path(capture_filename)


def resolve_capture_path():
    configured = os.environ.get(CAPTURE_PATH_ENV, "").strip()
    if configured:
        return normalize_path(configured)

    for arg in get_process_args()[1:]:
        if arg.lower().endswith(".rdc"):
            return normalize_path(arg)

    return current_ui_capture_path()


def resolve_report_path(capture_path, report_suffix):
    configured = os.environ.get(REPORT_PATH_ENV, "").strip()
    if configured:
        return Path(configured).resolve()

    report_base = capture_path
    if report_base is None:
        report_base = Path.cwd() / "ui_loaded_capture.rdc"
    return report_base.with_name(report_base.stem + report_suffix)


def write_report(report_path, report_lines):
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(report_lines) + "\n", encoding="utf-8")


def close_ui_cleanly():
    capture_context = get_capture_context()
    if capture_context is None:
        return

    try:
        if hasattr(capture_context, "IsCaptureLoaded") and capture_context.IsCaptureLoaded():
            capture_context.CloseCapture()
    except Exception:
        pass

    try:
        main_window = capture_context.GetMainWindow()
        if hasattr(main_window, "Widget"):
            widget = main_window.Widget()
        else:
            widget = main_window
        close_method = getattr(widget, "close", None)
        if callable(close_method):
            close_method()
    except Exception:
        pass


def validate_loaded_capture(requested_capture_path):
    ensure_ui_context()
    current_capture = current_ui_capture_path()
    if current_capture is None:
        raise RuntimeError(
            "RenderDoc UI does not have a capture loaded. Launch qrenderdoc with the .rdc file or load a capture before running the script."
        )
    if requested_capture_path is not None and not paths_equal(
        current_capture, requested_capture_path
    ):
        raise RuntimeError(
            "Loaded capture does not match requested capture. loaded={} requested={}".format(
                current_capture, requested_capture_path
            )
        )
    return current_capture


def is_work_action(flags):
    rd = renderdoc_module()
    return bool(
        (flags & rd.ActionFlags.Drawcall)
        or (flags & rd.ActionFlags.Dispatch)
        or (flags & rd.ActionFlags.MeshDispatch)
        or (flags & rd.ActionFlags.Copy)
        or (flags & rd.ActionFlags.Resolve)
        or (flags & rd.ActionFlags.Clear)
    )


def format_flags(flags):
    rd = renderdoc_module()
    mapping = (
        ("Clear", rd.ActionFlags.Clear),
        ("Draw", rd.ActionFlags.Drawcall),
        ("Dispatch", rd.ActionFlags.Dispatch),
        ("MeshDispatch", rd.ActionFlags.MeshDispatch),
        ("CmdList", rd.ActionFlags.CmdList),
        ("SetMarker", rd.ActionFlags.SetMarker),
        ("PushMarker", rd.ActionFlags.PushMarker),
        ("PopMarker", rd.ActionFlags.PopMarker),
        ("Present", rd.ActionFlags.Present),
        ("Copy", rd.ActionFlags.Copy),
        ("Resolve", rd.ActionFlags.Resolve),
        ("GenMips", rd.ActionFlags.GenMips),
        ("BeginPass", rd.ActionFlags.BeginPass),
        ("EndPass", rd.ActionFlags.EndPass),
    )
    active = [name for name, bit in mapping if flags & bit]
    if active:
        return "|".join(active)
    return "NoFlags"


def matching_pass_name(path_parts):
    for path_part in reversed(path_parts):
        exact_matches = [pass_name for pass_name in KNOWN_PASS_NAMES if pass_name == path_part]
        if exact_matches:
            return max(exact_matches, key=len)

        substring_matches = [
            pass_name for pass_name in KNOWN_PASS_NAMES if pass_name in path_part
        ]
        if substring_matches:
            return max(substring_matches, key=len)
    return None


def collect_action_records(controller):
    structured = controller.GetStructuredFile()
    records = []

    def walk(action, parent_path):
        name = action.GetName(structured)
        current_path = list(parent_path) + [name]
        records.append(
            ActionRecord(
                event_id=action.eventId,
                name=name,
                path=" > ".join(current_path),
                flags=action.flags,
                pass_name=matching_pass_name(current_path),
            )
        )
        for child in action.children:
            walk(child, current_path)

    for root in controller.GetRootActions():
        walk(root, [])

    return records


def find_scope_records(action_records, pass_name):
    return [record for record in action_records if record.name == pass_name]


def find_pass_work_records(action_records, pass_name):
    return [
        record
        for record in action_records
        if record.pass_name == pass_name and is_work_action(record.flags)
    ]


def collect_resource_records_raw(controller):
    if hasattr(controller, "GetResources"):
        return controller.GetResources()
    return []


def resource_id_to_name(controller):
    names = {}
    for resource in collect_resource_records_raw(controller):
        resource_id = safe_getattr(resource, "resourceId")
        name = safe_getattr(resource, "name", "")
        if resource_id is None or not name:
            continue
        names[str(resource_id)] = name
    return names


def summarize_event_ids(records, limit=8):
    if not records:
        return "<none>"

    tokens = [str(record.event_id) for record in records[:limit]]
    if len(records) > limit:
        tokens.append("...")
    return ", ".join(tokens)


def run_ui_script(report_suffix, analysis_callback):
    requested_capture_path = resolve_capture_path()
    report_path = resolve_report_path(requested_capture_path, report_suffix)
    capture_context = ensure_ui_context()

    try:
        loaded_capture_path = validate_loaded_capture(requested_capture_path)
        report = ReportWriter(report_path)
        callback_completed = {"value": False}
        callback_error = {"value": None}

        def replay_callback(controller):
            try:
                analysis_callback(controller, report, loaded_capture_path, report_path)
                callback_completed["value"] = True
            except Exception:
                callback_error["value"] = traceback.format_exc()

        capture_context.Replay().BlockInvoke(replay_callback)
        if callback_error["value"] is not None:
            raise RuntimeError("Replay callback failed:\n{}".format(callback_error["value"]))
        if not callback_completed["value"]:
            raise RuntimeError("Replay callback did not execute")
        report.flush()
    except Exception:
        write_report(
            report_path,
            [
                "analysis_result=exception",
                "requested_capture_path={}".format(
                    requested_capture_path if requested_capture_path else "<ui_loaded_capture>"
                ),
                "report_path={}".format(report_path),
                "",
                "exception_traceback:",
                traceback.format_exc(),
            ],
        )
        close_ui_cleanly()
        return 1

    close_ui_cleanly()
    return 0
