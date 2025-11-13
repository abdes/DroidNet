"""Task plugins for the traversal tool."""

from .invoke_tests import invoke_tests  # noqa: F401
from .new_package import new_package  # noqa: F401
from .select_name import select_name  # noqa: F401
from .select_path import select_path  # noqa: F401

__all__ = [
    "invoke_tests",
    "new_package",
    "select_name",
    "select_path",
]
