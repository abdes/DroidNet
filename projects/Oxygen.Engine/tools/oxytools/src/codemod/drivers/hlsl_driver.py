"""Token-only HLSL proposals with comments and literals protected by default."""

from ..lexical import byte_span, replacement_name, spans


class HlslDriver:
    SUPPORTED_EXTENSIONS = frozenset({".hlsl", ".hlsli"})

    def __init__(self, context):
        self.ctx = context

    def generate_edits(self, files):
        edits = []
        for path in files:
            text = self.ctx.sources.read(path).decode("utf-8")
            regions = (False, True) if self.ctx.args.update_strings else (False,)
            for protected in regions:
                reason = "comment/string match" if protected else "HLSL token match"
                for start, end in spans(
                    text, self.ctx.args.from_sym, regions=protected
                ):
                    start, end = byte_span(text, start, end)
                    edits.append(
                        self.ctx.sources.edit(
                            path,
                            start,
                            end,
                            replacement_name(
                                self.ctx.args.from_sym, self.ctx.args.to_sym
                            ),
                            reason + "; symbol identity is not established",
                        )
                    )
        return [], edits
