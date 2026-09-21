"""C++ literal and comment regions shared by source-editing tools."""

import re

REGIONS = re.compile(
    r'\b[0-9][\w.\']*|//[^\r\n]*|/\*[\s\S]*?\*/|(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)"|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\''
)
