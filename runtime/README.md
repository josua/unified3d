# Unified3D Runtime

`unified3d-runtime` is the persistent C++20 process that owns native state and
dispatches deterministic operations. The first transport is newline-delimited
JSON-RPC 2.0 over standard input/output, intended for protocol and SDK tests.

Initial methods:

```text
runtime.hello
runtime.shutdown
analysis.validate
analysis.compare
```

The Runtime uses `nlohmann/json` only at this wire boundary. Core and Operations
remain JSON-independent. Large geometry, texture and animation buffers will
never be carried by this control-plane transport.

The dependency is pinned to `nlohmann/json` 3.12.0 and verified with the
upstream release archive SHA-256 during CMake configuration. Implicit JSON
conversions are disabled.

## Run the development transport

Build with a repository preset, then start:

```powershell
.\build\dev-gcc\runtime\unified3d-runtime.exe
```

Send one compact JSON-RPC request per line. The process stays alive until EOF or
`runtime.shutdown`. Empty lines are ignored and every response is flushed as a
single line. A control message is limited to 4 MiB; asset buffers must use the
future data plane rather than JSON.

Example request:

```json
{"jsonrpc":"2.0","id":1,"method":"runtime.hello"}
```

The stdio transport is a protocol-development and SDK-test transport. The
production local transport planned for Windows is a per-user Named Pipe.
