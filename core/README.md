# Unified3D Core

`unified3d-core` is the C++20 data and invariant layer. It has no frontend,
renderer, JSON, adapter or proprietary SDK dependency.

The first executable slice contains:

- the canonical `unified3d.analysis/1.0-rc1` in-memory model;
- explicit optional measurements preserving the `0` versus unavailable rule;
- coordinate-system, bounds, geometry, material, skeleton, skin and animation
  analysis sections;
- deterministic semantic validation and structured diagnostics.

Format-native sections are retained as opaque content-typed byte payloads. This
preserves arbitrary nested FBX/glTF extension data without forcing a JSON value
type or a format API into Core.

JSON parsing belongs at an adapter or Runtime boundary. Keeping it outside the
Core prevents the wire library from becoming a dependency of geometry code.
