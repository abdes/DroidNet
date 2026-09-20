"""Exact documentation-token edits, proposed only for manual review."""

import re

from ..lexical import byte_span, replacement_name


class TextDriver:
    SUPPORTED_EXTENSIONS = frozenset({".md", ".txt", ".rst"})

    def __init__(self, context):
        self.ctx = context

    def generate_edits(self, files):
        edits = []
        if self.ctx.args.mode != "aggressive":
            return [], edits
        pattern = re.compile(r"(?<!\w)" + re.escape(self.ctx.args.from_sym) + r"(?!\w)")
        for path in files:
            text = self.ctx.sources.read(path).decode("utf-8")
            for match in pattern.finditer(text):
                start, end = byte_span(text, match.start(), match.end())
                edits.append(
                    self.ctx.sources.edit(
                        path,
                        start,
                        end,
                        replacement_name(self.ctx.args.from_sym, self.ctx.args.to_sym),
                        "documentation match; symbol identity is not established",
                    )
                )
        return [], edits
