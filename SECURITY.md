# Security Policy

Unified3D processes 3D files and external resources that may be untrusted. Parser, resource-resolution and memory-safety issues are therefore treated as security-relevant.

## Security-sensitive areas

Examples include:

- malformed FBX, glTF/GLB, USD or future format payloads;
- out-of-bounds buffer access;
- excessive memory allocation;
- recursion or nesting exhaustion;
- unsafe path traversal;
- external URI resolution;
- unexpected network access;
- crashes in native parsers or third-party adapters;
- stale or forged frontend handles;
- unsafe plugin execution.

## Default security direction

The v0.2 architecture requires:

```text
network access = disabled by default
resource access = LocalOnly + Relative by default
path normalization = required
buffer bounds validation = required
maximum allocation = bounded
maximum recursion depth = bounded
```

Risky adapters or plugins may later be isolated in worker processes.

## Reporting a vulnerability

Please use GitHub's private security reporting / security advisory mechanism for this repository when available. Avoid publishing exploit details in a public issue before a fix or mitigation is available.
