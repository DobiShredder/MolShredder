#!/bin/sh
# Launcher: the host starts this file (it is hash-pinned together with plugin.py
# and molshredder_plugin.py). The host gives workers a minimal environment, so
# the interpreter is named explicitly; change the path to the Python you want.
exec /usr/bin/python3 "$MOLSHREDDER_PLUGIN_DIR/plugin.py"
