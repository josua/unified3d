import json
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from unified3d.analysis import compare_analyses, normalize_analysis  # noqa: E402


FBX_SUMMARY = """FBX Geometry / Rig Analysis
File: C:\\assets\\thief-walking.fbx
FBX Version: 7.7.0
Meshes: 10
Control Points: 27311
Polygon Vertices: 150177
Polygons: 50059
Triangles (fan-equivalent): 50059
UV Sets: 10
Materials: 1
Skeleton Bones: 41
Skins: 10
Clusters: 75
Has Skin: True
Max Influences / Control Point: 6
Animations: 1
Topology Signature: fd17e9faa2426bc8
"""


GLB_SUMMARY = """GLB Geometry / Rig Analysis
File: C:\\assets\\thief.glb
glTF Version: 2.0
Generator: Tripo
Meshes: 10
Mesh Instances: 10
Primitives: 10
Render Vertices: 1045852
Unique POSITION Tuples (diagnostic): 983579
Indices: 5871732
Triangles: 1957244
Degenerate Triangles: 0
UV Sets: 1
Materials: 10
Textures: 30
Joints: 0
Skins: 0
Has Applied Skin: False
Max Influences / Render Vertex: 0
Animations: 0
Draco: False
Meshopt: False
Diagnostics: 0
Decoded Topology Signature: adf37d930c59a58b1f2be3890623de660f2e47aaf453578b65d487d5c53fbbed
"""


def fbx_json() -> str:
    return json.dumps(
        {
            "status": "ok",
            "analyzer": {
                "name": "FBX Geometry Rig Analyzer",
                "backend": "Autodesk FBX SDK",
            },
            "file": {
                "path": "C:\\assets\\thief-walking.fbx",
                "fbx_version": "7.7.0",
            },
            "geometry": {
                "mesh_count": 10,
                "control_point_count": 27311,
                "polygon_vertex_count": 150177,
                "polygon_count": 50059,
                "triangle_count": 50059,
                "topology_signature": "fd17e9faa2426bc8",
            },
            "attributes": {"uv_set_count": 10},
            "materials": {"material_count": 1, "texture_object_count": 3},
            "rig": {
                "bone_count": 41,
                "skin_deformer_count": 10,
                "has_skin": True,
                "max_influences_per_control_point": 6,
            },
            "animation": {"animation_stack_count": 1},
        }
    )


def glb_json() -> str:
    return json.dumps(
        {
            "status": "ok",
            "schema": "unified3d.analysis/1.0",
            "analyzer": {
                "name": "GLB Geometry Rig Analyzer",
                "backend": "glTF-Transform",
            },
            "asset": {
                "path": "C:\\assets\\thief.glb",
                "container": "glb",
                "gltf_version": "2.0",
                "generator": "Tripo",
            },
            "geometry": {
                "mesh_count": 10,
                "mesh_instance_count": 10,
                "primitive_count": 10,
                "render_vertex_count": 1045852,
                "unique_position_tuple_count": 983579,
                "explicit_index_count": 5871732,
                "triangle_count": 1957244,
                "degenerate_triangle_count": 0,
                "uv_set_count": 1,
                "signatures": {
                    "decoded_topology_sha256": "adf37d930c59a58b1f2be3890623de660f2e47aaf453578b65d487d5c53fbbed"
                },
            },
            "materials": {"material_count": 10, "texture_count": 30},
            "skeleton": {"joint_count": 0},
            "skin": {"skin_count": 0, "applied": False, "max_influences": 0},
            "animation": {"animation_count": 0},
            "native": {
                "gltf": {"draco_compressed": False, "meshopt_compressed": False}
            },
            "diagnostics": [],
        }
    )


class NormalizationTests(unittest.TestCase):
    def test_normalizes_fbx_and_glb_json(self):
        fbx = normalize_analysis(fbx_json())
        glb = normalize_analysis(glb_json())

        self.assertEqual(fbx["kind"], "FBX")
        self.assertEqual(fbx["vertex_semantic"], "control points")
        self.assertEqual(fbx["skeleton_element_count"], 41)
        self.assertEqual(glb["kind"], "GLB")
        self.assertEqual(glb["vertex_semantic"], "render vertices")
        self.assertEqual(glb["index_count"], 5871732)

    def test_normalizes_both_text_summaries(self):
        fbx = normalize_analysis(FBX_SUMMARY)
        glb = normalize_analysis(GLB_SUMMARY)

        self.assertEqual(fbx["triangle_count"], 50059)
        self.assertTrue(fbx["has_skin"])
        self.assertEqual(glb["unique_position_tuple_count"], 983579)
        self.assertFalse(glb["has_skin"])

    def test_accepts_fenced_json(self):
        normalized = normalize_analysis(f"```json\n{glb_json()}\n```")
        self.assertEqual(normalized["kind"], "GLB")

    def test_accepts_python_dictionary(self):
        source = json.loads(glb_json())
        normalized = normalize_analysis(source)

        self.assertEqual(normalized["kind"], "GLB")
        self.assertEqual(normalized["triangle_count"], 1957244)

    def test_rejects_invalid_json_object(self):
        with self.assertRaisesRegex(ValueError, "Invalid analysis JSON"):
            normalize_analysis('{"status": "ok"')


class ComparatorTests(unittest.TestCase):
    def test_recognizes_effective_clips_and_cross_format_topology_candidate(self):
        fbx = json.loads(fbx_json())
        glb = json.loads(glb_json())
        fbx["geometry"].update(
            {
                "mesh_count": 1,
                "control_point_count": 54707,
                "polygon_vertex_count": 328737,
                "polygon_count": 109579,
                "triangle_count": 109579,
            }
        )
        fbx["animation"] = {
            "animation_stack_count": 3,
            "raw_animation_stack_count": 3,
            "effective_clip_count": 2,
            "technical_stack_count": 1,
            "duplicate_stack_count": 0,
            "stack_names": ["Running", "Walking", "Character_output.fbx"],
            "effective_clip_names": ["Running", "Walking"],
        }
        fbx["scene"] = {
            "handedness": "right",
            "up_axis": "Y",
            "front_axis": "-Z",
            "meters_per_unit": 0.01,
            "local_bounding_box": {"min": [-50, 0, -25], "max": [50, 170, 25]},
            "world_bounding_box": {"min": [-0.5, 0, -0.25], "max": [0.5, 1.7, 0.25]},
        }
        glb["geometry"].update(
            {
                "mesh_count": 1,
                "render_vertex_count": 85313,
                "unique_position_tuple_count": 54707,
                "explicit_index_count": 328737,
                "triangle_count": 109579,
            }
        )
        glb["animation"] = {
            "raw_animation_count": 2,
            "animation_count": 2,
            "effective_clip_count": 2,
            "technical_clip_count": 0,
            "duplicate_clip_count": 0,
            "clip_names": ["Running", "Walking"],
            "effective_clip_names": ["Running", "Walking"],
            "channel_count": 48,
            "sampler_count": 48,
            "duration_seconds": 2.0,
        }
        glb["spatial"] = {
            "coordinate_system": {
                "handedness": "right",
                "up_axis": "Y",
                "forward_axis": "-Z",
                "unit": "meter",
                "meters_per_unit": 1,
            },
            "position_bounds": {"min": [-0.5, 0, -0.25], "max": [0.5, 1.7, 0.25]},
            "node_transformed_bounds": {
                "min": [-0.005, 0, -0.0025],
                "max": [0.005, 0.017, 0.0025],
            },
        }

        result = compare_analyses(fbx, glb)
        candidate = result.comparison["cross_format_topology_candidate"]

        self.assertTrue(candidate["detected"])
        self.assertEqual(candidate["strength"], "strong_candidate_not_proven")
        self.assertEqual(result.input_a["animation_count"], 2)
        self.assertEqual(result.input_a["raw_animation_count"], 3)
        self.assertIn("2 effectif(s) (3 brut(s))", result.comparison_markdown)
        self.assertIn("Même nombre de triangles", result.interpreted_markdown)
        self.assertIn("Même ensemble de clips effectifs : Running, Walking", result.interpreted_markdown)
        self.assertIn("évaluer le bind pose", result.interpreted_markdown)

    def test_compares_json_and_generates_markdown(self):
        result = compare_analyses(fbx_json(), glb_json())
        markdown = result.comparison_markdown
        interpreted = result.interpreted_markdown
        payload = result.to_dict()

        self.assertIn("| Géométrie | Triangles | 50\u202f059 | 1\u202f957\u202f244 |", markdown)
        self.assertIn("B = 39.099 × A", markdown)
        self.assertIn("aucune équivalence directe", markdown)
        self.assertIn("algorithmes d’adaptateur différents", markdown)
        self.assertEqual(payload["schema"], "unified3d.analysis-comparison/1.0-rc1")
        self.assertEqual(payload["inputs"]["a"]["schema"], "unified3d.analysis/1.0-rc1")
        self.assertTrue(payload["comparison"]["same_mesh_count"])
        self.assertTrue(
            payload["comparison"]["index_transfer_ruled_out_by_triangle_count"]
        )
        self.assertEqual(
            payload["comparison"]["rig_donor_geometry_target_pattern"]["donor"],
            "a",
        )
        self.assertIn(
            "| Propriété | FBX animé | GLB haute définition | Interprétation |",
            interpreted,
        )
        self.assertIn(
            "| Triangles | 50\u202f059 | 1\u202f957\u202f244 | Le GLB est environ 39,10× plus dense |",
            interpreted,
        )
        self.assertIn(
            "| Influences maximales | 6 | 0 | 2 groupes `JOINTS_n` / `WEIGHTS_n`",
            interpreted,
        )
        self.assertIn("Les matériaux du GLB doivent être conservés", interpreted)

    def test_accepts_summaries_in_reverse_order(self):
        result = compare_analyses(GLB_SUMMARY, FBX_SUMMARY)
        markdown = result.comparison_markdown
        interpreted = result.interpreted_markdown
        payload = json.loads(result.to_json())

        self.assertIn("GLB", markdown)
        self.assertIn("FBX", markdown)
        self.assertEqual(
            payload["comparison"]["rig_donor_geometry_target_pattern"]["donor"],
            "b",
        )
        self.assertEqual(
            payload["comparison"]["rig_donor_geometry_target_pattern"]["target"],
            "a",
        )
        self.assertIn("| Propriété | FBX animé | GLB haute définition |", interpreted)
        self.assertIn("| Maillages | 10 | 10 |", interpreted)

    def test_specialized_interpretation_compares_two_fbx_files(self):
        converted = (
            FBX_SUMMARY.replace("thief-walking.fbx", "thief-converted.fbx")
            .replace("Control Points: 27311", "Control Points: 27312")
            .replace("Polygon Vertices: 150177", "Polygon Vertices: 150180")
            .replace("Polygons: 50059", "Polygons: 50060")
            .replace("Triangles (fan-equivalent): 50059", "Triangles (fan-equivalent): 50060")
            .replace("fd17e9faa2426bc8", "converted-signature")
        )

        interpreted = compare_analyses(FBX_SUMMARY, converted).interpreted_markdown

        self.assertIn("# Comparaison principale", interpreted)
        self.assertIn("**Mode : FBX ↔ FBX**", interpreted)
        self.assertIn("| Propriété | FBX A | FBX B | Interprétation |", interpreted)
        self.assertIn("| Control points | 27\u202f311 | 27\u202f312 |", interpreted)
        self.assertIn("Écart B − A : +1", interpreted)
        self.assertIn("Topologies différentes", interpreted)
        self.assertNotIn("Interprétation spécialisée indisponible", interpreted)

    def test_compares_two_glbs_with_decimation_and_texture_inventory(self):
        source = json.loads(glb_json())
        decimated = json.loads(glb_json())
        source["asset"].update(
            {"path": "C:\\assets\\goblin-2m.glb", "size_bytes": 117806172}
        )
        source["geometry"].update(
            {
                "mesh_count": 16,
                "mesh_instance_count": 16,
                "primitive_count": 16,
                "render_vertex_count": 1047168,
                "unique_position_tuple_count": 968570,
                "explicit_index_count": 5777619,
                "triangle_count": 1925873,
                "signatures": {"decoded_topology_sha256": "source-signature"},
            }
        )
        source["materials"] = {
            "material_count": 16,
            "texture_count": 48,
            "image_count": 48,
            "texture_encoded_bytes": 65169250,
            "texture_max_width": 4096,
            "texture_max_height": 4096,
            "texture_resolution_counts": {
                "4096x4096": 6,
                "2048x2048": 21,
                "1024x1024": 21,
            },
        }
        source["spatial"] = {
            "node_transformed_bounds": {
                "min": [-0.4, 0.0, -0.2],
                "max": [0.4, 0.84830597, 0.2],
            }
        }

        decimated["asset"].update(
            {"path": "C:\\assets\\goblin-150k.glb", "size_bytes": 33452216}
        )
        decimated["geometry"].update(
            {
                "mesh_count": 16,
                "mesh_instance_count": 16,
                "primitive_count": 16,
                "render_vertex_count": 104386,
                "unique_position_tuple_count": 78968,
                "explicit_index_count": 449973,
                "triangle_count": 149991,
                "signatures": {"decoded_topology_sha256": "decimated-signature"},
            }
        )
        decimated["materials"] = {
            "material_count": 16,
            "texture_count": 48,
            "image_count": 48,
            "texture_encoded_bytes": 29185302,
            "texture_max_width": 4096,
            "texture_max_height": 4096,
            "texture_resolution_counts": {
                "4096x4096": 2,
                "2048x2048": 9,
                "1024x1024": 16,
                "512x512": 14,
                "256x256": 7,
            },
        }
        decimated["spatial"] = {
            "node_transformed_bounds": {
                "min": [-0.4, 0.0, -0.2],
                "max": [0.4, 0.862941474, 0.2],
            }
        }

        result = compare_analyses(source, decimated)
        interpreted = result.interpreted_markdown

        self.assertIn("# Comparaison GLB → GLB", interpreted)
        self.assertIn("goblin-2m.glb", interpreted)
        self.assertIn("goblin-150k.glb", interpreted)
        self.assertIn("1\u202f925\u202f873", interpreted)
        self.assertIn("149\u202f991", interpreted)
        self.assertIn("7,79 % conservés", interpreted)
        self.assertIn("4096x4096 × 6", interpreted)
        self.assertIn("256x256 × 7", interpreted)
        self.assertIn("ce n’est pas une texture 2K uniforme", interpreted)
        self.assertIn("Topologie reconstruite", interpreted)
        self.assertTrue(result.comparison["topology_signatures_comparable"])
        self.assertFalse(result.comparison["topology_signature_match"])
        self.assertTrue(result.comparison["glb_pair"]["detected"])
        self.assertEqual(result.comparison["glb_pair"]["denser_input"], "a")
        self.assertAlmostEqual(
            result.comparison["glb_pair"]["retained_triangle_percent"],
            7.788,
            places=3,
        )
        texture_inventory = result.comparison["glb_pair"]["texture_inventory"]
        self.assertTrue(texture_inventory["available"])
        self.assertEqual(texture_inventory["b"]["resolution_counts"]["256x256"], 7)
        self.assertAlmostEqual(
            texture_inventory["encoded_byte_ratio_b_over_a"],
            29185302 / 65169250,
        )

    def test_to_dict_returns_isolated_data(self):
        result = compare_analyses(fbx_json(), glb_json())
        exported = result.to_dict()
        exported["inputs"]["a"]["asset"]["format"] = "unknown"

        self.assertEqual(result.input_a["kind"], "FBX")
        self.assertEqual(result.canonical_input_a["asset"]["format"], "fbx")

    def test_reports_which_input_is_invalid(self):
        with self.assertRaisesRegex(ValueError, "Analysis B"):
            compare_analyses(FBX_SUMMARY, "")


if __name__ == "__main__":
    unittest.main()
