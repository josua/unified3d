"""Canonicalization and semantic validation for analysis records."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
import json
import math
from typing import Any, Literal, cast

from .models import AnalysisRecord, AnalysisRecordDict


ANALYSIS_SCHEMA = "unified3d.analysis/1.0-rc1"


@dataclass(frozen=True, slots=True)
class ContractIssue:
    severity: Literal["warning", "error"]
    code: str
    path: str
    message: str

    def to_dict(self) -> dict[str, str]:
        return {
            "severity": self.severity,
            "code": self.code,
            "path": self.path,
            "message": self.message,
        }


@dataclass(frozen=True, slots=True)
class AnalysisValidationResult:
    valid: bool
    issues: tuple[ContractIssue, ...]

    @property
    def errors(self) -> tuple[ContractIssue, ...]:
        return tuple(issue for issue in self.issues if issue.severity == "error")

    @property
    def warnings(self) -> tuple[ContractIssue, ...]:
        return tuple(issue for issue in self.issues if issue.severity == "warning")

    def raise_for_errors(self) -> None:
        if not self.errors:
            return
        details = "; ".join(f"{issue.path}: {issue.message}" for issue in self.errors)
        raise ValueError(f"Invalid {ANALYSIS_SCHEMA} record: {details}")


def _json_object(value: str | dict[str, Any]) -> dict[str, Any] | None:
    if isinstance(value, dict):
        return deepcopy(value)
    text = str(value).strip()
    lines = text.splitlines()
    if len(lines) >= 2 and lines[0].strip().startswith("```") and lines[-1].strip() == "```":
        text = "\n".join(lines[1:-1]).strip()
    if not text.startswith("{"):
        return None
    try:
        parsed = json.loads(text)
    except json.JSONDecodeError:
        return None
    return parsed if isinstance(parsed, dict) else None


def _presence(count: int | None) -> bool | None:
    if count is None:
        return None
    return count > 0


def _signature(normalized: dict[str, Any]) -> dict[str, str] | None:
    digest = normalized.get("topology_signature")
    algorithm = normalized.get("topology_signature_kind")
    if not digest or not algorithm:
        return None
    return {
        "algorithm": str(algorithm),
        "digest": str(digest),
        "domain": "adapter-local-decoded-topology",
    }


def canonicalize_analysis(value: str | dict[str, Any] | AnalysisRecord) -> AnalysisRecord:
    """Convert a supported legacy analysis or summary to the RC1 contract.

    Existing RC1 records are validated and copied. Legacy migration is
    conservative: a field becomes ``None`` whenever its semantics were not
    explicit in the source analyzer.
    """

    if isinstance(value, AnalysisRecord):
        return AnalysisRecord.from_dict(value.to_dict())

    source = _json_object(value)
    if source is not None and source.get("schema") == ANALYSIS_SCHEMA:
        validation = validate_analysis(source)
        validation.raise_for_errors()
        return AnalysisRecord.from_dict(cast(AnalysisRecordDict, source))

    # Local import avoids a module cycle while the legacy comparator API stays
    # source-compatible during the RC migration.
    from .comparison import normalize_analysis

    normalized = normalize_analysis(value)
    kind = normalized.get("kind")
    if kind == "FBX":
        asset_format = "fbx"
        container = "fbx"
        geometric_count = normalized.get("vertex_count")
        geometric_semantic = "control_points"
        render_count = None
        uv_channels = None
        uv_bindings = normalized.get("uv_set_count")
    elif kind == "GLB":
        asset_format = "gltf"
        container = "glb"
        geometric_count = normalized.get("unique_position_tuple_count")
        geometric_semantic = "unique_positions" if geometric_count is not None else None
        render_count = normalized.get("vertex_count")
        uv_channels = normalized.get("uv_set_count")
        uv_bindings = None
    else:
        asset_format = "unknown"
        container = "unknown"
        geometric_count = None
        geometric_semantic = None
        render_count = normalized.get("vertex_count")
        uv_channels = normalized.get("uv_set_count")
        uv_bindings = None

    source_analyzer = source.get("analyzer", {}) if source else {}
    if not isinstance(source_analyzer, dict):
        source_analyzer = {}
    backend = source_analyzer.get("backend") or normalized.get("generator") or "unknown"
    analyzer_name = source_analyzer.get("name") or f"Legacy {kind} analysis adapter"
    skin_count = normalized.get("skin_count")
    skeleton_count = normalized.get("skeleton_element_count")
    animation_count = normalized.get("animation_count")
    max_influences = normalized.get("max_influences")

    diagnostics: list[dict[str, str | None]] = []
    diagnostic_count = normalized.get("diagnostic_count")
    if isinstance(diagnostic_count, int) and diagnostic_count > 0:
        diagnostics.append(
            {
                "severity": "warning",
                "code": "LEGACY_DIAGNOSTICS_NOT_EXPANDED",
                "message": f"Legacy analyzer reported {diagnostic_count} diagnostic(s) without structured details.",
                "path": None,
            }
        )

    native_key = asset_format if asset_format != "unknown" else "legacy"
    native: dict[str, Any] = {
        native_key: {
            "source_schema": source.get("schema") if source else None,
            "source_format": normalized.get("source_format"),
        }
    }
    if kind == "GLB":
        native["gltf"].update(
            {
                "draco_compressed": normalized.get("draco"),
                "meshopt_compressed": normalized.get("meshopt"),
            }
        )
    if kind == "FBX":
        native["fbx"].update(
            {
                "polygon_vertex_count": normalized.get("polygon_vertex_count"),
                "polygon_count": normalized.get("polygon_count"),
            }
        )

    record = cast(
        AnalysisRecordDict,
        {
            "status": "ok",
            "schema": ANALYSIS_SCHEMA,
            "schema_status": "release-candidate",
            "analyzer": {
                "name": str(analyzer_name),
                "backend": str(backend),
                "backend_version": source_analyzer.get("backend_version"),
                "analyzer_version": source_analyzer.get("analyzer_version") or source_analyzer.get("schema_version"),
            },
            "asset": {
                "path": normalized.get("file"),
                "format": asset_format,
                "container": container,
                "version": normalized.get("version"),
                "generator": normalized.get("generator"),
                "size_bytes": (
                    source.get("asset", {}).get("size_bytes")
                    if source and isinstance(source.get("asset"), dict)
                    else source.get("file", {}).get("size_bytes")
                    if source and isinstance(source.get("file"), dict)
                    else None
                ),
                "coordinate_system": {
                    "handedness": None,
                    "up_axis": None,
                    "forward_axis": None,
                    "unit": None,
                    "meters_per_unit": None,
                },
            },
            "scene": {
                "scene_count": None,
                "node_count": None,
                "mesh_instance_count": normalized.get("mesh_instance_count"),
                "bounds": None,
            },
            "geometry": {
                "mesh_count": normalized.get("mesh_count"),
                "primitive_count": normalized.get("primitive_count"),
                "geometric_vertex_count": geometric_count,
                "geometric_vertex_semantic": geometric_semantic,
                "render_vertex_count": render_count,
                "index_count": normalized.get("index_count"),
                "polygon_vertex_count": normalized.get("polygon_vertex_count"),
                "polygon_count": normalized.get("polygon_count"),
                "triangle_count": normalized.get("triangle_count"),
                "degenerate_triangle_count": normalized.get("degenerate_triangle_count"),
                "ngon_count": None,
                "uv_channel_count": uv_channels,
                "uv_set_binding_count": uv_bindings,
                "normal_count": None,
                "tangent_count": None,
                "color_attribute_count": None,
                "bounds": None,
                "topology_signature": _signature(normalized),
            },
            "materials": {
                "material_resource_count": normalized.get("material_count"),
                "material_binding_count": None,
                "texture_resource_count": normalized.get("texture_count"),
            },
            "skeleton": {
                "present": _presence(skeleton_count),
                "joint_count": skeleton_count,
                "root_joint_count": None,
                "hierarchy_signature": None,
                "bind_pose_signature": None,
                "joint_names": None,
            },
            "skin": {
                "present": normalized.get("has_skin") if normalized.get("has_skin") is not None else _presence(skin_count),
                "skin_count": skin_count,
                "cluster_count": (
                    source.get("rig", {}).get("cluster_count")
                    if source and isinstance(source.get("rig"), dict)
                    else None
                ),
                "skinned_vertex_count": None,
                "max_influences": max_influences,
                "influence_set_count": math.ceil(max_influences / 4) if isinstance(max_influences, int) and max_influences > 0 else 0 if max_influences == 0 else None,
            },
            "animation": {
                "present": _presence(animation_count),
                "clip_count": animation_count,
                "channel_count": None,
                "sampler_count": None,
                "duration_seconds": None,
            },
            "native": native,
            "diagnostics": diagnostics,
        },
    )
    validation = validate_analysis(record)
    validation.raise_for_errors()
    return AnalysisRecord.from_dict(record)


_TOP_LEVEL_KEYS = {
    "status", "schema", "schema_status", "analyzer", "asset", "scene",
    "geometry", "materials", "skeleton", "skin", "animation", "native",
    "diagnostics",
}


def validate_analysis(value: AnalysisRecord | dict[str, Any]) -> AnalysisValidationResult:
    """Validate RC1 structure and cross-field semantic invariants."""

    data = value.to_dict() if isinstance(value, AnalysisRecord) else value
    issues: list[ContractIssue] = []

    def error(code: str, path: str, message: str) -> None:
        issues.append(ContractIssue("error", code, path, message))

    def warning(code: str, path: str, message: str) -> None:
        issues.append(ContractIssue("warning", code, path, message))

    if not isinstance(data, dict):
        error("TYPE_OBJECT", "$", "The analysis root must be an object.")
        return AnalysisValidationResult(False, tuple(issues))

    for key in sorted(_TOP_LEVEL_KEYS - set(data)):
        error("REQUIRED_FIELD", f"$.{key}", "Required field is missing.")
    for key in sorted(set(data) - _TOP_LEVEL_KEYS):
        error("UNKNOWN_FIELD", f"$.{key}", "Unknown top-level field.")
    if data.get("status") != "ok":
        error("STATUS", "$.status", "RC1 success records must use status 'ok'.")
    if data.get("schema") != ANALYSIS_SCHEMA:
        error("SCHEMA", "$.schema", f"Expected {ANALYSIS_SCHEMA!r}.")
    if data.get("schema_status") != "release-candidate":
        error("SCHEMA_STATUS", "$.schema_status", "Expected 'release-candidate'.")

    section_keys: dict[str, set[str]] = {
        "analyzer": {"name", "backend", "backend_version", "analyzer_version"},
        "asset": {"path", "format", "container", "version", "generator", "size_bytes", "coordinate_system"},
        "scene": {"scene_count", "node_count", "mesh_instance_count", "bounds"},
        "geometry": {
            "mesh_count", "primitive_count", "geometric_vertex_count", "geometric_vertex_semantic",
            "render_vertex_count", "index_count", "polygon_vertex_count", "polygon_count",
            "triangle_count", "degenerate_triangle_count", "ngon_count", "uv_channel_count",
            "uv_set_binding_count", "normal_count", "tangent_count", "color_attribute_count",
            "bounds", "topology_signature",
        },
        "materials": {"material_resource_count", "material_binding_count", "texture_resource_count"},
        "skeleton": {"present", "joint_count", "root_joint_count", "hierarchy_signature", "bind_pose_signature", "joint_names"},
        "skin": {"present", "skin_count", "cluster_count", "skinned_vertex_count", "max_influences", "influence_set_count"},
        "animation": {"present", "clip_count", "channel_count", "sampler_count", "duration_seconds"},
    }
    for section, required in section_keys.items():
        current = data.get(section)
        if not isinstance(current, dict):
            error("TYPE_OBJECT", f"$.{section}", "Section must be an object.")
            continue
        for key in sorted(required - set(current)):
            error("REQUIRED_FIELD", f"$.{section}.{key}", "Required field is missing; use null when not computed.")
        for key in sorted(set(current) - required):
            error("UNKNOWN_FIELD", f"$.{section}.{key}", "Unknown common-contract field; format-specific data belongs in native.")

    integer_paths = {
        "asset": ("size_bytes",),
        "scene": ("scene_count", "node_count", "mesh_instance_count"),
        "geometry": tuple(key for key in section_keys["geometry"] if key.endswith("_count")),
        "materials": tuple(section_keys["materials"]),
        "skeleton": ("joint_count", "root_joint_count"),
        "skin": ("skin_count", "cluster_count", "skinned_vertex_count", "max_influences", "influence_set_count"),
        "animation": ("clip_count", "channel_count", "sampler_count"),
    }
    for section, keys in integer_paths.items():
        current = data.get(section)
        if not isinstance(current, dict):
            continue
        for key in keys:
            current_value = current.get(key)
            if current_value is not None and (isinstance(current_value, bool) or not isinstance(current_value, int) or current_value < 0):
                error("NON_NEGATIVE_INTEGER", f"$.{section}.{key}", "Value must be a non-negative integer or null.")

    for section, count_key in (("skeleton", "joint_count"), ("skin", "skin_count"), ("animation", "clip_count")):
        current = data.get(section)
        if not isinstance(current, dict):
            continue
        present = current.get("present")
        count = current.get(count_key)
        if present is not True and present is not False and present is not None:
            error("PRESENCE_TYPE", f"$.{section}.present", "Presence must be true, false, or null.")
        if present is False and isinstance(count, int) and count != 0:
            error("PRESENCE_COUNT", f"$.{section}", f"present=false requires {count_key}=0 or null.")
        if present is True and count == 0:
            error("PRESENCE_COUNT", f"$.{section}", f"present=true conflicts with {count_key}=0.")

    geometry = data.get("geometry")
    if isinstance(geometry, dict):
        semantic = geometry.get("geometric_vertex_semantic")
        geometric_count = geometry.get("geometric_vertex_count")
        if geometric_count is not None and semantic not in {"control_points", "unique_positions"}:
            error("VERTEX_SEMANTIC", "$.geometry.geometric_vertex_semantic", "A geometric vertex count requires an explicit supported semantic.")
        degenerate = geometry.get("degenerate_triangle_count")
        triangles = geometry.get("triangle_count")
        if isinstance(degenerate, int) and isinstance(triangles, int) and degenerate > triangles:
            error("COUNT_RELATION", "$.geometry.degenerate_triangle_count", "Degenerate triangle count cannot exceed triangle count.")

        signature = geometry.get("topology_signature")
        if signature is not None:
            signature_keys = {"algorithm", "digest", "domain"}
            if not isinstance(signature, dict):
                error("TYPE_OBJECT", "$.geometry.topology_signature", "Topology signature must be an object or null.")
            else:
                for key in sorted(signature_keys - set(signature)):
                    error("REQUIRED_FIELD", f"$.geometry.topology_signature.{key}", "Required signature field is missing.")
                for key in sorted(set(signature) - signature_keys):
                    error("UNKNOWN_FIELD", f"$.geometry.topology_signature.{key}", "Unknown topology signature field.")
                for key in signature_keys:
                    signature_value = signature.get(key)
                    if not isinstance(signature_value, str) or not signature_value:
                        error("NON_EMPTY_STRING", f"$.geometry.topology_signature.{key}", "Value must be a non-empty string.")

    asset = data.get("asset")
    if isinstance(asset, dict):
        asset_format = asset.get("format")
        container = asset.get("container")
        if asset_format not in {"fbx", "gltf", "unknown"}:
            error("FORMAT", "$.asset.format", "Unsupported common format identifier.")
        if container not in {"fbx", "glb", "gltf", "unknown"}:
            error("CONTAINER", "$.asset.container", "Unsupported container identifier.")
        if asset_format == "fbx" and container != "fbx":
            error("FORMAT_CONTAINER", "$.asset.container", "FBX format requires the FBX container.")
        if asset_format == "gltf" and container not in {"glb", "gltf"}:
            error("FORMAT_CONTAINER", "$.asset.container", "glTF format requires the GLB or glTF container.")
        coordinate_system = asset.get("coordinate_system")
        required_coordinates = {"handedness", "up_axis", "forward_axis", "unit", "meters_per_unit"}
        if not isinstance(coordinate_system, dict):
            error("TYPE_OBJECT", "$.asset.coordinate_system", "Coordinate system must be an object.")
        else:
            for key in sorted(required_coordinates - set(coordinate_system)):
                error("REQUIRED_FIELD", f"$.asset.coordinate_system.{key}", "Required field is missing; use null when not computed.")
            scale = coordinate_system.get("meters_per_unit")
            if scale is not None and (isinstance(scale, bool) or not isinstance(scale, (int, float)) or not math.isfinite(scale) or scale <= 0):
                error("UNIT_SCALE", "$.asset.coordinate_system.meters_per_unit", "Scale must be a finite positive number or null.")
            if all(coordinate_system.get(key) is None for key in required_coordinates):
                warning("COORDINATE_SYSTEM_UNKNOWN", "$.asset.coordinate_system", "Spatial compatibility cannot be evaluated until axes and units are known.")

    def validate_bounds(path: str, bounds: Any) -> None:
        if bounds is None:
            return
        if not isinstance(bounds, dict):
            error("TYPE_OBJECT", path, "Bounds must be an object or null.")
            return
        if set(bounds) != {"min", "max"}:
            error("BOUNDS_FIELDS", path, "Bounds must contain exactly min and max.")
            return
        minimum, maximum = bounds.get("min"), bounds.get("max")
        for key, vector in (("min", minimum), ("max", maximum)):
            if not isinstance(vector, list) or len(vector) != 3:
                error("VECTOR3", f"{path}.{key}", "Bounds vector must contain exactly three finite numbers.")
                return
            if any(isinstance(component, bool) or not isinstance(component, (int, float)) or not math.isfinite(component) for component in vector):
                error("VECTOR3", f"{path}.{key}", "Bounds vector must contain exactly three finite numbers.")
                return
        if any(float(minimum[index]) > float(maximum[index]) for index in range(3)):
            error("BOUNDS_ORDER", path, "Each min component must be less than or equal to max.")

    scene = data.get("scene")
    validate_bounds("$.scene.bounds", scene.get("bounds") if isinstance(scene, dict) else None)
    validate_bounds("$.geometry.bounds", geometry.get("bounds") if isinstance(geometry, dict) else None)

    analyzer = data.get("analyzer")
    if isinstance(analyzer, dict):
        for key in ("name", "backend"):
            if not isinstance(analyzer.get(key), str) or not analyzer.get(key):
                error("NON_EMPTY_STRING", f"$.analyzer.{key}", "Value must be a non-empty string.")
        for key in ("backend_version", "analyzer_version"):
            if analyzer.get(key) is not None and not isinstance(analyzer.get(key), str):
                error("NULLABLE_STRING", f"$.analyzer.{key}", "Value must be a string or null.")

    skeleton = data.get("skeleton")
    if isinstance(skeleton, dict):
        joint_names = skeleton.get("joint_names")
        if joint_names is not None and (
            not isinstance(joint_names, list)
            or any(not isinstance(name, str) for name in joint_names)
        ):
            error("STRING_ARRAY", "$.skeleton.joint_names", "Joint names must be an array of strings or null.")
        joint_count = skeleton.get("joint_count")
        if isinstance(joint_names, list) and isinstance(joint_count, int) and len(joint_names) not in {0, joint_count}:
            warning("JOINT_NAME_COVERAGE", "$.skeleton.joint_names", "Joint-name inventory is partial.")

    skin = data.get("skin")
    if isinstance(skin, dict):
        max_influences = skin.get("max_influences")
        influence_set_count = skin.get("influence_set_count")
        if isinstance(max_influences, int) and isinstance(influence_set_count, int):
            required_sets = math.ceil(max_influences / 4)
            if influence_set_count < required_sets:
                error(
                    "INFLUENCE_SET_CAPACITY",
                    "$.skin.influence_set_count",
                    "Influence-set count cannot represent max_influences.",
                )

    animation = data.get("animation")
    if isinstance(animation, dict):
        duration = animation.get("duration_seconds")
        if duration is not None and (
            isinstance(duration, bool)
            or not isinstance(duration, (int, float))
            or not math.isfinite(duration)
            or duration < 0
        ):
            error("DURATION", "$.animation.duration_seconds", "Duration must be a finite non-negative number or null.")

    native = data.get("native")
    if not isinstance(native, dict):
        error("TYPE_OBJECT", "$.native", "Native extension data must be an object.")
    diagnostics = data.get("diagnostics")
    if not isinstance(diagnostics, list):
        error("TYPE_ARRAY", "$.diagnostics", "Diagnostics must be an array.")
    else:
        diagnostic_keys = {"severity", "code", "message", "path"}
        for index, diagnostic in enumerate(diagnostics):
            path = f"$.diagnostics[{index}]"
            if not isinstance(diagnostic, dict):
                error("TYPE_OBJECT", path, "Diagnostic must be an object.")
                continue
            if set(diagnostic) != diagnostic_keys:
                error("DIAGNOSTIC_FIELDS", path, "Diagnostic must contain exactly severity, code, message, and path.")
            if diagnostic.get("severity") not in {"info", "warning", "error"}:
                error("DIAGNOSTIC_SEVERITY", f"{path}.severity", "Unsupported diagnostic severity.")
            for key in ("code", "message"):
                if not isinstance(diagnostic.get(key), str) or not diagnostic.get(key):
                    error("NON_EMPTY_STRING", f"{path}.{key}", "Value must be a non-empty string.")
            if diagnostic.get("path") is not None and not isinstance(diagnostic.get("path"), str):
                error("NULLABLE_STRING", f"{path}.path", "Path must be a string or null.")

    return AnalysisValidationResult(not any(issue.severity == "error" for issue in issues), tuple(issues))
