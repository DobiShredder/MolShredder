#!/usr/bin/env python3
"""The new shell's English/Korean catalog (SEQ-330): every qsTr() string of apps/desktop/qml_next/*.qml has exactly
one finished Korean translation in translations/molshredder_next_ko.ts under the file's context, with the same
placeholders, and the catalog holds nothing stale."""

import argparse
import re
import xml.etree.ElementTree as ET
from pathlib import Path


def unescape(text: str) -> str:
    return text.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qml-dir", type=Path, required=True)
    parser.add_argument("--ts", type=Path, required=True)
    args = parser.parse_args()

    root = ET.parse(args.ts).getroot()
    assert root.get("language") == "ko_KR" and root.get("sourcelanguage") == "en_US"
    placeholder = re.compile(r"%\d+")
    catalog: dict[str, dict[str, str]] = {}
    for context in root.findall("context"):
        name = context.findtext("name", default="")
        entries = catalog.setdefault(name, {})
        for message in context.findall("message"):
            source = message.findtext("source", default="")
            translation = message.find("translation")
            assert source, f"{name}: empty source"
            assert source not in entries, f"{name}: duplicate source {source!r}"
            assert translation is not None and translation.get("type") != "unfinished", f"{name}: unfinished {source!r}"
            translated = translation.text or ""
            assert translated, f"{name}: empty translation for {source!r}"
            assert placeholder.findall(source) == placeholder.findall(translated), f"{name}: placeholder mismatch {source!r}"
            entries[source] = translated

    total = 0
    for qml in sorted(args.qml_dir.glob("*.qml")):
        extracted = {unescape(s) for s in re.findall(r'qsTr\("((?:[^"\\]|\\.)*)"\)', qml.read_text(encoding="utf-8"))}
        known = set(catalog.get(qml.stem, {}))
        assert extracted == known, (
            f"{qml.name}: catalog drift missing={sorted(extracted - known)!r} stale={sorted(known - extracted)!r}"
        )
        total += len(extracted)
    unknown = set(catalog) - {q.stem for q in args.qml_dir.glob("*.qml")}
    assert not unknown, f"catalog has contexts without a QML file: {sorted(unknown)!r}"
    print(f"new shell catalog ready: {total} English/Korean messages")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
