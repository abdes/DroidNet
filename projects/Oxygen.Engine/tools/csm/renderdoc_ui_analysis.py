"""Bridge module exposing shared RenderDoc UI helpers to the CSM toolset."""

import importlib.util
from pathlib import Path


def _load_shared_module():
    module_path = (
        Path(__file__).resolve().parents[2]
        / "Examples"
        / "RenderScene"
        / "renderdoc_ui_analysis.py"
    )
    spec = importlib.util.spec_from_file_location(
        "_oxygen_renderdoc_ui_analysis", module_path
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load shared RenderDoc helper: {module_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_SHARED = _load_shared_module()

REPORT_PATH_ENV = _SHARED.REPORT_PATH_ENV
CAPTURE_PATH_ENV = _SHARED.CAPTURE_PATH_ENV
PASS_NAME_ENV = _SHARED.PASS_NAME_ENV
PASS_EVENT_LIMIT_ENV = _SHARED.PASS_EVENT_LIMIT_ENV
EVENT_ID_ENV = _SHARED.EVENT_ID_ENV
RESOURCE_NAME_ENV = _SHARED.RESOURCE_NAME_ENV
RESOURCE_EVENT_LIMIT_ENV = _SHARED.RESOURCE_EVENT_LIMIT_ENV
DEFAULT_PASS_EVENT_LIMIT = _SHARED.DEFAULT_PASS_EVENT_LIMIT
DEFAULT_RESOURCE_EVENT_LIMIT = _SHARED.DEFAULT_RESOURCE_EVENT_LIMIT
KNOWN_PASS_NAMES = _SHARED.KNOWN_PASS_NAMES
ResourceRecord = _SHARED.ResourceRecord
ActionRecord = _SHARED.ActionRecord
ActionResources = _SHARED.ActionResources
ReportWriter = _SHARED.ReportWriter
get_capture_context = _SHARED.get_capture_context
ensure_ui_context = _SHARED.ensure_ui_context
renderdoc_module = _SHARED.renderdoc_module
safe_getattr = _SHARED.safe_getattr
parse_int_env = _SHARED.parse_int_env
parse_optional_int_env = _SHARED.parse_optional_int_env
get_required_env = _SHARED.get_required_env
get_process_args = _SHARED.get_process_args
normalize_path = _SHARED.normalize_path
paths_equal = _SHARED.paths_equal
current_ui_capture_path = _SHARED.current_ui_capture_path
resolve_capture_path = _SHARED.resolve_capture_path
resolve_report_path = _SHARED.resolve_report_path
write_report = _SHARED.write_report
close_ui_cleanly = _SHARED.close_ui_cleanly
validate_loaded_capture = _SHARED.validate_loaded_capture
run_ui_script = _SHARED.run_ui_script
collect_action_records = _SHARED.collect_action_records
find_scope_records = _SHARED.find_scope_records
find_pass_work_records = _SHARED.find_pass_work_records
resource_id_to_name = _SHARED.resource_id_to_name
format_flags = _SHARED.format_flags
summarize_event_ids = _SHARED.summarize_event_ids
