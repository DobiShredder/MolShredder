#!/usr/bin/env python3

import argparse
import re
import xml.etree.ElementTree as ET
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qml", type=Path, required=True)
    parser.add_argument("--actions", type=Path, required=True)
    parser.add_argument("--action-translations", type=Path, required=True)
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
    action_source = args.actions.read_text(encoding="utf-8")
    action_ids = set(re.findall(r'\.id\s*=\s*"([a-z0-9_.-]+)"', action_source))
    metadata_bindings = dict(
        re.findall(
            r'readonly property var (\w+):\s*localization\.actionMetadata\("([^"]+)"\)',
            qml,
        )
    )
    assert set(metadata_bindings.values()) == action_ids, (
        f"QML action metadata projection drift: "
        f"missing={sorted(action_ids - set(metadata_bindings.values()))!r} "
        f"stale={sorted(set(metadata_bindings.values()) - action_ids)!r}"
    )
    palette_bindings = set(re.findall(r'metadata:\s*root\.(\w+)', qml))
    assert palette_bindings == set(metadata_bindings), (
        f"command palette projection drift: "
        f"missing={sorted(set(metadata_bindings) - palette_bindings)!r} "
        f"stale={sorted(palette_bindings - set(metadata_bindings))!r}"
    )
    menu_names = [
        "fileMenu",
        "editMenu",
        "objectMenu",
        "selectMenu",
        "representMenu",
        "analyzeMenu",
        "trajectoryMenu",
        "sceneMenu",
        "toolsMenu",
        "helpMenu",
    ]
    menu_offsets = [qml.index(f'objectName: "{name}"') for name in menu_names]
    assert menu_offsets == sorted(menu_offsets), "top-level menu taxonomy order drifted"
    action_strings = set(
        re.findall(
            r'\.(?:label_source|status_source|error_source|keywords_source|unavailable_source)'
            r'\s*=\s*\n?\s*"((?:[^"\\]|\\.)*)"',
            action_source,
        )
    )
    action_strings.discard("")
    action_translation_source = args.action_translations.read_text(encoding="utf-8")
    action_translation_strings = set(
        re.findall(
            r'QT_TRANSLATE_NOOP\("Main",\s*"((?:[^"\\]|\\.)*)"\)',
            action_translation_source,
        )
    )
    assert action_translation_strings == action_strings, (
        "action translation marker drift: "
        f"missing={sorted(action_strings - action_translation_strings)!r} "
        f"stale={sorted(action_translation_strings - action_strings)!r}"
    )
    extracted.update(action_strings)
    extracted.discard("")
    assert extracted == sources, (
        f"catalog drift: missing={sorted(extracted - sources)!r} "
        f"stale={sorted(sources - extracted)!r}"
    )
    print(f"localization catalog ready: {len(messages)} English/Korean messages")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
