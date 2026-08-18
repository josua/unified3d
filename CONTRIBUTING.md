# Contributing to Unified3D

Unified3D is currently in a documentation-first architecture phase. Contributions should preserve the explicit boundaries defined by the current implementation specification.

## Before contributing

Read:

1. [README.md](README.md)
2. [docs/architecture/ARCHITECTURE.md](docs/architecture/ARCHITECTURE.md)
3. [docs/specifications/Unified3D_Implementation_Specification_v0.2.0.md](docs/specifications/Unified3D_Implementation_Specification_v0.2.0.md)

## Architectural constraints

Contributions must not silently violate these v0.2 decisions:

```text
Core = C++20
Runtime = separate native process
Workbench = TypeScript frontend
Workbench ↔ Runtime = RPC over local IPC
Initial RPC = JSON-RPC 2.0
Windows IPC = Named Pipe
Linux/macOS IPC = Unix Domain Socket
Frontends manipulate handles
Heavy buffers are not serialized to JSON
Operation != Node
Renderer is optional
Native format data is preserved
Autodesk SDK backend is optional and external
```

## Documentation changes

For architecture changes:

- explain the motivation;
- identify the affected layer;
- state whether the change modifies a v0.2 decision or an open question;
- update the versioned implementation specification when a decision changes;
- keep narrower protocol/operation/format documents synchronized.

## Implementation changes

Implementation has not started in this bootstrap revision. When source code is introduced, contributions should prefer small, auditable dependencies and deterministic operations independent of frontend UI code.

## Third-party and proprietary SDKs

Do not commit Autodesk FBX SDK headers, libraries, DLLs, source code or other redistributable-restricted assets to this repository.

Adapters for proprietary SDKs must keep those dependencies external and optional.

## Test fixtures

When implementation begins, every regression should be accompanied by a minimal reproducible fixture when licensing permits. Test assets must have clear provenance and redistribution rights.
