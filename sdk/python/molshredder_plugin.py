"""MolShredder plugin SDK helper for out-of-process Python workers (protocol 1.0).

A worker is a program the host starts after the user approved it. It speaks
one JSON object per line on stdin/stdout (stderr is for logs). This module
implements that protocol so a plugin only supplies operation functions:

    import molshredder_plugin as sdk

    def centroid(arguments, context):
        atoms = context.host_request("selection.atoms", {"selection": arguments.get("selection", "all")})
        ...
        return sdk.Result("centroid computed", {"count": atoms["count"]})

    sdk.serve("org.example.sample", "1.0.0", {"centroid": centroid})

The operations must match the manifest exactly. Return `Result(summary, fields,
table)`; raise `PluginError(message, code, suggestion)` to report a failure.
Never write to stdout yourself: it is the protocol channel.
"""

import json
import queue
import sys
import threading

PROTOCOL_MAJOR = 1
PROTOCOL_MINOR = 0
MAX_MESSAGE_BYTES = 8 * 1024 * 1024


class PluginError(Exception):
    """A failure the plugin reports to the host."""

    def __init__(self, message, code="failed", suggestion=""):
        super().__init__(message)
        self.code = code
        self.suggestion = suggestion


class Result:
    def __init__(self, summary="", fields=None, table=None):
        self.summary = summary
        self.fields = fields or {}
        self.table = table  # {"columns": [...], "rows": [[...], ...]}


class Context:
    """What an operation may use while it runs."""

    def __init__(self, server, invocation_id):
        self._server = server
        self._id = invocation_id
        self._cancelled = False

    @property
    def cancelled(self):
        """True once the host asked to cancel; poll it in long loops."""
        self._server._drain(self)
        return self._cancelled

    def progress(self, fraction, message=""):
        self._server._send({"type": "progress", "id": self._id,
                            "fraction": float(fraction), "message": str(message)})

    def host_request(self, method, params=None):
        """Read-only data from the host, for example `selection.atoms`."""
        return self._server._host_request(self, method, params or {})


class _Server:
    def __init__(self, plugin_id, version, operations, stdin, stdout):
        self._plugin_id = plugin_id
        self._version = version
        self._operations = operations
        self._in = stdin
        self._out = stdout
        self._lines = queue.Queue()
        self._next_request = 0
        threading.Thread(target=self._read_loop, daemon=True).start()

    def _read_loop(self):
        for line in self._in:
            self._lines.put(line)
        self._lines.put(None)

    def _send(self, message):
        text = json.dumps(message, separators=(",", ":"), allow_nan=False)
        if len(text) > MAX_MESSAGE_BYTES:
            raise PluginError("the result is larger than the 8 MiB message limit")
        self._out.write(text + "\n")
        self._out.flush()

    def _receive(self, block):
        try:
            line = self._lines.get(block=block)
        except queue.Empty:
            return None
        if line is None:
            raise SystemExit(0)
        return json.loads(line)

    def _drain(self, context):
        while True:
            message = self._receive(block=False)
            if message is None:
                return
            if message.get("type") == "cancel" and message.get("id") == context._id:
                context._cancelled = True

    def _host_request(self, context, method, params):
        self._next_request += 1
        request_id = self._next_request
        self._send({"type": "host_request", "id": context._id, "request_id": request_id,
                    "method": method, "params": params})
        while True:
            message = self._receive(block=True)
            if message.get("type") == "cancel" and message.get("id") == context._id:
                context._cancelled = True
            elif message.get("type") == "host_response" and message.get("request_id") == request_id:
                if not message.get("ok"):
                    raise PluginError(message.get("error", "the host refused the request"), "failed")
                return message["result"]

    def serve(self):
        hello = self._receive(block=True)
        if hello is None or hello.get("type") != "hello":
            raise SystemExit("expected a hello from the host")
        self._send({"type": "hello", "protocol": {"major": PROTOCOL_MAJOR, "minor": PROTOCOL_MINOR},
                    "plugin": {"id": self._plugin_id, "version": self._version},
                    "operations": sorted(self._operations)})
        while True:
            message = self._receive(block=True)
            kind = message.get("type")
            if kind == "shutdown":
                return
            if kind != "invoke":
                continue
            invocation_id = message["id"]
            handler = self._operations.get(message.get("operation"))
            if handler is None:
                self._send({"type": "error", "id": invocation_id, "code": "not_found",
                            "message": "unknown operation"})
                continue
            context = Context(self, invocation_id)
            try:
                result = handler(message.get("arguments", {}), context)
                if not isinstance(result, Result):
                    raise PluginError("the operation did not return a Result")
                reply = {"type": "result", "id": invocation_id, "summary": result.summary,
                         "fields": result.fields}
                if result.table is not None:
                    reply["table"] = result.table
                self._send(reply)
            except PluginError as error:
                self._send({"type": "error", "id": invocation_id, "code": error.code,
                            "message": str(error), "suggestion": error.suggestion})
            except Exception as error:  # a plugin bug must not kill the protocol
                self._send({"type": "error", "id": invocation_id, "code": "failed",
                            "message": f"{type(error).__name__}: {error}"})


def serve(plugin_id, version, operations, stdin=None, stdout=None):
    """Runs the protocol until the host asks the worker to shut down."""
    _Server(plugin_id, version, operations, stdin or sys.stdin, stdout or sys.stdout).serve()
