#!/usr/bin/env python3

from console_script import portable_console_script


def main() -> int:
    source = (
        'invoke "load" --path "C:\\Users\\runner\\model.pqr" '
        '--selection "name \\t"\n'
        'invoke "load" --path "\\\\server\\share\\model.pdb"\n'
    )
    expected = (
        'invoke "load" --path "C:/Users/runner/model.pqr" '
        '--selection "name \\t"\n'
        'invoke "load" --path "//server/share/model.pdb"\n'
    )
    assert portable_console_script(source, platform="win32") == expected
    assert portable_console_script(source, platform="darwin") == source
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
