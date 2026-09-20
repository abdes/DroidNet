"""Review-only JSON/YAML scalar edits with duplicate-key and syntax checks."""

import json
import re

import yaml
from oxytools.common import ToolError, yaml_documents

from ..lexical import byte_span, replacement_name
from ..patcher import apply_edits

STRINGS = re.compile(r'"(?:\\.|[^"\\])*"')


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ToolError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def _json(text):
    return json.loads(text.lstrip("\ufeff"), object_pairs_hook=_unique_object)


class JsonDriver:
    SUPPORTED_EXTENSIONS = frozenset({".json", ".scene", ".yaml", ".yml"})

    def __init__(self, context):
        self.ctx = context
        self.new = replacement_name(context.args.from_sym, context.args.to_sym)

    def _edit(self, path, text, start, end, replacement, kind):
        start, end = byte_span(text, start, end)
        return self.ctx.sources.edit(
            path,
            start,
            end,
            replacement,
            f"{kind} scalar match; symbol identity is not established",
        )

    def _yaml_edits(self, path, text):
        yaml_documents(text)
        proposed, seen = [], set()

        def visit(node):
            if node is None or id(node) in seen:
                return
            seen.add(id(node))
            if isinstance(node, yaml.ScalarNode):
                if (
                    node.tag != "tag:yaml.org,2002:str"
                    or node.value != self.ctx.args.from_sym
                ):
                    return
                raw = text[node.start_mark.index : node.end_mark.index]
                if raw.startswith(("!", "&")) or node.style in ("|", ">"):
                    raise ToolError(
                        f"Rename the tagged, anchored, or block YAML scalar manually: {path}:{node.start_mark.line + 1}"
                    )
                replacement = json.dumps(self.new, ensure_ascii=False)
                if node.style == "'":
                    replacement = "'" + self.new.replace("'", "''") + "'"
                elif node.style is None:
                    try:
                        if yaml.safe_load(self.new) == self.new:
                            replacement = self.new
                    except yaml.YAMLError:
                        pass  # A quoted string preserves this replacement value.
                proposed.append(
                    self._edit(
                        path,
                        text,
                        node.start_mark.index,
                        node.end_mark.index,
                        replacement,
                        "YAML",
                    )
                )
            elif isinstance(node, yaml.MappingNode):
                for key, value in node.value:
                    visit(key)
                    visit(value)
            elif isinstance(node, yaml.SequenceNode):
                for value in node.value:
                    visit(value)

        for document in yaml.compose_all(text):
            visit(document)
        return proposed

    def generate_edits(self, files):
        edits = []
        for path in files:
            data = self.ctx.sources.read(path)
            text = data.decode("utf-8")
            if path.suffix.lower() in {".json", ".scene"}:
                _json(text)
                proposed = [
                    self._edit(
                        path,
                        text,
                        match.start(),
                        match.end(),
                        json.dumps(self.new, ensure_ascii=False),
                        "JSON",
                    )
                    for match in STRINGS.finditer(text)
                    if json.loads(match.group()) == self.ctx.args.from_sym
                ]
                _json(apply_edits(data, proposed).decode("utf-8"))
            else:
                proposed = self._yaml_edits(path, text)
                yaml_documents(apply_edits(data, proposed).decode("utf-8"))
            edits.extend(proposed)
        return [], edits
