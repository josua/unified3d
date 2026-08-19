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
        "skeleton_element_count": None,
        "skeleton_element_semantic": None,
        "skin_count": None,
        "has_skin": None,
        "max_influences": None,
        "animation_count": None,
        "draco": None,
        "meshopt": None,
        "diagnostic_count": None,
        "topology_signature": None,
        "topology_signature_kind": None,
        "topology_signature_domain": None,
        "coordinate_system": None,
        "bounds": None,
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
                "draco": _boolean(native.get("draco_compressed")),
                "meshopt": _boolean(native.get("meshopt_compressed")),
                "diagnostic_count": len(diagnostics) if isinstance(diagnostics, list) else None,
                "topology_signature": signature.get("digest"),
                "topology_signature_kind": signature.get("algorithm"),
                "topology_signature_domain": signature.get("domain"),
                "coordinate_system": asset.get("coordinate_system"),
                "bounds": _first(geometry.get("bounds"), scene.get("bounds")),
            }
        )
        return result

    if kind == "FBX":
        rig = data.get("rig", {}) if isinstance(data.get("rig"), dict) else {}
        animation = data.get("animation", {}) if isinstance(data.get("animation"), dict) else {}
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
                "animation_count": _integer(animation.get("animation_stack_count")),
                "topology_signature": geometry.get("topology_signature"),
                "topology_signature_kind": "fbx_adapter_topology",
                "topology_signature_domain": "adapter-local-decoded-topology",
            }
        )
        return result

    if kind == "GLB":
        skeleton = data.get("skeleton", {}) if isinstance(data.get("skeleton"), dict) else {}
        skin = data.get("skin", {}) if isinstance(data.get("skin"), dict) else {}
        animation = data.get("animation", {}) if isinstance(data.get("animation"), dict) else {}
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
                "skeleton_element_count": _integer(skeleton.get("joint_count")),
                "skeleton_element_semantic": "joints",
                "skin_count": _integer(skin.get("skin_count")),
                "has_skin": _boolean(_first(skin.get("applied"), skin.get("has_skin"))),
                "max_influences": _integer(skin.get("max_influences")),
                "animation_count": _integer(animation.get("animation_count")),
                "draco": _boolean(_nested(data, "native", "gltf", "draco_compressed")),
                "meshopt": _boolean(_nested(data, "native", "gltf", "meshopt_compressed")),
                "diagnostic_count": len(diagnostics) if isinstance(diagnostics, list) else _integer(diagnostics),
                "topology_signature": _nested(
                    data, "geometry", "signatures", "decoded_topology_sha256"
                ),
                "topology_signature_kind": "decoded_gltf_topology_sha256",
                "topology_signature_domain": "adapter-local-decoded-topology",
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
            "skin_count": _integer(get("Skins")),
            "animation_count": _integer(get("Animations")),
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
        "topology_signatures_comparable": (
            bool(a.get("topology_signature") and b.get("topology_signature"))
            and a.get("topology_signature_kind") == b.get("topology_signature_kind")
            and a.get("topology_signature_domain") == b.get("topology_signature_domain")
        ),
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
    vertex_reading = (
        "Mesures différentes : aucune équivalence directe"
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
        "Animations",
        _number(a.get("animation_count")),
        _number(b.get("animation_count")),
        _numeric_reading(a.get("animation_count"), b.get("animation_count")),
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


def _make_interpreted_markdown(
    a: dict[str, Any], b: dict[str, Any], comparison: dict[str, Any]
) -> str:
    """Build a concise FBX-to-GLB table only when both roles are identifiable."""

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
        if glb_triangles >= fbx_triangles:
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
            "Représentations différentes"
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
            "Diagnostic spatial, pas un équivalent exact des control points"
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
    add(
        "Animations",
        _number(fbx_animations),
        _number(glb_animations),
        "L’animation doit venir du FBX" if animation_from_fbx else "Animations à comparer explicitement",
    )

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

    try:
        canonical_a = canonicalize_analysis(analysis_a)
        normalized_a = normalize_analysis(canonical_a.to_dict())
    except ValueError as error:
        raise ValueError(f"Analysis A: {error}") from error
    try:
        canonical_b = canonicalize_analysis(analysis_b)
        normalized_b = normalize_analysis(canonical_b.to_dict())
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
