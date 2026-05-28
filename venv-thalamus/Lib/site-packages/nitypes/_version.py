from __future__ import annotations

import re


def parse_version(version: str) -> tuple[int, int, int]:
    """Parse a version number into a major/minor/update tuple, ignoring suffixes.

    This is a minimal version parser to avoid requiring the ``packaging`` package.

    >>> parse_version("1.2.3")
    (1, 2, 3)
    >>> parse_version("123.456.789")
    (123, 456, 789)
    >>> parse_version("1.0")
    (1, 0, 0)
    >>> parse_version("1")
    (1, 0, 0)
    >>> parse_version("1.2.3-dev0")
    (1, 2, 3)
    """
    match = re.match(r"^(\d+)(?:\.(\d+)(?:\.(\d+))?)?", version)
    if match is None:
        raise ValueError(f"Invalid version number: {version}")
    return int(match.group(1)), int(match.group(2) or "0"), int(match.group(3) or "0")
