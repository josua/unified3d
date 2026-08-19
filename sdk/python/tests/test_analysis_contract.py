import json
import sys
import unittest
from copy import deepcopy
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = ROOT.parents[1]
FIXTURES = ROOT / "tests" / "fixtures"
sys.path.insert(0, str(ROOT / "src"))

from unified3d import (  # noqa: E402
    ANALYSIS_SCHEMA,
    AnalysisRecord,
    canonicalize_analysis,
    compare_analyses,
    validate_analysis,
)


def fixture(name: str) -> dict:
    return json.loads((FIXTURES / name).read_text(encoding="utf-8"))


class SchemaDocumentTests(unittest.TestCase):
    def test_schema_documents_are_valid_json_with_expected_ids(self):
        analysis_schema = json.loads(
            (REPOSITORY_ROOT / "schemas" / "unified3d.analysis-1.0-rc1.schema.json").read_text(encoding="utf-8")
        )
        comparison_schema = json.loads(
            (REPOSITORY_ROOT / "schemas" / "unified3d.analysis-comparison-1.0-rc1.schema.json").read_text(encoding="utf-8")
        )

        self.assertEqual(analysis_schema["properties"]["schema"]["const"], ANALYSIS_SCHEMA)
        self.assertEqual(
            comparison_schema["properties"]["schema"]["const"],
            "unified3d.analysis-comparison/1.0-rc1",
        )

        def local_refs(value):
            if isinstance(value, dict):
                if isinstance(value.get("$ref"), str) and value["$ref"].startswith("#/$defs/"):
                    yield value["$ref"]
                for child in value.values():
                    yield from local_refs(child)
            elif isinstance(value, list):
                for child in value:
                    yield from local_refs(child)

        definitions = analysis_schema["$defs"]
        self.assertTrue(all(reference.removeprefix("#/$defs/") in definitions for reference in local_refs(analysis_schema)))


class ContractValidationTests(unittest.TestCase):
    def test_regression_fixtures_are_valid_with_only_coordinate_warnings(self):
        for name in (
            "thief-fbx.analysis-1.0-rc1.json",
            "thief-glb.analysis-1.0-rc1.json",
        ):
            result = validate_analysis(fixture(name))
            self.assertTrue(result.valid, result.errors)
            self.assertEqual(
                [issue.code for issue in result.warnings],
                ["COORDINATE_SYSTEM_UNKNOWN"],
            )

    def test_missing_measured_field_is_rejected_instead_of_assumed(self):
        source = fixture("thief-glb.analysis-1.0-rc1.json")
        del source["geometry"]["triangle_count"]

        result = validate_analysis(source)

        self.assertFalse(result.valid)
        self.assertIn("$.geometry.triangle_count", [issue.path for issue in result.errors])

    def test_zero_and_null_have_distinct_valid_meanings(self):
        source = fixture("thief-glb.analysis-1.0-rc1.json")
        source["skin"]["present"] = True

        result = validate_analysis(source)

        self.assertFalse(result.valid)
        self.assertIn("PRESENCE_COUNT", [issue.code for issue in result.errors])

    def test_influence_sets_must_cover_maximum_influences(self):
        source = fixture("thief-fbx.analysis-1.0-rc1.json")
        source["skin"]["influence_set_count"] = 1

        result = validate_analysis(source)

        self.assertFalse(result.valid)
        self.assertIn("INFLUENCE_SET_CAPACITY", [issue.code for issue in result.errors])

    def test_record_exports_are_isolated(self):
        record = AnalysisRecord.from_dict(fixture("thief-fbx.analysis-1.0-rc1.json"))
        exported = record.to_dict()
        exported["geometry"]["mesh_count"] = 999

        self.assertEqual(record.geometry["mesh_count"], 10)

    def test_parallel_up_and_forward_axes_are_rejected(self):
        source = fixture("thief-glb.analysis-1.0-rc1.json")
        source["asset"]["coordinate_system"] = {
            "handedness": "right",
            "up_axis": "Y",
            "forward_axis": "-Y",
            "unit": "m",
            "meters_per_unit": 1.0,
        }

        result = validate_analysis(source)

        self.assertFalse(result.valid)
        self.assertIn("AXIS_BASIS", [issue.code for issue in result.errors])


class CanonicalizationTests(unittest.TestCase):
    def test_existing_rc1_record_is_copied(self):
        source = fixture("thief-fbx.analysis-1.0-rc1.json")
        record = canonicalize_analysis(source)
        source["geometry"]["mesh_count"] = 999

        self.assertEqual(record.geometry["mesh_count"], 10)

    def test_legacy_sources_preserve_vertex_and_uv_semantics(self):
        from test_comparison import fbx_json, glb_json

        fbx = canonicalize_analysis(fbx_json())
        glb = canonicalize_analysis(glb_json())

        self.assertEqual(fbx.geometry["geometric_vertex_semantic"], "control_points")
        self.assertEqual(fbx.geometry["uv_set_binding_count"], 10)
        self.assertIsNone(fbx.geometry["uv_channel_count"])
        self.assertEqual(glb.geometry["geometric_vertex_semantic"], "unique_positions")
        self.assertEqual(glb.geometry["render_vertex_count"], 1045852)
        self.assertEqual(glb.geometry["uv_channel_count"], 1)

    def test_comparison_contains_levels_zero_through_seven(self):
        result = compare_analyses(
            fixture("thief-fbx.analysis-1.0-rc1.json"),
            fixture("thief-glb.analysis-1.0-rc1.json"),
        )
        compatibility = result.comparison["compatibility"]

        self.assertEqual([level["level"] for level in compatibility["levels"]], list(range(8)))
        self.assertEqual(compatibility["classification"], "ADVANCED_TRANSFER_REQUIRED")
        self.assertEqual(compatibility["recommended_next_level"], 1)
        self.assertAlmostEqual(compatibility["coverage"], 2 / 7)
        self.assertEqual(compatibility["levels"][5]["status"], "not_comparable")
        self.assertEqual(compatibility["levels"][6]["status"], "not_comparable")
        self.assertEqual(compatibility["levels"][7]["status"], "not_available")
        serialized = result.to_dict()
        self.assertEqual(serialized["status"], "ok")
        self.assertEqual(serialized["schema"], "unified3d.analysis-comparison/1.0-rc1")
        self.assertEqual(serialized["inputs"]["a"]["schema"], ANALYSIS_SCHEMA)
        self.assertEqual(serialized["inputs"]["b"]["schema"], ANALYSIS_SCHEMA)

    def test_level_seven_normalizes_axes_units_scale_and_bounds(self):
        fbx = fixture("thief-fbx.analysis-1.0-rc1.json")
        glb = fixture("thief-glb.analysis-1.0-rc1.json")
        fbx["asset"]["coordinate_system"] = {
            "handedness": "right",
            "up_axis": "Y",
            "forward_axis": "-Z",
            "unit": "m",
            "meters_per_unit": 1.0,
        }
        fbx["geometry"]["bounds"] = {
            "min": [-1.0, 0.0, -0.5],
            "max": [1.0, 2.0, 0.5],
        }
        glb["asset"]["coordinate_system"] = {
            "handedness": "left",
            "up_axis": "Z",
            "forward_axis": "Y",
            "unit": "cm",
            "meters_per_unit": 0.01,
        }
        glb["geometry"]["bounds"] = {
            "min": [-100.0, -50.0, 0.0],
            "max": [100.0, 50.0, 200.0],
        }

        compatibility = compare_analyses(fbx, glb).comparison["compatibility"]
        spatial = compatibility["levels"][7]

        self.assertEqual(spatial["status"], "match")
        self.assertAlmostEqual(spatial["score"], 1.0)
        self.assertAlmostEqual(spatial["evidence"]["center_distance_m"], 0.0)
        self.assertAlmostEqual(spatial["evidence"]["bounds_iou"], 1.0)
        self.assertTrue(spatial["evidence"]["units_normalized"])
        self.assertEqual(
            compatibility["classification"],
            "SPATIAL_SKIN_TRANSFER_REQUIRED",
        )

    def test_semantically_invalid_rc1_input_is_rejected_by_comparator(self):
        source = fixture("thief-fbx.analysis-1.0-rc1.json")
        source["skin"]["present"] = False

        with self.assertRaisesRegex(ValueError, "Analysis A"):
            compare_analyses(source, fixture("thief-glb.analysis-1.0-rc1.json"))


if __name__ == "__main__":
    unittest.main()
