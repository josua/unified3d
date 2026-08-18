# Unified3D Repository Bootstrap Manifest

This manifest records the intended first documentation-only repository commit.

## Intended branch

```text
agent/bootstrap-documentation
```

## Intended base

```text
main
```

## Intended commit message

```text
bootstrap Unified3D documentation
```

## Intended pull request

```text
Title: Bootstrap Unified3D documentation and architecture
Draft: true
Base: main
Head: agent/bootstrap-documentation
```

## Files

```text
README.md
CONTRIBUTING.md
SECURITY.md
BOOTSTRAP_MANIFEST.md
docs/README.md
docs/architecture/ARCHITECTURE.md
docs/specifications/Unified3D_Implementation_Specification_v0.2.0.md
docs/protocols/RPC_PROTOCOL.md
docs/operations/OPERATIONS.md
docs/formats/FORMAT_ADAPTERS.md
```

## Explicitly omitted

```text
LICENSE
```

Reason: the v0.2 specification explicitly leaves the open-source license undecided between Apache-2.0, MIT and BSD-3-Clause.

No implementation directories are populated in this bootstrap commit. They should appear when concrete implementation begins rather than as empty Git directories.
