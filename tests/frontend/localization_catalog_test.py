#!/usr/bin/env python3

import argparse
import re
import xml.etree.ElementTree as ET
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qml", type=Path, required=True)
    parser.add_argument("--ts", type=Path, required=True)
    args = parser.parse_args()

    root = ET.parse(args.ts).getroot()
    assert root.get("language") == "ko_KR"
    assert root.get("sourcelanguage") == "en_US"

    messages = root.findall(".//message")
    assert messages, "translation catalog is empty"
    sources: set[str] = set()
    placeholder = re.compile(r"%\d+")
    for message in messages:
        source = message.findtext("source", default="")
        translation = message.find("translation")
        assert source, "catalog contains an empty source string"
        assert source not in sources, f"duplicate translation source: {source}"
        sources.add(source)
        assert translation is not None
        assert translation.get("type") != "unfinished", f"unfinished: {source}"
        translated = translation.text or ""
        assert translated, f"empty translation: {source}"
        assert placeholder.findall(source) == placeholder.findall(translated), (
            f"placeholder mismatch: {source} -> {translated}"
        )

    qml = args.qml.read_text(encoding="utf-8")
    extracted = set(re.findall(r'qsTr\("((?:[^"\\]|\\.)*)"\)', qml))
    assert extracted == sources, (
        f"catalog drift: missing={sorted(extracted - sources)!r} "
        f"stale={sorted(sources - extracted)!r}"
    )
    print(f"localization catalog ready: {len(messages)} English/Korean messages")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
