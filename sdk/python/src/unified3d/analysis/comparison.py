from copy import deepcopy
from dataclasses import dataclass
import json
import math
import re
from typing import Any


SCHEMA = "unified3d.analysis-comparison/1.0-rc1"
MISSING = "—"


def _nested(data: dict[str, Any], *path: str, default: Any = None) -> Any:
    current: Any = data
    for key in path:
        if not isinstance(current, dict) or key not in current:
            return default
        current = current[key]
    return current


def _first(*values: Any) -> Any:
    for value in values:
        if value is not None:
            return value
    return None


def _integer(value: Any) -> int | None:
    if value is None or isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, float) and math.isfinite(value):
        return int(value)

    text = str(value).strip()
    if not text or text.casefold() in {"null", "none", "n/a", "unknown", "—", "-"}:
        return None
    cleaned = re.sub(r"[\s,_\u202f\u00a0]", "", text)
    try:
        return int(cleaned)
    except ValueError:
        return None


def _boolean(value: Any) -> bool | None:
    if isinstance(value, bool):
        return value
    if value is None:
        return None
    text = str(value).strip().casefold()
    if text in {"true", "yes", "1", "oui", "vrai"}:
        return True
    if text in {"false", "no", "0", "non", "faux"}:
        return False
    return None


def _strip_code_fence(text: str) -> str:
    lines = text.strip().splitlines()
    if len(lines) >= 2 and lines[0].strip().startswith("```") and lines[-1].strip() == "```":
        return "\n".join(lines[1:-1]).strip()
    return text.strip()


def _summary_fields(text: str) -> tuple[str, dict[str, str]]:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    if not lines:
        raise ValueError("The analysis input is empty")

    header = lines[0]
    fields: dict[str, str] = {}
    for line in lines[1:]:
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        fields[key.strip().casefold()] = value.strip()
    return header, fields


def _detect_json_kind(data: dict[str, Any]) -> str:
    canonical_format = str(_nested(data, "asset", "format", default="")).casefold()
    container = str(_nested(data, "asset", "container", default="")).casefold()
    gltf_version = _nested(data, "asset", "gltf_version")
    fbx_version = _nested(data, "file", "fbx_version")
    analyzer = " ".join(
        str(value)
        for value in (
            _nested(data, "analyzer", "name"),
            _nested(data, "analyzer", "backend"),
        )
        if value
    ).casefold()

    if canonical_format == "gltf" or container in {"glb", "gltf"} or gltf_version is not None or "gltf" in analyzer or "glb" in analyzer:
        return "GLB"
    if canonical_format == "fbx" or container == "fbx" or fbx_version is not None or "fbx" in analyzer:
        return "FBX"
    return "Unknown"


def _base_normalized(kind: str, source_format: str) -> dict[str, Any]:
    return {
        "kind": kind,
        "source_format": source_format,
        "file": None,
        "version": None,
        "generator": None,
        "mesh_count": None,
        "mesh_instance_count": None,
        "primitive_count": None,
        "vertex_count": None,
        "vertex_semantic": None,
        "unique_position_tuple_count": None,
        "polygon_vertex_count": None,
        "polygon_count": None,
        "index_count": None,
        "triangle_count": None,
        "degenerate_triangle_count": None,
        "uv_set_count": None,
        "material_count": None,
        "texture_count": None,
        "image_count": None,
        "file_size_bytes": None,
        "texture_encoded_bytes": None,
        "texture_max_width": None,
        "texture_max_height": None,
        "texture_resolution_counts": {},
        "native_node_count": None,
        "skeleton_element_count": None,
        "skeleton_element_semantic": None,
        "skin_count": None,
        "has_skin": None,
        "max_influences": None,
        "animation_count": None,
        "raw_animation_count": None,
        "technical_animation_count": None,
        "duplicate_animation_count": None,
        "clip_names": [],
        "effective_clip_names": [],
        "animation_channel_count": None,
        "animation_sampler_count": None,
        "animation_duration_seconds": None,
        "draco": None,
        "meshopt": None,
        "diagnostic_count": None,
        "topology_signature": None,
        "topology_signature_kind": None,
        "topology_signature_domain": None,
        "coordinate_system": None,
        "bounds": None,
        "position_bounds": None,
        "node_transformed_bounds": None,
        "spatial_details": None,
    }


def _normalize_json(data: dict[str, Any]) -> dict[str, Any]:
    kind = _detect_json_kind(data)
    result = _base_normalized(kind, "json")
    geometry = data.get("geometry", {}) if isinstance(data.get("geometry"), dict) else {}

    if data.get("schema") == "unified3d.analysis/1.0-rc1":
        asset = data.get("asset", {}) if isinstance(data.get("asset"), dict) else {}
        scene = data.get("scene", {}) if isinstance(data.get("scene"), dict) else {}
        materials = data.get("materials", {}) if isinstance(data.get("materials"), dict) else {}
        skeleton = data.get("skeleton", {}) if isinstance(data.get("skeleton"), dict) else {}
        skin = data.get("skin", {}) if isinstance(data.get("skin"), dict) else {}
        animation = data.get("animation", {}) if isinstance(data.get("animation"), dict) else {}
        signature = geometry.get("topology_signature")
        if not isinstance(signature, dict):
            signature = {}
        diagnostics = data.get("diagnostics")
        native_format = "gltf" if kind == "GLB" else "fbx" if kind == "FBX" else "legacy"
        native = _nested(data, "native", native_format, default={})
        if not isinstance(native, dict):
            native = {}
        native_animation = native.get("animation", {})
        if not isinstance(native_animation, dict):
            native_animation = {}
        spatial_details = native.get("spatial")
        if not isinstance(spatial_details, dict):
            spatial_details = None
        geometric_count = _integer(geometry.get("geometric_vertex_count"))
        render_count = _integer(geometry.get("render_vertex_count"))
        vertex_count = render_count if kind == "GLB" and render_count is not None else geometric_count
        vertex_semantic = (
            "render vertices"
            if kind == "GLB" and render_count is not None
            else str(geometry.get("geometric_vertex_semantic") or "").replace("_", " ") or None
        )
        result.update(
            {
                "source_format": "canonical-json",
                "file": asset.get("path"),
                "version": asset.get("version"),
                "generator": _first(asset.get("generator"), _nested(data, "analyzer", "backend")),
                "mesh_count": _integer(geometry.get("mesh_count")),
                "mesh_instance_count": _integer(scene.get("mesh_instance_count")),
                "primitive_count": _integer(geometry.get("primitive_count")),
                "vertex_count": vertex_count,
                "vertex_semantic": vertex_semantic,
                "unique_position_tuple_count": (
                    geometric_count
                    if geometry.get("geometric_vertex_semantic") == "unique_positions"
                    else None
                ),
                "polygon_vertex_count": _integer(geometry.get("polygon_vertex_count")),
                "polygon_count": _integer(geometry.get("polygon_count")),
                "index_count": _integer(geometry.get("index_count")),
                "triangle_count": _integer(geometry.get("triangle_count")),
                "degenerate_triangle_count": _integer(geometry.get("degenerate_triangle_count")),
                "uv_set_count": _integer(
                    geometry.get("uv_channel_count")
                    if kind == "GLB"
                    else geometry.get("uv_set_binding_count")
                ),
                "material_count": _integer(materials.get("material_resource_count")),
                "texture_count": _integer(materials.get("texture_resource_count")),
                "skeleton_element_count": _integer(skeleton.get("joint_count")),
                "skeleton_element_semantic": "joints",
                "skin_count": _integer(skin.get("skin_count")),
                "has_skin": _boolean(skin.get("present")),
                "max_influences": _integer(skin.get("max_influences")),
                "animation_count": _integer(animation.get("clip_count")),
                "raw_animation_count": _integer(
                    _first(
                        native_animation.get("raw_animation_stack_count"),
                        native_animation.get("raw_animation_count"),
                        native_animation.get("animation_stack_count"),
                        animation.get("clip_count"),
                    )
                ),
                "technical_animation_count": _integer(
                    _first(
                        native_animation.get("technical_stack_count"),
                        native_animation.get("technical_clip_count"),
                    )
                ),
                "duplicate_animation_count": _integer(
                    _first(
                        native_animation.get("duplicate_stack_count"),
                        native_animation.get("duplicate_clip_count"),
                    )
                ),
                "clip_names": list(native_animation.get("clip_names", native_animation.get("stack_names", [])))
                if isinstance(native_animation.get("clip_names", native_animation.get("stack_names", [])), list)
                else [],
                "effective_clip_names": list(native_animation.get("effective_clip_names", []))
                if isinstance(native_animation.get("effective_clip_names"), list)
                else [],
                "animation_channel_count": _integer(animation.get("channel_count")),
                "animation_sampler_count": _integer(animation.get("sampler_count")),
                "animation_duration_seconds": animation.get("duration_seconds"),
                "draco": _boolean(native.get("draco_compressed")),
                "meshopt": _boolean(native.get("meshopt_compressed")),
                "diagnostic_count": len(diagnostics) if isinstance(diagnostics, list) else None,
                "topology_signature": signature.get("digest"),
                "topology_signature_kind": signature.get("algorithm"),
                "topology_signature_domain": signature.get("domain"),
                "coordinate_system": asset.get("coordinate_system"),
                "bounds": _first(geometry.get("bounds"), scene.get("bounds")),
                "position_bounds": (
                    _first(
                        spatial_details.get("position_bounds"),
                        spatial_details.get("local_bounding_box"),
                    )
                    if spatial_details
                    else None
                ),
                "node_transformed_bounds": (
                    _first(
                        spatial_details.get("node_transformed_bounds"),
                        spatial_details.get("world_bounding_box"),
                    )
                    if spatial_details
                    else None
                ),
                "spatial_details": spatial_details,
            }
        )
        return result

    if kind == "FBX":
        rig = data.get("rig", {}) if isinstance(data.get("rig"), dict) else {}
        animation = data.get("animation", {}) if isinstance(data.get("animation"), dict) else {}
        scene = data.get("scene", {}) if isinstance(data.get("scene"), dict) else {}
        effective_animation_count = _integer(
            _first(animation.get("effective_clip_count"), animation.get("animation_stack_count"))
        )
        result.update(
            {
                "file": _nested(data, "file", "path"),
                "version": _nested(data, "file", "fbx_version"),
                "generator": _nested(data, "analyzer", "backend"),
                "mesh_count": _integer(geometry.get("mesh_count")),
                "vertex_count": _integer(geometry.get("control_point_count")),
                "vertex_semantic": "control points",
                "polygon_vertex_count": _integer(geometry.get("polygon_vertex_count")),
                "polygon_count": _integer(geometry.get("polygon_count")),
                "triangle_count": _integer(geometry.get("triangle_count")),
                "uv_set_count": _integer(_nested(data, "attributes", "uv_set_count")),
                "material_count": _integer(_nested(data, "materials", "material_count")),
                "texture_count": _integer(_nested(data, "materials", "texture_object_count")),
                "skeleton_element_count": _integer(rig.get("bone_count")),
                "skeleton_element_semantic": "bones",
                "skin_count": _integer(rig.get("skin_deformer_count")),
                "has_skin": _boolean(rig.get("has_skin")),
                "max_influences": _integer(rig.get("max_influences_per_control_point")),
                "animation_count": effective_animation_count,
                "raw_animation_count": _integer(
                    _first(animation.get("raw_animation_stack_count"), animation.get("animation_stack_count"))
                ),
                "technical_animation_count": _integer(animation.get("technical_stack_count")),
                "duplicate_animation_count": _integer(animation.get("duplicate_stack_count")),
                "clip_names": list(animation.get("stack_names", []))
                if isinstance(animation.get("stack_names"), list)
                else [],
                "effective_clip_names": list(animation.get("effective_clip_names", []))
                if isinstance(animation.get("effective_clip_names"), list)
                else [],
                "topology_signature": geometry.get("topology_signature"),
                "topology_signature_kind": "fbx_adapter_topology",
                "topology_signature_domain": "adapter-local-decoded-topology",
                "coordinate_system": {
                    "handedness": scene.get("handedness"),
                    "up_axis": scene.get("up_axis"),
                    "forward_axis": _first(scene.get("forward_axis"), scene.get("front_axis")),
                    "unit": "meter" if scene.get("meters_per_unit") == 1 else "source_unit",
                    "meters_per_unit": scene.get("meters_per_unit"),
                },
                "bounds": _first(scene.get("world_bounding_box"), scene.get("local_bounding_box")),
                "position_bounds": scene.get("local_bounding_box"),
                "node_transformed_bounds": scene.get("world_bounding_box"),
                "spatial_details": scene,
            }
        )
        return result

    if kind == "GLB":
        skeleton = data.get("skeleton", {}) if isinstance(data.get("skeleton"), dict) else {}
        skin = data.get("skin", {}) if isinstance(data.get("skin"), dict) else {}
        animation = data.get("animation", {}) if isinstance(data.get("animation"), dict) else {}
        spatial = data.get("spatial", {}) if isinstance(data.get("spatial"), dict) else {}
        diagnostics = data.get("diagnostics")
        result.update(
            {
                "file": _nested(data, "asset", "path"),
                "version": _nested(data, "asset", "gltf_version"),
                "generator": _nested(data, "asset", "generator"),
                "mesh_count": _integer(geometry.get("mesh_count")),
                "mesh_instance_count": _integer(geometry.get("mesh_instance_count")),
                "primitive_count": _integer(geometry.get("primitive_count")),
                "vertex_count": _integer(geometry.get("render_vertex_count")),
                "vertex_semantic": "render vertices",
                "unique_position_tuple_count": _integer(geometry.get("unique_position_tuple_count")),
                "index_count": _integer(geometry.get("explicit_index_count")),
                "triangle_count": _integer(geometry.get("triangle_count")),
                "degenerate_triangle_count": _integer(geometry.get("degenerate_triangle_count")),
                "uv_set_count": _integer(geometry.get("uv_set_count")),
                "material_count": _integer(_nested(data, "materials", "material_count")),
                "texture_count": _integer(_nested(data, "materials", "texture_count")),
                "image_count": _integer(_nested(data, "materials", "image_count")),
                "file_size_bytes": _integer(_nested(data, "asset", "size_bytes")),
                "texture_encoded_bytes": _integer(
                    _nested(data, "materials", "texture_encoded_bytes")
                ),
                "texture_max_width": _integer(
                    _nested(data, "materials", "texture_max_width")
                ),
                "texture_max_height": _integer(
                    _nested(data, "materials", "texture_max_height")
                ),
                "texture_resolution_counts": deepcopy(
                    _nested(data, "materials", "texture_resolution_counts", default={})
                )
                if isinstance(
                    _nested(data, "materials", "texture_resolution_counts", default={}),
                    dict,
                )
                else {},
                "native_node_count": _integer(
                    _nested(data, "native", "gltf", "node_count")
                ),
                "skeleton_element_count": _integer(skeleton.get("joint_count")),
                "skeleton_element_semantic": "joints",
                "skin_count": _integer(skin.get("skin_count")),
                "has_skin": _boolean(_first(skin.get("applied"), skin.get("has_skin"))),
                "max_influences": _integer(skin.get("max_influences")),
                "animation_count": _integer(
                    _first(animation.get("effective_clip_count"), animation.get("animation_count"))
                ),
                "raw_animation_count": _integer(
                    _first(animation.get("raw_animation_count"), animation.get("animation_count"))
                ),
                "technical_animation_count": _integer(animation.get("technical_clip_count")),
                "duplicate_animation_count": _integer(animation.get("duplicate_clip_count")),
                "clip_names": list(animation.get("clip_names", []))
                if isinstance(animation.get("clip_names"), list)
                else [],
                "effective_clip_names": list(animation.get("effective_clip_names", []))
                if isinstance(animation.get("effective_clip_names"), list)
                else [],
                "animation_channel_count": _integer(animation.get("channel_count")),
                "animation_sampler_count": _integer(animation.get("sampler_count")),
                "animation_duration_seconds": animation.get("duration_seconds"),
                "draco": _boolean(_nested(data, "native", "gltf", "draco_compressed")),
                "meshopt": _boolean(_nested(data, "native", "gltf", "meshopt_compressed")),
                "diagnostic_count": len(diagnostics) if isinstance(diagnostics, list) else _integer(diagnostics),
                "topology_signature": _nested(
                    data, "geometry", "signatures", "decoded_topology_sha256"
                ),
                "topology_signature_kind": "decoded_gltf_topology_sha256",
                "topology_signature_domain": "adapter-local-decoded-topology",
                "coordinate_system": spatial.get("coordinate_system"),
                "bounds": _first(spatial.get("position_bounds"), geometry.get("bounds")),
                "position_bounds": _first(spatial.get("position_bounds"), geometry.get("position_bounds")),
                "node_transformed_bounds": _first(
                    spatial.get("node_transformed_bounds"), geometry.get("bounds")
                ),
                "spatial_details": spatial or None,
            }
        )
        return result

    # Best-effort support for future or partially normalized Unified3D analyses.
    result.update(
        {
            "file": _first(_nested(data, "asset", "path"), _nested(data, "file", "path")),
            "mesh_count": _integer(geometry.get("mesh_count")),
            "vertex_count": _integer(
                _first(geometry.get("render_vertex_count"), geometry.get("control_point_count"))
            ),
            "triangle_count": _integer(geometry.get("triangle_count")),
            "uv_set_count": _integer(geometry.get("uv_set_count")),
            "material_count": _integer(_nested(data, "materials", "material_count")),
        }
    )
    return result


def _normalize_summary(text: str) -> dict[str, Any]:
    header, fields = _summary_fields(text)
    folded_header = header.casefold()
    if "fbx" in folded_header:
        kind = "FBX"
    elif "glb" in folded_header or "gltf" in folded_header:
        kind = "GLB"
    else:
        kind = "Unknown"

    result = _base_normalized(kind, "summary")
    get = lambda key: fields.get(key.casefold())

    result.update(
        {
            "file": get("File"),
            "mesh_count": _integer(get("Meshes")),
            "triangle_count": _integer(
                _first(get("Triangles"), get("Triangles (fan-equivalent)"))
            ),
            "uv_set_count": _integer(get("UV Sets")),
            "material_count": _integer(get("Materials")),
            "file_size_bytes": _integer(get("File Size Bytes")),
            "skin_count": _integer(get("Skins")),
            "animation_count": _integer(get("Animations")),
            "raw_animation_count": _integer(
                _first(get("Raw Animation Stacks"), get("Raw Animation Clips"), get("Animations"))
            ),
            "technical_animation_count": _integer(
                _first(get("Technical Animation Stacks"), get("Technical Animation Clips"))
            ),
            "duplicate_animation_count": _integer(
                _first(get("Duplicate Animation Stacks"), get("Duplicate Animation Clips"))
            ),
            "effective_clip_names": [
                name.strip() for name in str(get("Effective Clip Names") or "").split(",") if name.strip()
            ],
        }
    )

    if kind == "FBX":
        result.update(
            {
                "version": get("FBX Version"),
                "vertex_count": _integer(get("Control Points")),
                "vertex_semantic": "control points",
                "polygon_vertex_count": _integer(get("Polygon Vertices")),
                "polygon_count": _integer(get("Polygons")),
                "texture_count": None,
                "skeleton_element_count": _integer(get("Skeleton Bones")),
                "skeleton_element_semantic": "bones",
                "has_skin": _boolean(get("Has Skin")),
                "max_influences": _integer(get("Max Influences / Control Point")),
                "topology_signature": get("Topology Signature"),
                "topology_signature_kind": "fbx_adapter_topology",
                "topology_signature_domain": "adapter-local-decoded-topology",
            }
        )
    elif kind == "GLB":
        result.update(
            {
                "version": get("glTF Version"),
                "generator": get("Generator"),
                "mesh_instance_count": _integer(get("Mesh Instances")),
                "primitive_count": _integer(get("Primitives")),
                "vertex_count": _integer(get("Render Vertices")),
                "vertex_semantic": "render vertices",
                "unique_position_tuple_count": _integer(
                    get("Unique POSITION Tuples (diagnostic)")
                ),
                "index_count": _integer(get("Indices")),
                "degenerate_triangle_count": _integer(get("Degenerate Triangles")),
                "texture_count": _integer(get("Textures")),
                "image_count": _integer(get("Texture Images")),
                "texture_encoded_bytes": _integer(get("Texture Encoded Bytes")),
                "skeleton_element_count": _integer(get("Joints")),
                "skeleton_element_semantic": "joints",
                "has_skin": _boolean(_first(get("Has Applied Skin"), get("Has Skin"))),
                "max_influences": _integer(get("Max Influences / Render Vertex")),
                "draco": _boolean(get("Draco")),
                "meshopt": _boolean(get("Meshopt")),
                "diagnostic_count": _integer(get("Diagnostics")),
                "topology_signature": get("Decoded Topology Signature"),
                "topology_signature_kind": "decoded_gltf_topology_sha256",
                "topology_signature_domain": "adapter-local-decoded-topology",
            }
        )

    if kind == "Unknown" and not fields:
        raise ValueError(
            "The input is neither a supported analysis JSON nor an FBX/GLB summary"
        )
    return result


def normalize_analysis(value: str | dict[str, Any]) -> dict[str, Any]:
    if isinstance(value, dict):
        return _normalize_json(deepcopy(value))
    if value is None or not str(value).strip():
        raise ValueError("The analysis input is empty")
    text = _strip_code_fence(str(value))
    try:
        parsed = json.loads(text)
    except json.JSONDecodeError as error:
        if text.lstrip().startswith(("{", "[")):
            raise ValueError(f"Invalid analysis JSON: {error.msg}") from error
        return _normalize_summary(text)

    if not isinstance(parsed, dict):
        raise ValueError("The analysis JSON root must be an object")
    return _normalize_json(parsed)


def _escape_markdown(value: Any) -> str:
    if value is None or value == "":
        return MISSING
    return str(value).replace("|", "\\|").replace("\r", " ").replace("\n", "<br>")


def _number(value: Any) -> str:
    parsed = _integer(value)
    if parsed is None:
        return MISSING
    return f"{parsed:,}".replace(",", "\u202f")


def _yes_no(value: Any) -> str:
    parsed = _boolean(value)
    if parsed is None:
        return MISSING
    return "Oui" if parsed else "Non"


def _numeric_reading(a: int | None, b: int | None) -> str:
    if a is None or b is None:
        return "Donnée non fournie par l’une des analyses"
    if a == b:
        return "Identique"
    if a == 0 or b == 0:
        return "Différent"
    if b > a:
        return f"B = {b / a:.3f} × A"
    return f"A = {a / b:.3f} × B"


def _boolean_reading(a: bool | None, b: bool | None) -> str:
    if a is None or b is None:
        return "Donnée non fournie par l’une des analyses"
    return "Identique" if a == b else "Différent"


def _vertex_value(data: dict[str, Any]) -> str:
    count = _number(data.get("vertex_count"))
    semantic = data.get("vertex_semantic")
    return f"{count} ({semantic})" if semantic and count != MISSING else count


def _skeleton_value(data: dict[str, Any]) -> str:
    count = _number(data.get("skeleton_element_count"))
    semantic = data.get("skeleton_element_semantic")
    return f"{count} ({semantic})" if semantic and count != MISSING else count


def _animation_value(data: dict[str, Any]) -> str:
    effective = data.get("animation_count")
    raw = data.get("raw_animation_count")
    if isinstance(effective, int) and isinstance(raw, int) and effective != raw:
        return f"{_number(effective)} effectif(s) ({_number(raw)} brut(s))"
    return _number(effective)


def _bounds_height_y(bounds: Any) -> float | None:
    if not isinstance(bounds, dict):
        return None
    minimum, maximum = bounds.get("min"), bounds.get("max")
    if not isinstance(minimum, list) or not isinstance(maximum, list):
        return None
    if len(minimum) < 2 or len(maximum) < 2:
        return None
    try:
        return float(maximum[1]) - float(minimum[1])
    except (TypeError, ValueError):
        return None


def _signature_reading(a: dict[str, Any], b: dict[str, Any]) -> str:
    signature_a = a.get("topology_signature")
    signature_b = b.get("topology_signature")
    if not signature_a or not signature_b:
        return "Signature absente d’une analyse"
    if (
        a.get("topology_signature_kind") != b.get("topology_signature_kind")
        or a.get("topology_signature_domain") != b.get("topology_signature_domain")
    ):
        return "Non comparables : algorithmes d’adaptateur différents"
    return "Identique" if signature_a == signature_b else "Différente"


def _ratio_score(a: int | float | None, b: int | float | None) -> float | None:
    if not isinstance(a, (int, float)) or isinstance(a, bool):
        return None
    if not isinstance(b, (int, float)) or isinstance(b, bool):
        return None
    if a < 0 or b < 0:
        return None
    if a == b:
        return 1.0
    if a == 0 or b == 0:
        return 0.0
    return min(float(a), float(b)) / max(float(a), float(b))


def _known_coordinate_system(value: Any) -> bool:
    return isinstance(value, dict) and all(
        value.get(key) is not None
        for key in ("handedness", "up_axis", "forward_axis", "meters_per_unit")
    )


def _bounds_similarity(a: Any, b: Any) -> float | None:
    if not isinstance(a, dict) or not isinstance(b, dict):
        return None
    min_a, max_a, min_b, max_b = a.get("min"), a.get("max"), b.get("min"), b.get("max")
    if not all(isinstance(value, list) and len(value) == 3 for value in (min_a, max_a, min_b, max_b)):
        return None
    try:
        extents_a = [abs(float(max_a[index]) - float(min_a[index])) for index in range(3)]
        extents_b = [abs(float(max_b[index]) - float(min_b[index])) for index in range(3)]
    except (TypeError, ValueError):
        return None
    axis_scores: list[float] = []
    for extent_a, extent_b in zip(extents_a, extents_b):
        score = _ratio_score(extent_a, extent_b)
        if score is None:
            return None
        axis_scores.append(score)
    return sum(axis_scores) / len(axis_scores)


_AXIS_VECTORS: dict[str, tuple[float, float, float]] = {
    "X": (1.0, 0.0, 0.0),
    "-X": (-1.0, 0.0, 0.0),
    "Y": (0.0, 1.0, 0.0),
    "-Y": (0.0, -1.0, 0.0),
    "Z": (0.0, 0.0, 1.0),
    "-Z": (0.0, 0.0, -1.0),
}


def _dot(
    a: tuple[float, float, float],
    b: tuple[float, float, float],
) -> float:
    return sum(left * right for left, right in zip(a, b))


def _cross(
    a: tuple[float, float, float],
    b: tuple[float, float, float],
) -> tuple[float, float, float]:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def _canonical_bounds(
    coordinates: Any,
    bounds: Any,
) -> dict[str, list[float] | float] | None:
    if not _known_coordinate_system(coordinates) or not isinstance(bounds, dict):
        return None
    minimum, maximum = bounds.get("min"), bounds.get("max")
    if not all(
        isinstance(value, list) and len(value) == 3
        for value in (minimum, maximum)
    ):
        return None
    up = _AXIS_VECTORS.get(coordinates["up_axis"])
    forward = _AXIS_VECTORS.get(coordinates["forward_axis"])
    if up is None or forward is None or abs(_dot(up, forward)) > 0.5:
        return None
    right = (
        _cross(forward, up)
        if coordinates["handedness"] == "right"
        else _cross(up, forward)
    )
    scale = float(coordinates["meters_per_unit"])
    canonical_corners: list[tuple[float, float, float]] = []
    for corner in range(8):
        source = tuple(
            float(maximum[axis] if corner & (1 << axis) else minimum[axis])
            for axis in range(3)
        )
        canonical_corners.append(
            (
                _dot(source, right) * scale,
                _dot(source, up) * scale,
                -_dot(source, forward) * scale,
            )
        )
    canonical_min = [min(corner[axis] for corner in canonical_corners) for axis in range(3)]
    canonical_max = [max(corner[axis] for corner in canonical_corners) for axis in range(3)]
    extents = [canonical_max[axis] - canonical_min[axis] for axis in range(3)]
    center = [(canonical_max[axis] + canonical_min[axis]) * 0.5 for axis in range(3)]
    return {
        "min": canonical_min,
        "max": canonical_max,
        "extents": extents,
        "center": center,
        "diagonal": math.sqrt(sum(extent * extent for extent in extents)),
    }


def _spatial_comparison(a: dict[str, Any], b: dict[str, Any]) -> dict[str, float] | None:
    canonical_a = _canonical_bounds(a.get("coordinate_system"), a.get("bounds"))
    canonical_b = _canonical_bounds(b.get("coordinate_system"), b.get("bounds"))
    if canonical_a is None or canonical_b is None:
        return None
    extents_a = canonical_a["extents"]
    extents_b = canonical_b["extents"]
    center_a = canonical_a["center"]
    center_b = canonical_b["center"]
    assert isinstance(extents_a, list) and isinstance(extents_b, list)
    assert isinstance(center_a, list) and isinstance(center_b, list)

    extent_similarity = sum(
        _ratio_score(extent_a, extent_b) or 0.0
        for extent_a, extent_b in zip(extents_a, extents_b)
    ) / 3.0
    center_distance = math.sqrt(
        sum((left - right) ** 2 for left, right in zip(center_a, center_b))
    )
    reference_diagonal = max(
        float(canonical_a["diagonal"]),
        float(canonical_b["diagonal"]),
    )
    normalized_center_distance = (
        center_distance / reference_diagonal
        if reference_diagonal > 0.0
        else 0.0 if center_distance == 0.0 else math.inf
    )
    center_alignment = (
        1.0 / (1.0 + normalized_center_distance)
        if math.isfinite(normalized_center_distance)
        else 0.0
    )

    minimum_a = canonical_a["min"]
    maximum_a = canonical_a["max"]
    minimum_b = canonical_b["min"]
    maximum_b = canonical_b["max"]
    assert all(
        isinstance(value, list)
        for value in (minimum_a, maximum_a, minimum_b, maximum_b)
    )
    intersection_volume = math.prod(
        max(0.0, min(maximum_a[axis], maximum_b[axis]) - max(minimum_a[axis], minimum_b[axis]))
        for axis in range(3)
    )
    volume_a = math.prod(extents_a)
    volume_b = math.prod(extents_b)
    union_volume = volume_a + volume_b - intersection_volume
    result = {
        "extent_similarity": extent_similarity,
        "center_distance_m": center_distance,
        "normalized_center_distance": normalized_center_distance,
        "center_alignment": center_alignment,
    }
    if union_volume > 0.0:
        bounds_iou = intersection_volume / union_volume
        result["bounds_iou"] = bounds_iou
        result["score"] = (
            0.5 * extent_similarity + 0.3 * center_alignment + 0.2 * bounds_iou
        )
    else:
        result["score"] = 0.625 * extent_similarity + 0.375 * center_alignment
    return result


def _compatibility_model(
    a: dict[str, Any],
    b: dict[str, Any],
    *,
    donor_detected: bool,
) -> dict[str, Any]:
    """Evaluate deterministic compatibility levels 0–7.

    The levels intentionally report unavailable evidence instead of filling
    gaps with assumptions. Level 7 normalizes metadata bounds into a canonical
    metric frame; it does not claim vertex correspondence.
    """

    levels: list[dict[str, Any]] = []

    def add(
        level: int,
        name: str,
        status: str,
        score: float | None,
        evidence: dict[str, Any],
    ) -> None:
        levels.append(
            {
                "level": level,
                "name": name,
                "status": status,
                "score": score,
                "evidence": evidence,
            }
        )

    add(
        0,
        "analysis_records",
        "match",
        1.0,
        {"a_kind": a.get("kind"), "b_kind": b.get("kind"), "normalized": True},
    )

    coordinates_a = a.get("coordinate_system")
    coordinates_b = b.get("coordinate_system")
    if _known_coordinate_system(coordinates_a) and _known_coordinate_system(coordinates_b):
        coordinates_match = coordinates_a == coordinates_b
        add(
            1,
            "coordinate_system",
            "match" if coordinates_match else "different",
            1.0 if coordinates_match else 0.0,
            {"a": deepcopy(coordinates_a), "b": deepcopy(coordinates_b)},
        )
    else:
        add(
            1,
            "coordinate_system",
            "not_available",
            None,
            {"reason": "axes_or_units_not_computed"},
        )

    bounds_score = _bounds_similarity(a.get("bounds"), b.get("bounds"))
    if bounds_score is None:
        add(2, "bounds", "not_available", None, {"reason": "bounds_not_computed"})
    else:
        add(
            2,
            "bounds",
            "match" if bounds_score >= 0.99 else "different",
            bounds_score,
            {"extent_similarity": bounds_score},
        )

    mesh_score = _ratio_score(a.get("mesh_count"), b.get("mesh_count"))
    if mesh_score is None:
        add(3, "mesh_structure", "not_available", None, {"reason": "mesh_count_not_computed"})
    else:
        add(
            3,
            "mesh_structure",
            "match" if mesh_score == 1.0 else "different",
            mesh_score,
            {"a_mesh_count": a.get("mesh_count"), "b_mesh_count": b.get("mesh_count")},
        )

    triangle_score = _ratio_score(a.get("triangle_count"), b.get("triangle_count"))
    if triangle_score is None:
        add(4, "triangle_statistics", "not_available", None, {"reason": "triangle_count_not_computed"})
    else:
        add(
            4,
            "triangle_statistics",
            "match" if triangle_score == 1.0 else "different",
            triangle_score,
            {
                "a_triangle_count": a.get("triangle_count"),
                "b_triangle_count": b.get("triangle_count"),
                "density_similarity": triangle_score,
            },
        )

    semantic_a = a.get("vertex_semantic")
    semantic_b = b.get("vertex_semantic")
    if not semantic_a or not semantic_b:
        add(5, "vertex_statistics", "not_available", None, {"reason": "vertex_count_not_computed"})
    elif semantic_a != semantic_b:
        add(
            5,
            "vertex_statistics",
            "not_comparable",
            None,
            {"a_semantic": semantic_a, "b_semantic": semantic_b},
        )
    else:
        vertex_score = _ratio_score(a.get("vertex_count"), b.get("vertex_count"))
        add(
            5,
            "vertex_statistics",
            "match" if vertex_score == 1.0 else "different" if vertex_score is not None else "not_available",
            vertex_score,
            {"semantic": semantic_a, "a_count": a.get("vertex_count"), "b_count": b.get("vertex_count")},
        )

    signature_a = a.get("topology_signature")
    signature_b = b.get("topology_signature")
    algorithms_match = (
        bool(signature_a and signature_b)
        and a.get("topology_signature_kind") == b.get("topology_signature_kind")
        and a.get("topology_signature_domain") == b.get("topology_signature_domain")
    )
    if not signature_a or not signature_b:
        add(6, "topology_signature", "not_available", None, {"reason": "signature_not_computed"})
    elif not algorithms_match:
        add(
            6,
            "topology_signature",
            "not_comparable",
            None,
            {"a_algorithm": a.get("topology_signature_kind"), "b_algorithm": b.get("topology_signature_kind")},
        )
    else:
        signatures_match = signature_a == signature_b
        add(
            6,
            "topology_signature",
            "match" if signatures_match else "different",
            1.0 if signatures_match else 0.0,
            {"algorithm": a.get("topology_signature_kind"), "same_digest": signatures_match},
        )

    spatial = _spatial_comparison(a, b)
    if spatial is None:
        coordinates_available = _known_coordinate_system(coordinates_a) and _known_coordinate_system(coordinates_b)
        bounds_available = isinstance(a.get("bounds"), dict) and isinstance(b.get("bounds"), dict)
        reason = (
            "axes_or_units_not_computed"
            if not coordinates_available
            else "bounds_not_computed"
            if not bounds_available
            else "invalid_axis_basis"
        )
        add(7, "spatial_alignment", "not_available", None, {"reason": reason})
    else:
        spatial_match = (
            spatial["extent_similarity"] >= 0.99
            and spatial["normalized_center_distance"] <= 0.01
        )
        evidence: dict[str, Any] = {
            "bounds_normalized": True,
            "canonical_space": "right_handed_y_up_negative_z_forward_meters",
            "center_alignment": spatial["center_alignment"],
            "center_distance_m": spatial["center_distance_m"],
            "extent_similarity": spatial["extent_similarity"],
            "normalized_center_distance": spatial["normalized_center_distance"],
            "units_normalized": True,
        }
        if "bounds_iou" in spatial:
            evidence["bounds_iou"] = spatial["bounds_iou"]
        add(
            7,
            "spatial_alignment",
            "match" if spatial_match else "different",
            spatial["score"],
            evidence,
        )

    measured_scores = [
        level["score"]
        for level in levels[1:]
        if isinstance(level["score"], float)
    ]
    score = sum(measured_scores) / len(measured_scores) if measured_scores else None
    coverage = len(measured_scores) / (len(levels) - 1)
    topology_level = levels[6]
    if topology_level["status"] == "match":
        classification: str | None = "EXACT_TOPOLOGY_MATCH"
    elif levels[7]["status"] == "match":
        classification = (
            "SPATIAL_SKIN_TRANSFER_REQUIRED" if donor_detected else "SPATIAL_MATCH"
        )
    elif donor_detected and triangle_score is not None and triangle_score < 1.0:
        classification = "ADVANCED_TRANSFER_REQUIRED"
    else:
        classification = None
    unresolved = next(
        (
            level["level"]
            for level in levels
            if level["status"] in {"not_available", "not_comparable"}
        ),
        None,
    )
    return {
        "classification": classification,
        "score": score,
        "coverage": coverage,
        "levels": levels,
        "recommended_next_level": unresolved if unresolved is not None else 8,
    }


def _comparison_model(a: dict[str, Any], b: dict[str, Any]) -> dict[str, Any]:
    triangles_a = a.get("triangle_count")
    triangles_b = b.get("triangle_count")
    triangle_ratio_b_over_a = None
    if isinstance(triangles_a, int) and isinstance(triangles_b, int) and triangles_a > 0:
        triangle_ratio_b_over_a = triangles_b / triangles_a

    donor_side = None
    target_side = None
    for side, current, other_side, other in (("a", a, "b", b), ("b", b, "a", a)):
        current_rigged = current.get("has_skin") is True or (
            isinstance(current.get("skeleton_element_count"), int)
            and current["skeleton_element_count"] > 0
        )
        other_unrigged = other.get("has_skin") is False and (
            other.get("skeleton_element_count") in {None, 0}
        )
        denser_target = (
            isinstance(current.get("triangle_count"), int)
            and isinstance(other.get("triangle_count"), int)
            and other["triangle_count"] > current["triangle_count"]
        )
        if current_rigged and other_unrigged and denser_target:
            donor_side = side
            target_side = other_side
            break

    different_triangle_counts = (
        isinstance(triangles_a, int)
        and isinstance(triangles_b, int)
        and triangles_a != triangles_b
    )

    donor_detected = donor_side is not None
    if a.get("kind") == "FBX" and b.get("kind") == "GLB":
        fbx, glb = a, b
    elif a.get("kind") == "GLB" and b.get("kind") == "FBX":
        fbx, glb = b, a
    else:
        fbx = glb = None

    topology_candidate: dict[str, Any] = {
        "detected": False,
        "strength": "not_available",
        "evidence": {},
        "caveat": "A canonical cross-format connectivity signature is required for proof.",
    }
    if fbx is not None and glb is not None:
        evidence = {
            "mesh_count_equal": (
                fbx.get("mesh_count") == glb.get("mesh_count")
                if fbx.get("mesh_count") is not None and glb.get("mesh_count") is not None
                else None
            ),
            "triangle_count_equal": (
                fbx.get("triangle_count") == glb.get("triangle_count")
                if fbx.get("triangle_count") is not None and glb.get("triangle_count") is not None
                else None
            ),
            "control_points_equal_unique_positions": (
                fbx.get("vertex_count") == glb.get("unique_position_tuple_count")
                if fbx.get("vertex_count") is not None
                and glb.get("unique_position_tuple_count") is not None
                else None
            ),
            "polygon_vertices_equal_indices": (
                fbx.get("polygon_vertex_count") == glb.get("index_count")
                if fbx.get("polygon_vertex_count") is not None and glb.get("index_count") is not None
                else None
            ),
        }
        core_matches = (
            evidence["triangle_count_equal"] is True
            and evidence["control_points_equal_unique_positions"] is True
        )
        strong = core_matches and evidence["polygon_vertices_equal_indices"] is True
        topology_candidate = {
            "detected": core_matches,
            "strength": "strong_candidate_not_proven" if strong else "candidate_not_proven" if core_matches else "not_detected",
            "evidence": evidence,
            "caveat": "Matching counts are strong evidence, not proof of identical vertex order or connectivity.",
        }
    topology_signatures_comparable = (
        bool(a.get("topology_signature") and b.get("topology_signature"))
        and a.get("topology_signature_kind") == b.get("topology_signature_kind")
        and a.get("topology_signature_domain") == b.get("topology_signature_domain")
    )
    glb_pair = a.get("kind") == "GLB" and b.get("kind") == "GLB"
    triangle_a = a.get("triangle_count")
    triangle_b = b.get("triangle_count")
    if (
        glb_pair
        and isinstance(triangle_a, int)
        and isinstance(triangle_b, int)
        and triangle_a > 0
        and triangle_b > 0
    ):
        denser_input = "a" if triangle_a > triangle_b else "b" if triangle_b > triangle_a else None
        retained_triangle_percent = 100 * min(triangle_a, triangle_b) / max(triangle_a, triangle_b)
    else:
        denser_input = None
        retained_triangle_percent = None
    texture_bytes_a = a.get("texture_encoded_bytes")
    texture_bytes_b = b.get("texture_encoded_bytes")
    texture_byte_ratio_b_over_a = (
        texture_bytes_b / texture_bytes_a
        if glb_pair
        and isinstance(texture_bytes_a, int)
        and texture_bytes_a > 0
        and isinstance(texture_bytes_b, int)
        else None
    )
    return {
        "same_mesh_count": (
            a.get("mesh_count") == b.get("mesh_count")
            if a.get("mesh_count") is not None and b.get("mesh_count") is not None
            else None
        ),
        "triangle_ratio_b_over_a": triangle_ratio_b_over_a,
        "index_transfer_ruled_out_by_triangle_count": different_triangle_counts,
        "rig_donor_geometry_target_pattern": {
            "detected": donor_detected,
            "donor": donor_side,
            "target": target_side,
            "recommended_next_check": "spatial_alignment" if donor_side else None,
        },
        "topology_signatures_comparable": topology_signatures_comparable,
        "topology_signature_match": (
            a.get("topology_signature") == b.get("topology_signature")
            if topology_signatures_comparable
            else None
        ),
        "glb_pair": {
            "detected": glb_pair,
            "denser_input": denser_input,
            "retained_triangle_percent": retained_triangle_percent,
            "texture_inventory": {
                "available": bool(
                    glb_pair
                    and a.get("texture_resolution_counts")
                    and b.get("texture_resolution_counts")
                ),
                "a": {
                    "image_count": a.get("image_count"),
                    "encoded_bytes": texture_bytes_a,
                    "resolution_counts": deepcopy(a.get("texture_resolution_counts", {})),
                },
                "b": {
                    "image_count": b.get("image_count"),
                    "encoded_bytes": texture_bytes_b,
                    "resolution_counts": deepcopy(b.get("texture_resolution_counts", {})),
                },
                "encoded_byte_ratio_b_over_a": texture_byte_ratio_b_over_a,
            },
        },
        "cross_format_topology_candidate": topology_candidate,
        "compatibility": _compatibility_model(
            a,
            b,
            donor_detected=donor_detected,
        ),
    }


def _make_markdown(a: dict[str, Any], b: dict[str, Any], comparison: dict[str, Any]) -> str:
    rows: list[tuple[str, str, str, str, str]] = []

    def add(category: str, metric: str, value_a: Any, value_b: Any, reading: str) -> None:
        rows.append(
            (
                category,
                metric,
                _escape_markdown(value_a),
                _escape_markdown(value_b),
                _escape_markdown(reading),
            )
        )

    add("Source", "Type détecté", a.get("kind"), b.get("kind"), "—")
    add("Source", "Entrée", a.get("source_format"), b.get("source_format"), "—")
    add("Source", "Fichier", a.get("file"), b.get("file"), "—")
    add("Source", "Version", a.get("version"), b.get("version"), "—")
    add("Source", "Générateur / backend", a.get("generator"), b.get("generator"), "—")
    add(
        "Géométrie",
        "Maillages",
        _number(a.get("mesh_count")),
        _number(b.get("mesh_count")),
        _numeric_reading(a.get("mesh_count"), b.get("mesh_count")),
    )
    add(
        "Géométrie",
        "Instances de maillage",
        _number(a.get("mesh_instance_count")),
        _number(b.get("mesh_instance_count")),
        _numeric_reading(a.get("mesh_instance_count"), b.get("mesh_instance_count")),
    )
    add(
        "Géométrie",
        "Primitives",
        _number(a.get("primitive_count")),
        _number(b.get("primitive_count")),
        _numeric_reading(a.get("primitive_count"), b.get("primitive_count")),
    )
    topology_candidate = comparison.get("cross_format_topology_candidate", {})
    vertex_reading = (
        "Candidat de correspondance géométrique inter-format ; ordre des sommets non encore prouvé"
        if topology_candidate.get("detected") is True
        else "Mesures différentes : aucune équivalence directe"
        if a.get("vertex_semantic") != b.get("vertex_semantic")
        else _numeric_reading(a.get("vertex_count"), b.get("vertex_count"))
    )
    add("Géométrie", "Sommets analysés", _vertex_value(a), _vertex_value(b), vertex_reading)
    add(
        "Géométrie",
        "Positions uniques (diagnostic)",
        _number(a.get("unique_position_tuple_count")),
        _number(b.get("unique_position_tuple_count")),
        _numeric_reading(
            a.get("unique_position_tuple_count"), b.get("unique_position_tuple_count")
        ),
    )
    add(
        "Topologie",
        "Correspondance inter-format candidate",
        topology_candidate.get("strength", MISSING),
        topology_candidate.get("strength", MISSING),
        topology_candidate.get("caveat", "Données insuffisantes"),
    )
    add(
        "Géométrie",
        "Triangles",
        _number(a.get("triangle_count")),
        _number(b.get("triangle_count")),
        _numeric_reading(a.get("triangle_count"), b.get("triangle_count")),
    )
    add(
        "Géométrie",
        "Triangles dégénérés",
        _number(a.get("degenerate_triangle_count")),
        _number(b.get("degenerate_triangle_count")),
        _numeric_reading(
            a.get("degenerate_triangle_count"), b.get("degenerate_triangle_count")
        ),
    )
    uv_reading = (
        "Comptage dépendant de l’adaptateur"
        if a.get("kind") != b.get("kind")
        else _numeric_reading(a.get("uv_set_count"), b.get("uv_set_count"))
    )
    add(
        "Apparence",
        "Ensembles UV",
        _number(a.get("uv_set_count")),
        _number(b.get("uv_set_count")),
        uv_reading,
    )
    add(
        "Apparence",
        "Matériaux",
        _number(a.get("material_count")),
        _number(b.get("material_count")),
        _numeric_reading(a.get("material_count"), b.get("material_count")),
    )
    add(
        "Apparence",
        "Textures",
        _number(a.get("texture_count")),
        _number(b.get("texture_count")),
        _numeric_reading(a.get("texture_count"), b.get("texture_count")),
    )
    skeleton_reading = (
        "Mesures squelettiques adaptées au format"
        if a.get("skeleton_element_semantic") != b.get("skeleton_element_semantic")
        else _numeric_reading(
            a.get("skeleton_element_count"), b.get("skeleton_element_count")
        )
    )
    add("Rig", "Éléments du squelette", _skeleton_value(a), _skeleton_value(b), skeleton_reading)
    add(
        "Rig",
        "Skins",
        _number(a.get("skin_count")),
        _number(b.get("skin_count")),
        _numeric_reading(a.get("skin_count"), b.get("skin_count")),
    )
    add(
        "Rig",
        "Skin appliqué",
        _yes_no(a.get("has_skin")),
        _yes_no(b.get("has_skin")),
        _boolean_reading(a.get("has_skin"), b.get("has_skin")),
    )
    add(
        "Rig",
        "Influences maximales",
        _number(a.get("max_influences")),
        _number(b.get("max_influences")),
        _numeric_reading(a.get("max_influences"), b.get("max_influences")),
    )
    add(
        "Animation",
        "Clips effectifs / entrées brutes",
        _animation_value(a),
        _animation_value(b),
        _numeric_reading(a.get("animation_count"), b.get("animation_count")),
    )
    add(
        "Animation",
        "Noms des clips effectifs",
        ", ".join(a.get("effective_clip_names", [])) or MISSING,
        ", ".join(b.get("effective_clip_names", [])) or MISSING,
        (
            "Identique"
            if a.get("effective_clip_names")
            and a.get("effective_clip_names") == b.get("effective_clip_names")
            else "À comparer"
        ),
    )
    add(
        "Espace",
        "Hauteur Y des positions",
        _number(_bounds_height_y(a.get("position_bounds"))),
        _number(_bounds_height_y(b.get("position_bounds"))),
        "Normaliser les axes et unités avant toute décision d’alignement",
    )
    add("Conteneur", "Draco", _yes_no(a.get("draco")), _yes_no(b.get("draco")), _boolean_reading(a.get("draco"), b.get("draco")))
    add("Conteneur", "Meshopt", _yes_no(a.get("meshopt")), _yes_no(b.get("meshopt")), _boolean_reading(a.get("meshopt"), b.get("meshopt")))
    add(
        "Validation",
        "Diagnostics",
        _number(a.get("diagnostic_count")),
        _number(b.get("diagnostic_count")),
        _numeric_reading(a.get("diagnostic_count"), b.get("diagnostic_count")),
    )
    add(
        "Topologie",
        "Signature",
        a.get("topology_signature"),
        b.get("topology_signature"),
        _signature_reading(a, b),
    )

    lines = [
        "# Unified3D — Comparaison des analyses",
        "",
        "| Catégorie | Mesure | Analyse A | Analyse B | Lecture |",
        "|---|---|---:|---:|---|",
    ]
    lines.extend(f"| {category} | {metric} | {value_a} | {value_b} | {reading} |" for category, metric, value_a, value_b, reading in rows)

    lines.extend(["", "## Conclusion automatique", ""])
    conclusions: list[str] = []
    if comparison.get("same_mesh_count") is True:
        conclusions.append("Les deux analyses déclarent le même nombre de maillages.")
    elif comparison.get("same_mesh_count") is False:
        conclusions.append("Les nombres de maillages sont différents.")

    ratio = comparison.get("triangle_ratio_b_over_a")
    if isinstance(ratio, float):
        conclusions.append(f"Le rapport de triangles B/A est de **{ratio:.4f}×**.")

    if comparison.get("index_transfer_ruled_out_by_triangle_count"):
        conclusions.append(
            "Les nombres de triangles différents excluent un transfert direct par index ; "
            "une correspondance spatiale doit être évaluée."
        )

    pattern = comparison.get("rig_donor_geometry_target_pattern", {})
    if pattern.get("detected"):
        donor = str(pattern.get("donor", "")).upper()
        target = str(pattern.get("target", "")).upper()
        conclusions.append(
            f"Le profil donneur/cible est détecté : **{donor}** fournit le rig et "
            f"**{target}** porte la géométrie plus dense. Vérifier ensuite l’alignement spatial."
        )

    if not comparison.get("topology_signatures_comparable"):
        conclusions.append(
            "Les signatures de topologie ne sont pas comparées lorsqu’elles proviennent "
            "d’algorithmes d’adaptateur différents."
        )

    candidate = comparison.get("cross_format_topology_candidate", {})
    if candidate.get("detected"):
        conclusions.append(
            "Les compteurs géométriques forment un **candidat fort de correspondance "
            "FBX/GLB**, mais une signature canonique commune doit encore confirmer "
            "l’ordre et la connectivité."
        )

    if not conclusions:
        conclusions.append("Aucune conclusion automatique fiable avec les champs disponibles.")
    lines.extend(f"- {conclusion}" for conclusion in conclusions)
    return "\n".join(lines)


def _french_decimal(value: float, digits: int = 2) -> str:
    return f"{value:.{digits}f}".replace(".", ",")


def _glb_compression_label(glb: dict[str, Any]) -> str:
    codecs: list[str] = []
    if glb.get("draco") is True:
        codecs.append("Draco")
    if glb.get("meshopt") is True:
        codecs.append("Meshopt")
    if codecs:
        return " + ".join(codecs)
    if glb.get("draco") is False and glb.get("meshopt") is False:
        return "aucune"
    return MISSING


def _format_bytes(value: Any) -> str:
    parsed = _integer(value)
    if parsed is None:
        return MISSING
    return f"{parsed / (1024 * 1024):.2f} Mio".replace(".", ",")


def _resolution_summary(glb: dict[str, Any]) -> str:
    counts = glb.get("texture_resolution_counts")
    if not isinstance(counts, dict) or not counts:
        return MISSING

    def area(item: tuple[str, Any]) -> int:
        match = re.fullmatch(r"(\d+)x(\d+)", str(item[0]))
        return int(match.group(1)) * int(match.group(2)) if match else -1

    return ", ".join(
        f"{resolution} × {_number(count)}"
        for resolution, count in sorted(counts.items(), key=area, reverse=True)
    )


def _short_file_name(value: Any) -> str:
    text = str(value or "").replace("\\", "/")
    return text.rsplit("/", 1)[-1] or MISSING


def _fbx_numeric_reading(a: Any, b: Any) -> str:
    """Describe a same-domain FBX count without implying topology equivalence."""

    left = _integer(a)
    right = _integer(b)
    if left is None or right is None:
        return "Donnée non fournie par l’une des analyses"
    if left == right:
        return "Identique"

    difference = right - left
    if left == 0:
        return f"Écart B − A : {difference:+d}"
    percentage = 100 * difference / left
    return (
        f"Écart B − A : {difference:+d} "
        f"({_french_decimal(percentage, 4)} %)"
    )


def _fbx_coordinate_label(data: dict[str, Any]) -> str:
    coordinate_system = data.get("coordinate_system")
    if not isinstance(coordinate_system, dict):
        return MISSING

    parts: list[str] = []
    handedness = coordinate_system.get("handedness")
    up_axis = coordinate_system.get("up_axis")
    forward_axis = coordinate_system.get("forward_axis")
    meters_per_unit = coordinate_system.get("meters_per_unit")
    if handedness:
        parts.append("main droite" if str(handedness).casefold() == "right" else str(handedness))
    if up_axis:
        parts.append(f"{up_axis} haut")
    if forward_axis:
        parts.append(f"{forward_axis} avant")
    if isinstance(meters_per_unit, (int, float)):
        parts.append(f"{_french_decimal(float(meters_per_unit), 4)} m/unité")
    return " ; ".join(parts) if parts else MISSING


def _bounds_extent_for_axis(bounds: Any, axis: Any) -> float | None:
    if not isinstance(bounds, dict):
        return None
    minimum, maximum = bounds.get("min"), bounds.get("max")
    if not isinstance(minimum, list) or not isinstance(maximum, list):
        return None
    axis_index = {"X": 0, "Y": 1, "Z": 2}.get(str(axis or "").upper())
    if axis_index is None or len(minimum) <= axis_index or len(maximum) <= axis_index:
        return None
    low, high = minimum[axis_index], maximum[axis_index]
    if not isinstance(low, (int, float)) or not isinstance(high, (int, float)):
        return None
    return float(high - low)


def _fbx_vertical_extent(data: dict[str, Any]) -> tuple[float | None, float | None]:
    up_axis = _nested(data, "coordinate_system", "up_axis")
    source_height = _bounds_extent_for_axis(data.get("node_transformed_bounds"), up_axis)
    if source_height is None:
        source_height = _bounds_height_y(data.get("position_bounds"))
    meters_per_unit = _nested(data, "coordinate_system", "meters_per_unit")
    meter_height = (
        source_height * float(meters_per_unit)
        if source_height is not None and isinstance(meters_per_unit, (int, float))
        else None
    )
    return source_height, meter_height


def _fbx_height_value(data: dict[str, Any]) -> str:
    source_height, meter_height = _fbx_vertical_extent(data)

    if source_height is not None:
        if meter_height is not None:
            return (
                f"{_french_decimal(source_height, 4)} unités source = "
                f"{_french_decimal(meter_height, 4)} m"
            )
        return f"{_french_decimal(source_height, 4)} unités source"
    return MISSING


def _make_fbx_pair_markdown(
    a: dict[str, Any], b: dict[str, Any], comparison: dict[str, Any]
) -> str:
    """Build a same-adapter FBX comparison with topology-safe conclusions."""

    rows: list[tuple[str, str, str, str]] = []

    def add(property_name: str, value_a: Any, value_b: Any, reading: str) -> None:
        rows.append(
            (
                _escape_markdown(property_name),
                _escape_markdown(value_a),
                _escape_markdown(value_b),
                _escape_markdown(reading),
            )
        )

    add(
        "Fichier",
        _short_file_name(a.get("file")),
        _short_file_name(b.get("file")),
        "Deux entrées FBX analysées avec l’adaptateur Autodesk FBX SDK",
    )
    add(
        "Version FBX",
        a.get("version"),
        b.get("version"),
        "Identique" if a.get("version") == b.get("version") else "Versions de conteneur différentes",
    )

    for label, key in (
        ("Maillages", "mesh_count"),
        ("Control points", "vertex_count"),
        ("Polygon vertices", "polygon_vertex_count"),
        ("Polygones", "polygon_count"),
        ("Triangles (fan-equivalent)", "triangle_count"),
        ("Ensembles UV", "uv_set_count"),
        ("Matériaux", "material_count"),
        ("Textures", "texture_count"),
        ("Os", "skeleton_element_count"),
        ("Skins", "skin_count"),
        ("Influences maximales", "max_influences"),
    ):
        add(
            label,
            _number(a.get(key)),
            _number(b.get(key)),
            _fbx_numeric_reading(a.get(key), b.get(key)),
        )

    add(
        "Skin appliqué",
        _yes_no(a.get("has_skin")),
        _yes_no(b.get("has_skin")),
        _boolean_reading(a.get("has_skin"), b.get("has_skin")),
    )

    names_a = a.get("effective_clip_names", [])
    names_b = b.get("effective_clip_names", [])
    if names_a and names_a == names_b:
        animation_reading = "Mêmes clips effectifs : " + ", ".join(names_a)
    else:
        animation_reading = _fbx_numeric_reading(
            a.get("animation_count"), b.get("animation_count")
        )
    add(
        "Animations",
        _animation_value(a),
        _animation_value(b),
        animation_reading,
    )

    coordinate_a = _fbx_coordinate_label(a)
    coordinate_b = _fbx_coordinate_label(b)
    add(
        "Axes / unités",
        coordinate_a,
        coordinate_b,
        "Identiques" if coordinate_a != MISSING and coordinate_a == coordinate_b else "Vérifier l’orientation et l’échelle",
    )

    height_a = _fbx_height_value(a)
    height_b = _fbx_height_value(b)
    _, meter_height_a = _fbx_vertical_extent(a)
    _, meter_height_b = _fbx_vertical_extent(b)
    if (
        meter_height_a is not None
        and meter_height_b is not None
        and meter_height_a > 0
    ):
        height_delta_percent = 100 * (meter_height_b / meter_height_a - 1)
        height_reading = (
            "Identique"
            if math.isclose(meter_height_a, meter_height_b, rel_tol=1e-9, abs_tol=1e-9)
            else f"Écart B/A : {_french_decimal(height_delta_percent, 4)} %"
        )
    else:
        height_reading = "Comparer les bornes et unités avant transfert"
    add(
        "Hauteur selon l’axe vertical",
        height_a,
        height_b,
        height_reading,
    )

    signatures_comparable = comparison.get("topology_signatures_comparable") is True
    signatures_match = comparison.get("topology_signature_match") is True
    if signatures_comparable and signatures_match:
        topology_reading = "Topologie identique selon la signature de l’adaptateur FBX"
    elif signatures_comparable:
        topology_reading = (
            "Topologies différentes ; un transfert direct par index n’est pas garanti"
        )
    else:
        topology_reading = "Signature absente ou non comparable"
    add(
        "Signature topologique",
        a.get("topology_signature"),
        b.get("topology_signature"),
        topology_reading,
    )

    lines = [
        "# Comparaison principale",
        "",
        "**Mode : FBX ↔ FBX**",
        "",
        "| Propriété | FBX A | FBX B | Interprétation |",
        "|---|---:|---:|---|",
    ]
    lines.extend(
        f"| {property_name} | {value_a} | {value_b} | {reading} |"
        for property_name, value_a, value_b, reading in rows
    )

    triangles_a = _integer(a.get("triangle_count"))
    triangles_b = _integer(b.get("triangle_count"))
    meshes_match = (
        a.get("mesh_count") is not None
        and a.get("mesh_count") == b.get("mesh_count")
    )
    close_triangle_counts = (
        triangles_a is not None
        and triangles_b is not None
        and max(triangles_a, triangles_b) > 0
        and abs(triangles_a - triangles_b) / max(triangles_a, triangles_b) <= 0.001
    )

    lines.extend(["", "## Conclusion", ""])
    if meshes_match and close_triangle_counts:
        lines.append(
            "- Les structures et densités sont **très proches en comptage**. "
            "Cela n’établit pas à lui seul une identité topologique."
        )
    if signatures_comparable and signatures_match:
        lines.append(
            "- Les signatures topologiques correspondent : la topologie décodée par "
            "l’adaptateur FBX est identique."
        )
    elif signatures_comparable:
        lines.append(
            "- Les signatures topologiques diffèrent : les deux FBX ne doivent pas être "
            "considérés comme interchangeables par index."
        )
    if a.get("has_skin") is False and b.get("has_skin") is False:
        lines.append("- Les deux fichiers sont dépourvus de skin.")
    elif a.get("has_skin") != b.get("has_skin"):
        lines.append("- Un seul des deux fichiers possède un skin ; vérifier le rôle source/cible.")
    if a.get("animation_count") == b.get("animation_count") == 0:
        lines.append("- Les deux fichiers sont dépourvus d’animation effective.")
    return "\n".join(lines)


def _make_glb_pair_markdown(
    a: dict[str, Any], b: dict[str, Any], comparison: dict[str, Any]
) -> str:
    """Build a same-adapter GLB comparison focused on decimation and textures."""

    a_triangles = a.get("triangle_count")
    b_triangles = b.get("triangle_count")
    if (
        isinstance(a_triangles, int)
        and isinstance(b_triangles, int)
        and a_triangles != b_triangles
    ):
        source, processed = (a, b) if a_triangles > b_triangles else (b, a)
        source_title, processed_title = "GLB source", "GLB décimé"
    else:
        source, processed = a, b
        source_title, processed_title = "GLB A", "GLB B"

    rows: list[tuple[str, str, str, str]] = []

    def add(property_name: str, source_value: Any, processed_value: Any, reading: str) -> None:
        rows.append(
            (
                _escape_markdown(property_name),
                _escape_markdown(source_value),
                _escape_markdown(processed_value),
                _escape_markdown(reading),
            )
        )

    add(
        "Fichier",
        _short_file_name(source.get("file")),
        _short_file_name(processed.get("file")),
        "Entrées GLB analysées avec le même adaptateur glTF-Transform",
    )
    source_size = source.get("file_size_bytes")
    processed_size = processed.get("file_size_bytes")
    if isinstance(source_size, int) and source_size > 0 and isinstance(processed_size, int):
        size_reduction = 100 * (1 - processed_size / source_size)
        size_reading = f"Réduction du conteneur : {_french_decimal(size_reduction)} %"
    else:
        size_reading = "Taille de conteneur non fournie"
    add("Taille du fichier", _format_bytes(source_size), _format_bytes(processed_size), size_reading)

    source_triangles = source.get("triangle_count")
    processed_triangles = processed.get("triangle_count")
    if (
        isinstance(source_triangles, int)
        and source_triangles > 0
        and isinstance(processed_triangles, int)
    ):
        retained = 100 * processed_triangles / source_triangles
        ratio = source_triangles / processed_triangles if processed_triangles > 0 else math.inf
        triangle_reading = (
            f"{_french_decimal(retained)} % conservés ; densité divisée par "
            f"{_french_decimal(ratio)}×"
        )
    else:
        triangle_reading = "Données insuffisantes pour mesurer la décimation"
    add("Triangles", _number(source_triangles), _number(processed_triangles), triangle_reading)

    add(
        "Sommets de rendu",
        _number(source.get("vertex_count")),
        _number(processed.get("vertex_count")),
        _numeric_reading(source.get("vertex_count"), processed.get("vertex_count")),
    )
    add(
        "Positions uniques",
        _number(source.get("unique_position_tuple_count")),
        _number(processed.get("unique_position_tuple_count")),
        "Mesure comparable entre ces deux analyses GLB",
    )
    add(
        "Triangles dégénérés",
        _number(source.get("degenerate_triangle_count")),
        _number(processed.get("degenerate_triangle_count")),
        "Aucun triangle dégénéré détecté"
        if source.get("degenerate_triangle_count") == 0
        and processed.get("degenerate_triangle_count") == 0
        else "Vérifier la géométrie dégénérée",
    )

    for label, key in (
        ("Maillages", "mesh_count"),
        ("Primitives", "primitive_count"),
        ("Matériaux", "material_count"),
        ("Textures", "texture_count"),
        ("Images", "image_count"),
        ("Canaux UV", "uv_set_count"),
    ):
        left = source.get(key)
        right = processed.get(key)
        add(
            label,
            _number(left),
            _number(right),
            "Structure conservée" if left is not None and left == right else _numeric_reading(left, right),
        )

    source_texture_bytes = source.get("texture_encoded_bytes")
    processed_texture_bytes = processed.get("texture_encoded_bytes")
    if (
        isinstance(source_texture_bytes, int)
        and source_texture_bytes > 0
        and isinstance(processed_texture_bytes, int)
    ):
        texture_reduction = 100 * (1 - processed_texture_bytes / source_texture_bytes)
        texture_reading = f"Poids encodé des images réduit de {_french_decimal(texture_reduction)} %"
    else:
        texture_reading = "Inventaire détaillé requis pour comparer le poids des images"
    add(
        "Poids des images",
        _format_bytes(source_texture_bytes),
        _format_bytes(processed_texture_bytes),
        texture_reading,
    )
    add(
        "Résolutions des textures",
        _resolution_summary(source),
        _resolution_summary(processed),
        "La distribution révèle les redimensionnements par canal ; ce n’est pas une texture 2K uniforme",
    )

    source_height = _bounds_height_y(source.get("node_transformed_bounds"))
    processed_height = _bounds_height_y(processed.get("node_transformed_bounds"))
    if source_height is not None and processed_height is not None and source_height > 0:
        height_change = 100 * (processed_height / source_height - 1)
        height_reading = f"Variation de hauteur scène : {_french_decimal(height_change)} %"
        source_height_value = f"{_french_decimal(source_height, 4)} m"
        processed_height_value = f"{_french_decimal(processed_height, 4)} m"
    else:
        height_reading = "Bornes scène non fournies"
        source_height_value = processed_height_value = MISSING
    add("Hauteur scène Y", source_height_value, processed_height_value, height_reading)

    signatures_comparable = comparison.get("topology_signatures_comparable") is True
    signatures_match = comparison.get("topology_signature_match") is True
    if signatures_comparable and signatures_match:
        topology_reading = "Topologie décodée identique"
    elif signatures_comparable:
        topology_reading = "Topologie reconstruite, résultat attendu après décimation"
    else:
        topology_reading = "Signatures indisponibles ou non comparables"
    add(
        "Signature topologique",
        source.get("topology_signature"),
        processed.get("topology_signature"),
        topology_reading,
    )
    add(
        "Compression géométrique",
        _glb_compression_label(source),
        _glb_compression_label(processed),
        "Même stratégie de conteneur" if _glb_compression_label(source) == _glb_compression_label(processed) else "Compression différente",
    )

    lines = [
        "# Comparaison GLB → GLB",
        "",
        f"| Propriété | {source_title} | {processed_title} | Interprétation |",
        "|---|---:|---:|---|",
    ]
    lines.extend(
        f"| {property_name} | {source_value} | {processed_value} | {reading} |"
        for property_name, source_value, processed_value, reading in rows
    )
    return "\n".join(lines)


def _make_interpreted_markdown(
    a: dict[str, Any], b: dict[str, Any], comparison: dict[str, Any]
) -> str:
    """Build the format-specific primary comparison table."""

    if a.get("kind") == "GLB" and b.get("kind") == "GLB":
        return _make_glb_pair_markdown(a, b, comparison)
    if a.get("kind") == "FBX" and b.get("kind") == "FBX":
        return _make_fbx_pair_markdown(a, b, comparison)
    if a.get("kind") == "FBX" and b.get("kind") == "GLB":
        fbx, glb = a, b
        fbx_side, glb_side = "a", "b"
    elif a.get("kind") == "GLB" and b.get("kind") == "FBX":
        fbx, glb = b, a
        fbx_side, glb_side = "b", "a"
    else:
        return "\n".join(
            [
                "# Comparaison principale",
                "",
                "> Interprétation spécialisée indisponible : une analyse FBX et une "
                "analyse GLB identifiables sont nécessaires.",
            ]
        )

    fbx_animated = isinstance(fbx.get("animation_count"), int) and fbx["animation_count"] > 0
    fbx_title = "FBX animé" if fbx_animated else "FBX"

    fbx_triangles = fbx.get("triangle_count")
    glb_triangles = glb.get("triangle_count")
    glb_is_denser = (
        isinstance(fbx_triangles, int)
        and isinstance(glb_triangles, int)
        and glb_triangles > fbx_triangles
    )
    glb_title = "GLB haute définition" if glb_is_denser else "GLB"

    pattern = comparison.get("rig_donor_geometry_target_pattern", {})
    transfer_profile = (
        pattern.get("detected") is True
        and pattern.get("donor") == fbx_side
        and pattern.get("target") == glb_side
    )

    rows: list[tuple[str, str, str, str]] = []

    def add(property_name: str, fbx_value: Any, glb_value: Any, interpretation: str) -> None:
        rows.append(
            (
                _escape_markdown(property_name),
                _escape_markdown(fbx_value),
                _escape_markdown(glb_value),
                _escape_markdown(interpretation),
            )
        )

    fbx_meshes = fbx.get("mesh_count")
    glb_meshes = glb.get("mesh_count")
    if isinstance(fbx_meshes, int) and isinstance(glb_meshes, int) and fbx_meshes == glb_meshes:
        mesh_interpretation = "Très bon signe pour l’association des parties"
    elif isinstance(fbx_meshes, int) and isinstance(glb_meshes, int):
        mesh_interpretation = "Le nombre de parties diffère ; une association explicite sera nécessaire"
    else:
        mesh_interpretation = "Données insuffisantes pour déduire l’association des parties"
    add("Maillages", _number(fbx_meshes), _number(glb_meshes), mesh_interpretation)

    if (
        isinstance(fbx_triangles, int)
        and isinstance(glb_triangles, int)
        and fbx_triangles > 0
        and glb_triangles > 0
    ):
        if glb_triangles == fbx_triangles:
            triangle_interpretation = "Même nombre de triangles ; aucune différence de densité mesurée"
        elif glb_triangles > fbx_triangles:
            ratio = glb_triangles / fbx_triangles
            triangle_interpretation = (
                f"Le GLB est environ {_french_decimal(ratio)}× plus dense"
            )
        else:
            ratio = fbx_triangles / glb_triangles
            triangle_interpretation = (
                f"Le FBX est environ {_french_decimal(ratio)}× plus dense"
            )
    else:
        triangle_interpretation = "Données insuffisantes pour comparer la densité"
    add(
        "Triangles",
        _number(fbx_triangles),
        _number(glb_triangles),
        triangle_interpretation,
    )

    add(
        "Sommets de référence",
        (
            f"{_number(fbx.get('vertex_count'))} {fbx.get('vertex_semantic')}"
            if fbx.get("vertex_count") is not None and fbx.get("vertex_semantic")
            else MISSING
        ),
        (
            f"{_number(glb.get('vertex_count'))} {glb.get('vertex_semantic')}"
            if glb.get("vertex_count") is not None and glb.get("vertex_semantic")
            else MISSING
        ),
        (
            "Le GLB sépare les sommets de rendu aux coutures d’attributs"
            if comparison.get("cross_format_topology_candidate", {}).get("detected")
            else "Représentations différentes"
            if fbx.get("vertex_semantic") != glb.get("vertex_semantic")
            else _numeric_reading(fbx.get("vertex_count"), glb.get("vertex_count"))
        ),
    )

    unique_positions = glb.get("unique_position_tuple_count")
    add(
        "Positions uniques GLB",
        MISSING,
        _number(unique_positions),
        (
            "Égal aux control points FBX : candidat fort, à confirmer par la connectivité"
            if comparison.get("cross_format_topology_candidate", {}).get("detected")
            else "Diagnostic spatial, pas un équivalent exact des control points"
            if unique_positions is not None
            else "Diagnostic non fourni"
        ),
    )

    fbx_uv = fbx.get("uv_set_count")
    glb_uv = glb.get("uv_set_count")
    fbx_uv_value = (
        f"{_number(fbx_uv)} ensembles FBX cumulés" if fbx_uv is not None else MISSING
    )
    if glb_uv == 1:
        glb_uv_value = "1 canal `TEXCOORD_0`"
    elif isinstance(glb_uv, int) and glb_uv > 1:
        glb_uv_value = f"{_number(glb_uv)} canaux `TEXCOORD_n`"
    else:
        glb_uv_value = MISSING

    if (
        fbx_uv == fbx_meshes
        and glb_uv == 1
        and isinstance(fbx_meshes, int)
        and fbx_meshes == glb_meshes
    ):
        uv_interpretation = "Probablement un canal UV par maillage dans les deux fichiers"
    else:
        uv_interpretation = "Comptages dépendants des adaptateurs ; vérifier les liaisons UV par maillage"
    add("UV", fbx_uv_value, glb_uv_value, uv_interpretation)

    add(
        "Matériaux",
        _number(fbx.get("material_count")),
        _number(glb.get("material_count")),
        (
            "Les matériaux du GLB doivent être conservés"
            if transfer_profile and glb.get("material_count") is not None
            else "Inventaires de matériaux à vérifier avant toute conversion"
        ),
    )
    add(
        "Textures",
        _number(fbx.get("texture_count")),
        _number(glb.get("texture_count")),
        (
            "Les textures du GLB doivent être conservées"
            if transfer_profile and glb.get("texture_count") is not None
            else "Inventaires de textures à vérifier avant toute conversion"
        ),
    )

    fbx_skeleton = fbx.get("skeleton_element_count")
    glb_skeleton = glb.get("skeleton_element_count")
    skeleton_from_fbx = (
        transfer_profile
        and isinstance(fbx_skeleton, int)
        and fbx_skeleton > 0
        and glb_skeleton == 0
    )
    add(
        "Os / articulations",
        _number(fbx_skeleton),
        _number(glb_skeleton),
        "Le squelette doit venir du FBX" if skeleton_from_fbx else "Squelettes à comparer explicitement",
    )

    fbx_skins = fbx.get("skin_count")
    glb_skins = glb.get("skin_count")
    weights_from_fbx = (
        transfer_profile
        and isinstance(fbx_skins, int)
        and fbx_skins > 0
        and glb_skins == 0
    )
    add(
        "Skins",
        _number(fbx_skins),
        _number(glb_skins),
        "Les poids doivent être transférés" if weights_from_fbx else "Skins à comparer explicitement",
    )

    fbx_influences = fbx.get("max_influences")
    glb_influences = glb.get("max_influences")
    if weights_from_fbx and isinstance(fbx_influences, int) and fbx_influences > 0:
        group_count = math.ceil(fbx_influences / 4)
        group_word = "groupe" if group_count == 1 else "groupes"
        influence_interpretation = (
            f"{group_count} {group_word} `JOINTS_n` / `WEIGHTS_n` seront nécessaires "
            f"si on préserve les {fbx_influences} influences"
        )
    elif (
        comparison.get("cross_format_topology_candidate", {}).get("detected")
        and isinstance(fbx_influences, int)
        and isinstance(glb_influences, int)
        and fbx_influences > glb_influences
    ):
        influence_interpretation = (
            f"Le GLB semble plafonné à {glb_influences} influences contre {fbx_influences} "
            "dans le FBX ; vérifier la réduction et la renormalisation des poids"
        )
    else:
        influence_interpretation = "Influences à comparer avant le transfert"
    add(
        "Influences maximales",
        _number(fbx_influences),
        _number(glb_influences),
        influence_interpretation,
    )

    fbx_animations = fbx.get("animation_count")
    glb_animations = glb.get("animation_count")
    animation_from_fbx = (
        transfer_profile
        and isinstance(fbx_animations, int)
        and fbx_animations > 0
        and glb_animations == 0
    )
    fbx_effective_names = fbx.get("effective_clip_names", [])
    glb_effective_names = glb.get("effective_clip_names", [])
    if fbx_effective_names and fbx_effective_names == glb_effective_names:
        animation_interpretation = (
            "Même ensemble de clips effectifs : " + ", ".join(fbx_effective_names)
        )
    elif animation_from_fbx:
        animation_interpretation = "L’animation doit venir du FBX"
    else:
        animation_interpretation = "Animations à comparer par nom, durée et empreinte de courbes"
    add(
        "Animations",
        _animation_value(fbx),
        _animation_value(glb),
        animation_interpretation,
    )

    fbx_position_height = _bounds_height_y(fbx.get("position_bounds"))
    glb_position_height = _bounds_height_y(glb.get("position_bounds"))
    glb_transformed_height = _bounds_height_y(glb.get("node_transformed_bounds"))
    fbx_meters_per_unit = _nested(
        fbx, "coordinate_system", "meters_per_unit"
    )
    if fbx_position_height is not None and isinstance(fbx_meters_per_unit, (int, float)):
        fbx_height_value = (
            f"{_french_decimal(fbx_position_height, 4)} unités source = "
            f"{_french_decimal(fbx_position_height * fbx_meters_per_unit, 4)} m"
        )
    elif fbx_position_height is not None:
        fbx_height_value = f"{_french_decimal(fbx_position_height, 4)} unités source"
    else:
        fbx_height_value = MISSING
    if glb_position_height is not None and glb_transformed_height is not None:
        glb_height_value = (
            f"{_french_decimal(glb_position_height, 4)} m POSITION / "
            f"{_french_decimal(glb_transformed_height, 4)} m scène"
        )
        scale_interpretation = (
            "Une transformation de nœud modifie fortement l’échelle ; évaluer le bind pose avant correction"
            if glb_position_height > 0
            and abs(glb_transformed_height / glb_position_height - 1.0) > 0.01
            else "Échelles POSITION et scène cohérentes"
        )
    else:
        glb_height_value = MISSING
        scale_interpretation = "Bounds multi-espaces incomplets"
    add("Échelle / hauteur Y", fbx_height_value, glb_height_value, scale_interpretation)

    glb_diagnostics = glb.get("diagnostic_count")
    add(
        "Diagnostics",
        MISSING,
        _number(glb_diagnostics),
        (
            "Le GLB a été décodé sans anomalie signalée"
            if glb_diagnostics == 0
            else (
                "Le GLB contient des diagnostics à examiner"
                if isinstance(glb_diagnostics, int) and glb_diagnostics > 0
                else "Diagnostics GLB non fournis"
            )
        ),
    )

    compression = _glb_compression_label(glb)
    if compression == "aucune":
        compression_interpretation = (
            "Aucun décodeur Draco/Meshopt nécessaire pour ce fichier précis"
        )
    elif compression == MISSING:
        compression_interpretation = "État de compression non fourni"
    else:
        compression_interpretation = f"Le décodage {compression} est nécessaire"
    add("Compression", MISSING, compression, compression_interpretation)

    lines = [
        "# Comparaison principale",
        "",
        f"| Propriété | {fbx_title} | {glb_title} | Interprétation |",
        "|---|---:|---:|---|",
    ]
    lines.extend(
        f"| {property_name} | {fbx_value} | {glb_value} | {interpretation} |"
        for property_name, fbx_value, glb_value, interpretation in rows
    )
    return "\n".join(lines)


@dataclass(frozen=True)
class AnalysisComparisonResult:
    """Headless result returned by :func:`compare_analyses`."""

    input_a: dict[str, Any]
    input_b: dict[str, Any]
    canonical_input_a: dict[str, Any]
    canonical_input_b: dict[str, Any]
    comparison: dict[str, Any]
    comparison_markdown: str
    interpreted_markdown: str

    def to_dict(self) -> dict[str, Any]:
        """Return an isolated JSON-compatible representation of the result."""

        return {
            "status": "ok",
            "schema": SCHEMA,
            "inputs": {
                "a": deepcopy(self.canonical_input_a),
                "b": deepcopy(self.canonical_input_b),
            },
            "comparison": deepcopy(self.comparison),
        }

    def to_json(self, *, pretty: bool = True) -> str:
        """Serialize the structured result without requiring any UI framework."""

        return json.dumps(
            self.to_dict(),
            ensure_ascii=False,
            indent=2 if pretty else None,
            separators=None if pretty else (",", ":"),
        )


def compare_analyses(
    analysis_a: str | dict[str, Any], analysis_b: str | dict[str, Any]
) -> AnalysisComparisonResult:
    """Compare two FBX/GLB analysis JSON objects, JSON strings, or summaries.

    This operation is deterministic, has no ComfyUI dependency, performs no file
    I/O, and does not require a viewer or renderer.
    """

    from .contract import canonicalize_analysis

    detail_keys = (
        "image_count",
        "file_size_bytes",
        "texture_encoded_bytes",
        "texture_max_width",
        "texture_max_height",
        "texture_resolution_counts",
        "native_node_count",
    )
    try:
        source_normalized_a = normalize_analysis(analysis_a)
        canonical_a = canonicalize_analysis(analysis_a)
        normalized_a = normalize_analysis(canonical_a.to_dict())
        for key in detail_keys:
            normalized_a[key] = deepcopy(source_normalized_a.get(key))
    except ValueError as error:
        raise ValueError(f"Analysis A: {error}") from error
    try:
        source_normalized_b = normalize_analysis(analysis_b)
        canonical_b = canonicalize_analysis(analysis_b)
        normalized_b = normalize_analysis(canonical_b.to_dict())
        for key in detail_keys:
            normalized_b[key] = deepcopy(source_normalized_b.get(key))
    except ValueError as error:
        raise ValueError(f"Analysis B: {error}") from error

    comparison = _comparison_model(normalized_a, normalized_b)
    return AnalysisComparisonResult(
        input_a=normalized_a,
        input_b=normalized_b,
        canonical_input_a=canonical_a.to_dict(),
        canonical_input_b=canonical_b.to_dict(),
        comparison=comparison,
        comparison_markdown=_make_markdown(normalized_a, normalized_b, comparison),
        interpreted_markdown=_make_interpreted_markdown(
            normalized_a, normalized_b, comparison
        ),
    )
