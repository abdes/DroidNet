from __future__ import annotations

from typing import Dict, List, Optional, Tuple

__all__ = ["parse_forwarded_arguments"]


def parse_forwarded_arguments(tokens: List[str]) -> Tuple[Dict[str, object], List[str]]:
    """Parse loose CLI tokens into a task argument dictionary.

    The parser supports ``-Name Value`` and ``--name value`` styles, boolean
    switches, repeated keys (aggregated into lists), and ``--key=value`` forms.
    Non-option tokens are returned in the list of extras.
    """

    arguments: Dict[str, object] = {}
    extras: List[str] = []

    i = 0
    while i < len(tokens):
        token = tokens[i]
        if token.startswith("-") and len(token) > 1:
            key, value, increment = _extract_option(token, tokens, i)
            i += increment
            if key is None:
                extras.append(token)
                continue
            current = arguments.get(key)
            if current is None:
                arguments[key] = value
            else:
                if not isinstance(current, list):
                    arguments[key] = [current]
                    current = arguments[key]
                current.append(value)  # type: ignore[assignment]
        else:
            extras.append(token)
            i += 1
    return arguments, extras


def _extract_option(token: str, tokens: List[str], index: int) -> Tuple[Optional[str], object, int]:
    token = token.lstrip("-")
    name: str
    value: object = True
    consumed = 1

    if "=" in token:
        name, assigned = token.split("=", 1)
        if assigned:
            value = assigned
    else:
        name = token
        next_index = index + 1
        if next_index < len(tokens):
            candidate = tokens[next_index]
            if not candidate.startswith("-"):
                value = candidate
                consumed += 1
    if not name:
        return None, True, consumed
    return name.lower(), value, consumed
