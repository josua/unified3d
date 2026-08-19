# Unified3D native adapters

`unified3d-adapters` translates FBX/glTF files into a neutral, UI-independent
buffer contract owned by the native Runtime.

Backends:

- `cgltf` 1.15 for `.gltf` and `.glb`;
- `ufbx` 0.23.0 for portable `.fbx` decoding;
- Autodesk FBX SDK 2020.3.10 when explicitly enabled in an MSVC build.

The open-source dependencies are pinned to exact upstream commits in
`cmake/Dependencies.cmake`. Autodesk SDK files remain external and are detected
through `UNIFIED3D_AUTODESK_FBX_SDK_ROOT`.

The output contract contains immutable position and triangle-index buffers,
optional ordered skin influence sets, a global joint-name palette, primitive
instance transforms and an explicit geometric/render vertex domain. World
space is normalized to right-handed Y-up meters.

This layer does not perform spatial correspondence, barycentric interpolation,
weight transfer, animation transfer, rendering or UI work.
