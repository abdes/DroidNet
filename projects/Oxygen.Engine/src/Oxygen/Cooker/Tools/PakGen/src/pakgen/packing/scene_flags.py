"""Canonical v5 scene-node flag masks shared by validation and packing."""

from typing import Any, Mapping

from .constants import SCENE_NODE_FLAGS_INHERITABLE, SCENE_NODE_FLAGS_KNOWN
from .errors import PakError


def node_flag_masks(node: Mapping[str, Any]) -> tuple[int, int]:
    """Return explicit local values and inheritance modes without coercion."""
    flags = node.get("flags", 0)
    inherited = node.get("inherited_flags", 0)
    for name, value in (("flags", flags), ("inherited_flags", inherited)):
        if type(value) is not int:
            raise PakError("E_TYPE", f"{name} must be an unsigned integer")
        if value < 0 or value > 0xFFFFFFFF:
            raise PakError("E_RANGE", f"{name} must fit uint32")
    if flags & ~SCENE_NODE_FLAGS_KNOWN:
        raise PakError("E_SCENE_FLAGS", "flags contains unknown scene-node bits")
    if inherited & ~SCENE_NODE_FLAGS_INHERITABLE:
        raise PakError(
            "E_SCENE_FLAGS",
            "inherited_flags supports only Visible, CastsShadows and ReceivesShadows",
        )
    if flags & inherited:
        raise PakError("E_SCENE_FLAGS", "flags and inherited_flags must not overlap")
    return flags, inherited
