#!/usr/bin/env python3

import re
import sys


_QUOTED_TOKEN = re.compile(r'"([^"\n]*)"')


def portable_console_script(script: str, platform: str | None = None) -> str:
    """Normalize only absolute Windows paths inside canonical quoted tokens."""
    if (platform is None):
        platform = sys.platform
    if platform != "win32":
        return script

    def normalize(match: re.Match[str]) -> str:
        value = match.group(1)
        is_drive_path = (len(value) >= 3 and value[0].isalpha() and
                         value[1] == ":" and value[2] == "\\")
        is_unc_path = value.startswith("\\\\")
        if not is_drive_path and not is_unc_path:
            return match.group(0)
        return '"' + value.replace("\\", "/") + '"'

    return _QUOTED_TOKEN.sub(normalize, script)
