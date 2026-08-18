# Unified3D Asset Processing Framework
## Spécification d’implémentation — Version 0.2.0 Draft

**Statut :** Architecture détaillée du Core, Runtime et Frontends  
**Date :** 2026-08-18  
**Version précédente :** 0.1.0  
**Nom de travail :** `Unified3D`  
**Orientation :** Open Source / Headless / Multi-format / Node-ready / API-first

---

# 0. Résumé exécutif

`Unified3D` est un framework open source de traitement 3D procédural, headless et multi-format.

Il vise à permettre :

- l’analyse de fichiers 3D ;
- la comparaison géométrique ;
- la conversion ;
- le traitement des matériaux et textures ;
- le transfert de skeleton et skin weights ;
- l’extraction et l’injection d’animations ;
- la validation ;
- l’optimisation ;
- l’automatisation en batch ;
- l’exposition des opérations sous forme de CLI, API Python, API TypeScript, nœuds ComfyUI et nœuds Workbench.

Le Core ne dépend d’aucun renderer.

```text
3D File
   ↓
Format Adapter
   ↓
Unified3D Core Asset
   ↓
Deterministic Operations
   ↓
Validation
   ↓
Format Adapter
   ↓
3D File
```

Le Workbench TypeScript ne manipule pas directement les millions de vertices d’un asset.

Il manipule des **handles** vers des objets conservés dans un runtime natif persistant :

```text
Workbench TypeScript
        ↓
@unified3d/client
        ↓
JSON-RPC 2.0
        ↓
Local IPC
        ↓
unified3d-runtime
        ↓
Unified3D C++ Core
```

Sous Windows :

```text
IPC = Named Pipe
```

Sous Linux/macOS :

```text
IPC = Unix Domain Socket
```

Le runtime natif est séparé du processus UI afin d’isoler :

- les SDK natifs ;
- les crashs de parsers ;
- la mémoire des gros assets ;
- les calculs longs ;
- les dépendances Autodesk / ufbx / glTF / USD ;
- le scheduling multi-thread.

---

# 1. Changements majeurs depuis la version 0.1

La version 0.2 formalise les points suivants.

## 1.1 Runtime natif persistant

Ajout de :

```text
unified3d-runtime
```

Processus headless natif exécutant le Core et conservant les assets chargés en mémoire.

---

## 1.2 Gateway RPC / IPC

Ajout d’une passerelle locale :

```text
JSON-RPC 2.0
+
Named Pipe / Unix Domain Socket
```

---

## 1.3 Handle Model

Les frontends utilisent des références légères :

```text
AssetHandle
SceneHandle
MeshHandle
SkeletonHandle
AnimationHandle
OperationHandle
```

et non des copies complètes de structures 3D.

---

## 1.4 TypeScript Client SDK

Ajout du package :

```text
@unified3d/client
```

pour le Workbench.

---

## 1.5 Core Object Model détaillé

Le Core est organisé autour d’environ 80 à 120 abstractions principales.

---

## 1.6 Operation Model détaillé

Une opération est indépendante d’un frontend.

```text
Operation != Node
```

---

## 1.7 Node Portability

ComfyUI et Workbench deviennent deux frontends exposant les mêmes opérations.

---

## 1.8 Native Data Preservation

Les données spécifiques FBX, glTF ou USD sont préservées dans des espaces natifs sans contaminer le modèle commun.

---

# 2. Vision du projet

Le projet doit permettre de traiter des assets 3D aussi simplement qu’un outil de traitement média en ligne de commande.

Analogie conceptuelle :

```text
FFmpeg
Video
→ Analyze
→ Filter
→ Convert
→ Encode
→ Save
```

devient :

```text
Unified3D
3D Asset
→ Analyze
→ Compare
→ Rig
→ Skin
→ Retarget
→ Optimize
→ Convert
→ Save
```

---

# 3. Non dépendance envers les DCC

`Unified3D` ne remplace pas les DCC.

Il automatise les traitements qui ne nécessitent pas une interaction humaine continue.

## 3.1 DCC

Utiles pour :

```text
Modeling
Sculpting
Manual Rigging
Weight Painting
UV Editing
Artistic Animation
Lighting
Rendering
Scene Authoring
```

## 3.2 Unified3D

Cible :

```text
Analysis
Batch Processing
Conversion
Validation
Geometry Processing
Topology Comparison
Skin Transfer
Animation Extraction
Animation Injection
Retargeting
Material Remapping
Texture Processing
Optimization
Automation
```

---

# 4. Principe fondamental

```text
Rendering != Processing
```

Le renderer est optionnel.

Le traitement d’un asset doit pouvoir fonctionner :

```text
on a server
without a screen
without a GPU
without a viewport
without a desktop session
```

---

# 5. Cas d’usage fondateur

## 5.1 Entrées

```text
Original GLB
├── haute qualité visuelle
├── matériaux corrects
├── textures correctes
└── géométrie de référence

Rigged FBX
├── skeleton Mixamo
├── skin weights
└── animation
```

## 5.2 Sortie

```text
Final GLB
├── géométrie originale
├── matériaux originaux
├── textures originales
├── skeleton du FBX
├── skin weights transférés
└── animations transférées
```

---

# 6. Pipeline fondateur

```text
[Load Original GLB]
        │
        ├──────────────────┐
        │                  │
        ▼                  ▼
[GLB Analyzer]       [Load Rigged FBX]
                           │
                           ▼
                      [FBX Analyzer]
                           │
        ┌──────────────────┘
        ▼
[Geometry Compatibility Comparator]
        │
        ▼
[Skeleton Compatibility]
        │
        ▼
[Skin Weight Transfer]
        │
        ▼
[Animation Transfer]
        │
        ▼
[GLB Validation]
        │
        ▼
[Save Final GLB]
```

---

# 7. Architecture générale v0.2

```text
Unified3D
│
├── Core
│   ├── Object Model
│   ├── Math
│   ├── Scene
│   ├── Geometry
│   ├── Materials
│   ├── Rigging
│   ├── Animation
│   ├── Resources
│   └── Diagnostics
│
├── Adapters
│   ├── FBX Autodesk
│   ├── FBX ufbx
│   ├── glTF / GLB
│   └── USD
│
├── Operations
│   ├── Analysis
│   ├── Comparison
│   ├── Geometry
│   ├── Materials
│   ├── Rigging
│   ├── Skinning
│   ├── Animation
│   ├── Conversion
│   └── Validation
│
├── Runtime
│   ├── Asset Registry
│   ├── Operation Scheduler
│   ├── Cache
│   ├── Resource Resolver
│   └── RPC Gateway
│
├── SDK
│   ├── C++
│   ├── Python
│   └── TypeScript
│
└── Frontends
    ├── CLI
    ├── ComfyUI
    ├── Workbench
    └── Optional Viewer
```

---

# 8. Layering strict

Les dépendances doivent suivre :

```text
Frontends
   ↓
Client SDK
   ↓
Runtime Gateway
   ↓
Operations
   ↓
Core
   ↓
Adapters / Native Libraries
```

Interdiction :

```text
Workbench
→ Autodesk FBX SDK directly
```

ou :

```text
ComfyUI node
→ embedded geometry algorithm
```

---

# 9. Core Object Model

Le Core contient les abstractions multi-format.

Il ne cherche pas à reproduire exactement :

```text
Autodesk FBX SDK
glTF JSON Schema
USD API
```

Il représente les concepts réellement nécessaires au traitement.

---

# 10. Famille Core

Abstractions proposées :

```text
ObjectId
BaseObject
NamedObject
Metadata
MetadataValue
Property
PropertyValue
PropertyType
ExtensionData
ExtensionCollection
ObjectReference
ObjectCollection
ObjectRegistry
Status
Diagnostic
DiagnosticList
Version
CapabilitySet
```

---

# 11. ObjectId

Type interne stable.

Exemple :

```text
ObjectId = 128-bit
```

Usage :

```text
identity
references
serialization
handles
graph links
cache
history
```

---

# 12. BaseObject

Concept :

```cpp
class BaseObject {
public:
    ObjectId id;
    Metadata metadata;
    ExtensionCollection extensions;
};
```

Invariant :

```text
id unique within a Runtime Asset Registry
```

---

# 13. NamedObject

```cpp
class NamedObject : public BaseObject {
public:
    std::string name;
};
```

---

# 14. Metadata

Métadonnées non critiques pour l’exécution.

Exemples :

```text
author
generator
source_format
source_application
creation_date
custom_tags
```

---

# 15. Property System

Un système de propriétés génériques est utile sans chercher à reproduire toute la mécanique dynamique FBX.

```text
Property
├── name
├── type
├── value
├── semantic
└── flags
```

Types :

```text
Boolean
Integer
Float
Double
String
Vector2
Vector3
Vector4
Quaternion
Matrix
Color
Binary
ObjectReference
Array
Dictionary
```

---

# 16. ExtensionData

But :

préserver des informations spécifiques à un format.

```text
ExtensionData
├── namespace
├── type
├── version
└── payload
```

Exemples :

```text
autodesk.fbx.native
khronos.gltf.extension
pixar.usd.metadata
```

---

# 17. Math Core

Types :

```text
Vector2f
Vector3f
Vector4f
Vector2d
Vector3d
Vector4d

Quaternionf
Quaterniond

Matrix3f
Matrix4f
Matrix3d
Matrix4d

Transform
BoundingBox
BoundingSphere
Plane
Ray
Color3
Color4
```

---

# 18. Precision Policy

Par défaut :

```text
asset transforms
→ double precision

vertex buffers
→ preserve source precision

normalized calculations
→ double precision
```

Le Core ne doit pas convertir automatiquement tout en float32.

---

# 19. Transform

```text
Transform
├── translation
├── rotation
├── scale
└── optional matrix override
```

Opérations :

```text
compose
decompose
inverse
multiply
applyPoint
applyVector
applyNormal
```

---

# 20. CoordinateSystem

```text
CoordinateSystem
├── handedness
├── up_axis
├── forward_axis
├── unit
└── unit_scale
```

Exemple :

```json
{
  "handedness": "right",
  "up_axis": "Y",
  "forward_axis": "-Z",
  "unit_scale": 1.0
}
```

---

# 21. CoordinateConverter

Responsabilités :

```text
axis conversion
unit conversion
winding correction
normal conversion
tangent conversion
skeleton transform conversion
animation transform conversion
```

---

# 22. Resource Core

Types :

```text
ResourceId
Resource
BinaryResource
ImageResource
ExternalResource
ResourceUri
ResourceResolver
ResourcePolicy
ResourceCache
```

---

# 23. ResourceResolver

Résout :

```text
relative path
absolute path
embedded binary
data URI
external texture
```

Policy par défaut :

```text
network access = false
```

---

# 24. Asset

Conteneur principal.

```text
Asset
├── metadata
├── coordinate_system
├── scenes
├── resources
├── materials
├── textures
├── skeletons
├── animations
└── native_data
```

---

# 25. Document

Optionnel pour formats pouvant contenir plusieurs documents/scènes.

```text
Document
├── assets
├── metadata
└── format_data
```

---

# 26. Scene

```text
Scene
├── root_nodes
├── default_camera
├── bounds
└── metadata
```

---

# 27. Node

```text
Node
├── parent
├── children
├── local_transform
├── world_transform_cache
└── components
```

---

# 28. NodeComponent

Interface commune pour :

```text
MeshComponent
CameraComponent
LightComponent
JointComponent
CustomComponent
```

---

# 29. Geometry Core

Types principaux :

```text
Geometry
Mesh
Primitive
Topology
VertexBuffer
IndexBuffer
VertexAttribute
AttributeSemantic
AttributeAccessor
UVSet
VertexColorSet
MorphTarget
GeometryBounds
GeometryStatistics
```

---

# 30. Geometry

```text
Geometry
├── bounds
├── source_space
├── native_data
└── statistics_cache
```

---

# 31. Mesh

```text
Mesh
├── primitives[]
├── morph_targets[]
├── bounds
└── metadata
```

---

# 32. Primitive

```text
Primitive
├── topology
├── vertex_buffer
├── index_buffer
├── material
└── native_data
```

---

# 33. PrimitiveTopology

```text
Points
Lines
LineStrip
Triangles
TriangleStrip
TriangleFan
```

---

# 34. VertexBuffer

Contient des attributs nommés.

```text
POSITION
NORMAL
TANGENT
TEXCOORD_0
TEXCOORD_1
COLOR_0
JOINTS_0
WEIGHTS_0
JOINTS_1
WEIGHTS_1
CUSTOM
```

---

# 35. VertexAttribute

```text
semantic
component_type
component_count
normalized
count
stride
data_view
```

---

# 36. IndexBuffer

```text
component_type
count
data_view
```

Types supportés :

```text
uint8
uint16
uint32
```

Conversion vers uint64 possible pour traitement interne.

---

# 37. BufferView

Le Core doit pouvoir référencer une zone mémoire sans copie.

```text
BufferView
├── resource
├── offset
├── length
├── stride
└── ownership
```

---

# 38. Memory Ownership

Valeurs possibles :

```text
Owned
Shared
Mapped
External
Temporary
```

---

# 39. GeometricVertex

Concept de sommet géométrique unique.

Important pour FBX :

```text
Fbx Control Point
```

---

# 40. RenderVertex

Concept de vertex effectivement utilisé par une primitive.

Un geometric vertex peut correspondre à plusieurs render vertices.

---

# 41. VertexMapping

Structure critique :

```text
VertexMapping
├── geometric_to_render[]
└── render_to_geometric[]
```

Usage :

```text
FBX Control Point
↔
glTF POSITION vertices
```

---

# 42. Topology

Analyse :

```text
vertex_count
triangle_count
edge_count
boundary_edge_count
connected_component_count
manifold_state
winding_state
degenerate_triangle_count
```

---

# 43. Geometry Signature

Types :

```text
RawTopologySignature
NormalizedTopologySignature
NormalizedGeometrySignature
SpatialGeometrySignature
```

---

# 44. Material Core

Types :

```text
Material
PBRMaterial
Texture
TextureSlot
ImageResource
Sampler
UVTransform
MaterialExtension
```

---

# 45. Material

Base :

```text
name
double_sided
alpha_mode
alpha_cutoff
native_data
```

---

# 46. PBRMaterial

```text
base_color
base_color_texture

metallic
roughness
metallic_roughness_texture

normal_texture
occlusion_texture
emissive_texture

emissive_factor
```

---

# 47. Texture

```text
image
sampler
uv_set
uv_transform
```

---

# 48. Rigging Core

Types :

```text
Skeleton
Joint
JointId
BindPose
RestPose
Skin
SkinBinding
InfluenceSet
SkinInfluence
SkeletonMap
SkeletonSignature
```

---

# 49. Skeleton

```text
Skeleton
├── root_joint
├── joints[]
├── bind_pose
└── rest_pose
```

---

# 50. Joint

```text
Joint
├── parent_joint
├── children
├── local_transform
├── bind_transform
├── inverse_bind_matrix
└── metadata
```

---

# 51. Skin

```text
Skin
├── skeleton
├── target_geometry
├── bindings
└── metadata
```

---

# 52. SkinInfluence

```text
geometric_vertex_id
joint_id
weight
```

---

# 53. InfluenceSet

```text
InfluenceSet
├── vertex_id
└── influences[]
```

Le Core supporte N influences.

Il ne force pas :

```text
4 influences
```

---

# 54. Influence Normalization

Modes :

```text
Preserve
Normalize
ClampAndNormalize
ReduceToN
```

---

# 55. BindPose

```text
joint_transforms[]
mesh_transform
coordinate_system
```

---

# 56. SkeletonSignature

Deux signatures :

```text
HierarchySignature
BindPoseSignature
```

Une troisième peut être ajoutée :

```text
SemanticSkeletonSignature
```

où certains noms de bones peuvent être normalisés.

---

# 57. Morph Core

Types :

```text
MorphTarget
MorphChannel
MorphWeight
MorphSet
```

---

# 58. MorphTarget

```text
position_deltas
normal_deltas
tangent_deltas
default_weight
```

---

# 59. Animation Core

Types :

```text
AnimationClip
AnimationChannel
AnimationSampler
AnimationCurve
Keyframe
InterpolationMode
Time
TimeSpan
FrameRate
TimeCode
AnimationTarget
```

---

# 60. AnimationClip

```text
name
start_time
end_time
duration
channels[]
metadata
```

---

# 61. AnimationChannel

```text
target
property
sampler
```

---

# 62. AnimationTarget

Types :

```text
NodeTranslation
NodeRotation
NodeScale
MorphWeights
CustomProperty
```

---

# 63. AnimationSampler

```text
times[]
values[]
interpolation
```

---

# 64. InterpolationMode

```text
Step
Linear
Cubic
Bezier
BackendSpecific
```

---

# 65. Camera / Light

Non prioritaires pour le MVP mais définis dans le Core.

Types :

```text
Camera
PerspectiveCamera
OrthographicCamera
Light
DirectionalLight
PointLight
SpotLight
AreaLight
```

---

# 66. Diagnostics Core

Types :

```text
Diagnostic
DiagnosticCode
DiagnosticSeverity
ValidationResult
OperationStatus
Statistics
Logger
TraceEvent
```

---

# 67. DiagnosticSeverity

```text
Trace
Info
Warning
Error
Fatal
```

---

# 68. Adapter Contract

Interface conceptuelle :

```cpp
class IAssetFormatAdapter {
public:
    virtual AdapterCapabilities capabilities() const = 0;

    virtual AssetHandle load(
        const AssetSource& source,
        const ImportOptions& options
    ) = 0;

    virtual void save(
        AssetHandle asset,
        const AssetDestination& destination,
        const ExportOptions& options
    ) = 0;
};
```

---

# 69. AdapterCapabilities

Exemple :

```text
Read
Write
Mesh
Materials
Textures
Skeleton
Skin
Animation
MorphTargets
Cameras
Lights
CustomProperties
EmbeddedResources
```

---

# 70. FBX Autodesk Adapter

Backend de référence optionnel.

```text
FBX
↓
Autodesk FBX SDK
↓
AutodeskFbxAdapter
↓
Unified3D Core
```

Repository :

```text
no Autodesk headers
no Autodesk libs
no Autodesk DLL
```

Le SDK Autodesk est installé séparément.

---

# 71. FBX ufbx Adapter

Backend open source.

```text
FBX
↓
ufbx
↓
UfbxAdapter
↓
Unified3D Core
```

Objectif :

```text
no proprietary runtime dependency
```

---

# 72. Backend Cross Validation

```text
same FBX
├── Autodesk Adapter
└── ufbx Adapter
       ↓
Unified Assets
       ↓
Backend Comparison
```

---

# 73. glTF / GLB Adapter

Le MVP peut utiliser :

```text
Node.js
+
@gltf-transform/core
```

comme processus/outillage spécialisé.

Le Core ne doit pas dépendre de Node.js.

À terme :

```text
Native C++ glTF backend
```

peut être ajouté.

---

# 74. USD Adapter

Prévu mais non MVP.

Contrat à réserver :

```text
UsdAdapter
```

---

# 75. Operation Model

Une opération est une unité métier indépendante des frontends.

```text
Operation
├── id
├── version
├── input schema
├── output schema
├── parameter schema
├── execution policy
├── diagnostics
└── capabilities
```

---

# 76. IOperation

Interface conceptuelle :

```cpp
class IOperation {
public:
    virtual OperationDescriptor descriptor() const = 0;

    virtual OperationResult execute(
        const OperationContext& context,
        const OperationInput& input
    ) = 0;
};
```

---

# 77. OperationContext

Contient :

```text
asset registry
resource resolver
logger
cache
cancellation token
progress reporter
thread pool
temporary storage
security policy
```

---

# 78. OperationResult

```text
status
outputs
diagnostics
statistics
duration
cache_status
```

---

# 79. Operation Categories

```text
Analysis
Comparison
Geometry
Materials
Rigging
Skinning
Animation
Conversion
Optimization
Validation
Serialization
Utility
```

---

# 80. Analysis Operations

MVP :

```text
AnalyzeAsset
AnalyzeGeometry
AnalyzeTopology
AnalyzeMaterials
AnalyzeSkeleton
AnalyzeSkin
AnalyzeAnimation
```

---

# 81. Comparison Operations

```text
CompareAssets
CompareGeometry
CompareTopology
CompareSkeleton
CompareSkin
CompareMaterials
CompareAnimations
```

---

# 82. Geometry Compatibility Result

Classification :

```text
EXACT_TOPOLOGY_MATCH
DIRECT_SKIN_TRANSFER_COMPATIBLE

GEOMETRY_MATCH
GEOMETRIC_VERTEX_MAPPING_REQUIRED

SPATIAL_MATCH
SPATIAL_SKIN_TRANSFER_REQUIRED

SIMILAR_GEOMETRY
ADVANCED_TRANSFER_REQUIRED

INCOMPATIBLE
```

---

# 83. Comparison Score

Valeur :

```text
0.0 → 1.0
```

Sous-scores :

```text
bounds_score
triangle_score
topology_score
spatial_score
skeleton_score
```

---

# 84. Geometry Normalization Operation

Pipeline :

```text
clone working representation
↓
apply node transforms
↓
convert coordinate system
↓
convert units
↓
normalize winding
↓
canonicalize vertex positions
↓
canonicalize topology
```

L’asset source reste inchangé.

---

# 85. Direct Skin Transfer

Conditions :

```text
same topology
same geometric vertex mapping
compatible skeleton
```

---

# 86. Geometric Vertex Skin Transfer

Cas :

```text
FBX control point count
!=
glTF render vertex count
```

mais :

```text
same geometric surface
```

Mapping :

```text
source geometric vertex
→
target render vertices[]
```

---

# 87. Spatial Skin Transfer

Méthode recommandée :

```text
target vertex
↓
nearest source triangle
↓
barycentric coordinates
↓
interpolate source skin weights
↓
normalize
```

---

# 88. Skin Transfer Quality Levels

```text
Fast
Balanced
Precise
Diagnostic
```

---

# 89. Animation Extraction

Sortie commune :

```text
UnifiedAnimationData
```

---

# 90. Animation Injection

Validation préalable :

```text
skeleton hierarchy
joint map
rest pose
target properties
time base
```

---

# 91. Retargeting

Interfaces réservées :

```text
SkeletonMap
RetargetProfile
RetargetRules
RetargetOperation
```

Implémentation complète hors MVP initial.

---

# 92. Runtime Architecture

Le runtime est un processus natif persistant.

Nom :

```text
unified3d-runtime
```

Windows :

```text
unified3d-runtime.exe
```

---

# 93. Responsabilités du Runtime

```text
load native libraries
own assets
own buffers
maintain object registry
execute operations
schedule jobs
manage caches
emit progress
handle cancellation
validate resource policies
expose RPC API
```

---

# 94. Asset Registry

Structure centrale :

```text
AssetRegistry
├── AssetHandle → Asset
├── MeshHandle → Mesh
├── SkeletonHandle → Skeleton
├── AnimationHandle → Animation
└── OperationHandle → OperationState
```

---

# 95. Handle Model

Un handle frontend est léger.

Exemple TypeScript :

```typescript
interface AssetHandle {
    id: string;
    kind: "3D_ASSET";
    format?: string;
}
```

Exemple :

```json
{
  "id": "asset:000000000014",
  "kind": "3D_ASSET",
  "format": "FBX"
}
```

---

# 96. Handle Types

```text
AssetHandle
SceneHandle
NodeHandle
MeshHandle
PrimitiveHandle
MaterialHandle
TextureHandle
SkeletonHandle
SkinHandle
AnimationHandle
AnalysisHandle
ComparisonHandle
ValidationHandle
OperationHandle
```

---

# 97. Handle Lifetime

États :

```text
Alive
Released
Expired
Invalid
```

---

# 98. Handle Ownership

Par défaut :

```text
Runtime owns native object
Frontend owns reference
```

Le frontend peut demander :

```text
retain
release
```

---

# 99. Automatic Handle Cleanup

Le runtime doit pouvoir libérer :

```text
unused assets
temporary assets
expired operation results
cache entries
```

selon politiques configurables.

---

# 100. Runtime Session

Chaque client possède :

```text
SessionId
```

Un handle peut être :

```text
session scoped
global cache scoped
```

---

# 101. RPC

RPC signifie :

```text
Remote Procedure Call
```

Ici, le mot Remote signifie :

```text
execution occurs outside the caller process
```

même si la communication reste locale.

---

# 102. IPC

IPC signifie :

```text
Inter-Process Communication
```

C’est le canal entre :

```text
Workbench process
↔
Unified3D Runtime process
```

---

# 103. RPC / IPC Stack

```text
Workbench
↓
TypeScript SDK
↓
JSON-RPC 2.0
↓
IPC Transport
↓
Named Pipe / Unix Domain Socket
↓
Runtime Gateway
↓
Operation Dispatcher
↓
Unified3D Core
```

---

# 104. Windows IPC

Par défaut :

```text
Named Pipe
```

Nom proposé :

```text
\\.\pipe\Unified3D.Runtime.v1
```

---

# 105. Linux / macOS IPC

Utiliser :

```text
Unix Domain Socket
```

Exemple :

```text
/tmp/unified3d-runtime-v1.sock
```

ou emplacement utilisateur sécurisé.

---

# 106. Pourquoi pas HTTP par défaut

Éviter :

```text
localhost:port
```

pour le runtime local principal.

Raisons :

```text
unnecessary network surface
port conflicts
firewall complexity
authentication overhead
less explicit local-only behavior
```

---

# 107. JSON-RPC 2.0

Choisi initialement pour :

```text
simple protocol
human-readable diagnostics
easy TypeScript integration
easy Python integration
request/response model
notifications/events
versionable method namespace
```

---

# 108. RPC Namespace

Exemples :

```text
runtime.*
asset.*
geometry.*
material.*
skeleton.*
skin.*
animation.*
validation.*
operation.*
resource.*
cache.*
```

---

# 109. RPC Example — Asset Load

Requête :

```json
{
  "jsonrpc": "2.0",
  "id": 12,
  "method": "asset.load",
  "params": {
    "path": "C:/Models/character.glb"
  }
}
```

Réponse :

```json
{
  "jsonrpc": "2.0",
  "id": 12,
  "result": {
    "asset": {
      "id": "asset:000014",
      "kind": "3D_ASSET",
      "format": "GLB"
    }
  }
}
```

---

# 110. RPC Example — Geometry Analyze

```json
{
  "jsonrpc": "2.0",
  "id": 13,
  "method": "geometry.analyze",
  "params": {
    "asset": "asset:000014"
  }
}
```

---

# 111. Long Running Operations

Les opérations longues renvoient immédiatement :

```text
OperationHandle
```

Exemple :

```json
{
  "operation": {
    "id": "operation:000291",
    "state": "running"
  }
}
```

---

# 112. Progress Events

Notification :

```json
{
  "jsonrpc": "2.0",
  "method": "operation.progress",
  "params": {
    "operation": "operation:000291",
    "progress": 0.46,
    "phase": "Spatial Vertex Mapping"
  }
}
```

---

# 113. Operation States

```text
Queued
Running
Completed
Failed
Cancelled
```

---

# 114. Cancellation

RPC :

```text
operation.cancel
```

Paramètre :

```text
operation handle
```

---

# 115. Runtime Crash Isolation

Architecture :

```text
Workbench
       │
       X
Runtime crash
```

Le Workbench doit rester vivant.

Il peut proposer :

```text
Restart Runtime
Reconnect
Reload Assets
```

---

# 116. Runtime Recovery

Version future :

```text
session manifest
asset reload list
operation history
cache recovery
```

---

# 117. TypeScript Client SDK

Package :

```text
@unified3d/client
```

---

# 118. TypeScript SDK Structure

```text
packages/
└── unified3d-client/
    ├── src/
    │   ├── transport/
    │   │   ├── ITransport.ts
    │   │   ├── NamedPipeTransport.ts
    │   │   └── UnixSocketTransport.ts
    │   │
    │   ├── rpc/
    │   │   ├── JsonRpcClient.ts
    │   │   ├── RpcError.ts
    │   │   └── RpcEvent.ts
    │   │
    │   ├── clients/
    │   │   ├── Unified3DClient.ts
    │   │   ├── AssetClient.ts
    │   │   ├── GeometryClient.ts
    │   │   ├── SkeletonClient.ts
    │   │   ├── SkinClient.ts
    │   │   ├── AnimationClient.ts
    │   │   ├── ValidationClient.ts
    │   │   └── OperationClient.ts
    │   │
    │   └── types/
    │       ├── Handles.ts
    │       ├── Analysis.ts
    │       ├── Comparison.ts
    │       ├── Operations.ts
    │       └── Diagnostics.ts
    │
    └── package.json
```

---

# 119. TypeScript API Style

Préférer :

```typescript
client.asset.load()
client.asset.save()

client.geometry.analyze()
client.geometry.compare()

client.skeleton.analyze()
client.skeleton.compare()

client.skin.transfer()

client.animation.extract()
client.animation.inject()

client.validation.validate()
```

Éviter une exposition brute de centaines de classes C++.

---

# 120. TypeScript Example

```typescript
const original = await client.asset.load(
    "character-original.glb"
);

const rigged = await client.asset.load(
    "character-rigged.fbx"
);

const compatibility =
    await client.geometry.compare(
        rigged,
        original
    );
```

---

# 121. TypeScript Skin Transfer Example

```typescript
const result = await client.skin.transfer({
    source: rigged,
    target: original,
    method: compatibility.recommendedMethod
});
```

---

# 122. Python SDK

Deux modes possibles.

## Mode A — Runtime Client

```text
Python
↓
JSON-RPC / IPC
↓
Unified3D Runtime
```

## Mode B — Native Binding

```text
Python
↓
pybind11
↓
Unified3D Core
```

Le mode Runtime Client est recommandé pour ComfyUI afin de partager le même moteur que Workbench.

---

# 123. CLI

Le CLI peut :

```text
connect to runtime
```

ou :

```text
launch ephemeral runtime
```

Exemples :

```bash
unified3d analyze character.fbx

unified3d compare rigged.fbx original.glb

unified3d transfer-skin rigged.fbx original.glb -o output.glb
```

---

# 124. ComfyUI Frontend

Architecture cible :

```text
ComfyUI Node
↓
Unified3D Python Client
↓
RPC / IPC
↓
Unified3D Runtime
```

---

# 125. Workbench Frontend

Architecture :

```text
Workbench Node
↓
@unified3d/client
↓
Unified3D Runtime
```

---

# 126. Operation != Node

Règle absolue :

```text
Node
=
UI wrapper

Operation
=
business logic
```

---

# 127. Canonical Socket Types

```text
3D_ASSET
3D_SCENE
3D_NODE
3D_MESH
3D_PRIMITIVE
3D_MATERIAL
3D_TEXTURE
3D_SKELETON
3D_SKIN
3D_ANIMATION
3D_TRANSFORM
3D_ANALYSIS
3D_COMPARISON
3D_VALIDATION
3D_OPERATION
```

---

# 128. Socket Payload

Par défaut :

```text
Handle
```

et non données lourdes.

Exemple :

```text
3D_ASSET socket
→ AssetHandle
```

---

# 129. Lightweight Result Payloads

Les résultats analytiques peuvent être transmis directement :

```text
counts
scores
strings
small JSON objects
```

Les buffers lourds restent côté runtime.

---

# 130. Large Data Transfer

Si un frontend demande réellement des données lourdes :

options futures :

```text
shared memory
memory mapped file
binary IPC stream
temporary binary resource
```

Éviter le JSON pour :

```text
millions of vertices
textures
large animation buffers
```

---

# 131. Shared Memory Future Extension

Architecture possible :

```text
RPC control plane
+
Shared Memory data plane
```

RPC transmet :

```text
buffer handle
offset
length
format
```

---

# 132. Runtime Capability Negotiation

À la connexion :

```text
runtime.hello
```

réponse :

```json
{
  "runtime_version": "0.2.0",
  "protocol_version": "1.0",
  "capabilities": [
    "fbx.autodesk.read",
    "gltf.read",
    "gltf.write",
    "geometry.compare"
  ]
}
```

---

# 133. Protocol Versioning

Séparer :

```text
Core Version
Runtime Version
RPC Protocol Version
Schema Version
Adapter Version
Operation Version
```

---

# 134. Operation Descriptor

Chaque opération expose :

```json
{
  "id": "geometry.compare",
  "version": "1.0.0",
  "inputs": [
    "3D_ASSET",
    "3D_ASSET"
  ],
  "outputs": [
    "3D_COMPARISON"
  ]
}
```

---

# 135. Dynamic Node Generation Future

Le Workbench pourra potentiellement générer automatiquement une partie des nœuds à partir des descriptors d’opérations.

```text
Operation Descriptor
↓
Node Definition
```

---

# 136. Plugin Architecture

À terme :

```text
Unified3D Plugin
├── metadata
├── required core version
├── operations
├── adapters
└── capabilities
```

---

# 137. Plugin ABI

Ne pas figer en 0.2.

Prévoir deux niveaux :

```text
Stable C ABI
ou
Out-of-process plugin protocol
```

Pour les plugins risqués, préférer l’isolation out-of-process.

---

# 138. Runtime Adapter Loading

Exemple :

```text
runtime
├── core
├── fbx-autodesk adapter
├── fbx-ufbx adapter
├── gltf adapter
└── usd adapter
```

Les adapters peuvent être activés selon disponibilité.

---

# 139. Autodesk Backend Detection

Au démarrage :

```text
detect FBX SDK
detect runtime DLL
register capability
```

Si absent :

```text
fbx.autodesk = unavailable
```

Le backend ufbx peut rester disponible.

---

# 140. Security Model

Le runtime lit des fichiers potentiellement non fiables.

Prévoir :

```text
buffer bounds validation
max file size
max allocation
max recursion depth
URI restrictions
network disabled by default
path normalization
temporary file isolation
timeout policy
```

---

# 141. Resource Policy

```text
LocalOnly
AllowRelative
AllowAbsolute
AllowNetwork
```

Par défaut :

```text
LocalOnly + Relative
```

---

# 142. Operation Sandbox Future

Pour certains adapters/plugins :

```text
worker process
```

peut être utilisé.

---

# 143. Caching

Clé :

```text
input hash
+
adapter version
+
operation version
+
parameters
```

---

# 144. Analysis Cache

Exemple :

```text
FBX analysis
```

n’a pas besoin d’être recalculée si :

```text
file unchanged
adapter unchanged
analysis schema unchanged
```

---

# 145. Runtime Asset Cache

Éviter de parser plusieurs fois le même fichier dans un graph.

```text
Load once
↓
AssetHandle reused by many nodes
```

---

# 146. Graph Execution

Le Workbench peut envoyer :

```text
individual operation calls
```

ou plus tard :

```text
operation graph batch
```

---

# 147. OperationGraph

```text
OperationGraph
├── nodes
├── typed edges
├── constants
├── dependencies
└── execution options
```

---

# 148. Graph Scheduler

Responsabilités :

```text
dependency resolution
parallel scheduling
cache reuse
cancellation
error propagation
progress aggregation
```

---

# 149. Parallelism

Exemples :

```text
Analyze FBX
||
Analyze GLB
```

puis :

```text
Compare
```

---

# 150. Threading Policy

Core :

```text
no mutable globals
operation-local state
thread-safe registries
thread-safe cache
controlled worker pool
```

---

# 151. Memory Strategy

Priorités :

```text
avoid copies
preserve native buffers
lazy decoding
memory mapping when possible
copy-on-write
shared immutable views
```

---

# 152. Batch Processing

```text
Asset[]
↓
Operation
↓
Result[]
```

Options :

```text
concurrency
fail_fast
continue_on_error
cache
```

---

# 153. Validation Model

Chaque validation retourne :

```text
ValidationResult
```

avec diagnostics.

---

# 154. Validation Examples

```text
MISSING_POSITION
INVALID_INDEX
NAN_VERTEX
ZERO_AREA_TRIANGLE
MISSING_JOINT
INVALID_WEIGHT_SUM
INVALID_BIND_MATRIX
UNSUPPORTED_EXTENSION
MISSING_TEXTURE
BROKEN_RESOURCE_URI
```

---

# 155. Conversion Loss Reporting

Toute conversion doit pouvoir indiquer :

```text
Preserved
Converted
Approximated
Discarded
Unsupported
```

---

# 156. Conversion Report Example

```json
{
  "geometry": {
    "status": "preserved"
  },
  "materials": {
    "status": "converted"
  },
  "constraints": {
    "status": "unsupported",
    "count": 3
  }
}
```

---

# 157. Unified Analysis Schema v1

Structure proposée :

```json
{
  "schema": "unified3d.analysis/1.0",
  "asset": {},
  "geometry": {},
  "materials": {},
  "skeleton": {},
  "skin": {},
  "animation": {},
  "native": {}
}
```

---

# 158. Geometry Analysis Fields

```text
mesh_count
primitive_count
geometric_vertex_count
render_vertex_count
index_count
triangle_count
ngon_count
uv_set_count
normal_count
tangent_count
color_attribute_count
bounds
signatures
```

---

# 159. Skeleton Analysis Fields

```text
present
joint_count
root_joint
hierarchy_signature
bind_pose_signature
joint_names
```

---

# 160. Skin Analysis Fields

```text
present
skin_count
skinned_vertex_count
max_influences
average_influences
min_weight_sum
max_weight_sum
average_weight_sum
```

---

# 161. Animation Analysis Fields

```text
animation_count
clip_names
channel_count
duration
target_count
```

---

# 162. Native FBX Fields

```text
control_point_count
polygon_vertex_count
skin_deformer_count
cluster_count
fbx_version
```

---

# 163. Native glTF Fields

```text
accessor_count
buffer_view_count
buffer_count
primitive_count
skin_count
extension_list
```

---

# 164. First Real FBX Fixture

Référence existante :

```text
FBX Version                : 7.7.0
Meshes                     : 10
Control Points             : 27 311
Polygon Vertices           : 150 177
Polygons                   : 50 059
Triangles                  : 50 059
UV Sets                    : 10
Materials                  : 1
Skeleton Bones             : 41
Skins                      : 10
Clusters                   : 75
Max Influences / CP        : 6
Animations                 : 1
Topology Signature         : fd17e9faa2426bc8
```

---

# 165. MVP 0.1 — Analysis

Composants :

```text
FBX Geometry Rig Analyzer
GLB Geometry Rig Analyzer
Unified Analysis Schema
Geometry Compatibility Comparator
```

---

# 166. MVP 0.2 — Rig Transfer

Composants :

```text
Skeleton Comparator
Vertex Mapping
Skin Weight Transfer
GLB Skin Writer
```

---

# 167. MVP 0.3 — Animation

```text
Unified Animation Schema
Animation Extract
Animation Inject
Animation Validate
```

---

# 168. MVP 0.4 — Founding Workflow

```text
Original GLB
+
Rigged FBX
↓
Final Rigged Animated GLB
```

---

# 169. Workbench Integration Milestone

Validé si :

```text
Workbench TypeScript
↓
@unified3d/client
↓
RPC/IPC
↓
Unified3D Runtime
↓
Geometry Analyze
```

fonctionne sans charger de géométrie lourde dans le processus UI.

---

# 170. ComfyUI Migration Milestone

Les nœuds existants deviennent progressivement :

```text
thin wrappers
```

autour de :

```text
Unified3D operations
```

---

# 171. Repository Structure v0.2

```text
Unified3D/
│
├── README.md
├── LICENSE
├── CONTRIBUTING.md
├── SECURITY.md
│
├── docs/
│   ├── architecture/
│   ├── specifications/
│   ├── protocols/
│   ├── formats/
│   └── operations/
│
├── core/
│   ├── object/
│   ├── math/
│   ├── resources/
│   ├── scene/
│   ├── geometry/
│   ├── materials/
│   ├── rigging/
│   ├── animation/
│   └── diagnostics/
│
├── adapters/
│   ├── fbx-autodesk/
│   ├── fbx-ufbx/
│   ├── gltf/
│   └── usd/
│
├── operations/
│   ├── analysis/
│   ├── comparison/
│   ├── geometry/
│   ├── materials/
│   ├── rigging/
│   ├── skinning/
│   ├── animation/
│   ├── conversion/
│   └── validation/
│
├── runtime/
│   ├── registry/
│   ├── scheduler/
│   ├── cache/
│   ├── gateway/
│   └── transport/
│
├── sdk/
│   ├── cpp/
│   ├── python/
│   └── typescript/
│
├── frontends/
│   ├── cli/
│   ├── comfyui/
│   └── workbench/
│
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── regression/
│   └── assets/
│
└── tools/
```

---

# 172. Language Strategy

Core :

```text
C++20
```

Runtime :

```text
C++20
```

TypeScript SDK :

```text
TypeScript
```

Python SDK :

```text
Python
```

ComfyUI :

```text
Python
```

Workbench :

```text
TypeScript
```

---

# 173. C++ Library Split

Proposition :

```text
unified3d-core
unified3d-geometry
unified3d-rigging
unified3d-animation
unified3d-operations
unified3d-runtime
```

Adapters :

```text
unified3d-adapter-fbx-autodesk
unified3d-adapter-fbx-ufbx
unified3d-adapter-gltf
```

---

# 174. Build System

CMake recommandé pour le natif.

```text
CMake
+
CMakePresets.json
```

---

# 175. Dependency Policy

Préférer :

```text
small
auditable
optional
adapter-contained
```

Les dépendances propriétaires restent facultatives.

---

# 176. Open Source Boundary

Le dépôt ne doit contenir aucun code Autodesk.

Autorisé :

```text
our adapter source
build detection
runtime loader
documentation
```

Non inclus :

```text
fbxsdk.h
Autodesk .lib
Autodesk .dll
Autodesk source
```

---

# 177. License

Décision finale à prendre avant première release publique.

Candidats :

```text
Apache-2.0
MIT
BSD-3-Clause
```

---

# 178. Testing Strategy

## Unit

```text
math
transforms
coordinate conversion
weight normalization
topology signatures
buffer access
```

## Integration

```text
FBX load
GLB load
analysis
comparison
skin transfer
animation transfer
```

## Regression

Chaque bug produit une fixture.

---

# 179. Cross-format Test

Pour un asset connu :

```text
FBX
GLB
```

Comparer :

```text
bounds
triangles
geometric vertices
materials
skeleton
skin
animation
```

---

# 180. Cross-backend Test

```text
Autodesk FBX
vs
ufbx
```

Objectif :

```text
Unified representation equivalence
```

---

# 181. Performance Metrics

Mesurer :

```text
startup_time
parse_time
analysis_time
comparison_time
transfer_time
write_time
peak_ram
cpu_time
cache_hit_rate
```

---

# 182. Startup Performance

Objectif :

```text
Runtime start << DCC application start
```

Le runtime doit être conçu comme un service léger.

---

# 183. Warm Runtime

Après démarrage :

```text
adapters loaded
thread pool ready
cache ready
```

Les appels suivants doivent éviter les initialisations lourdes.

---

# 184. Viewer

Module strictement optionnel.

Peut utiliser :

```text
Three.js
Babylon.js
Direct3D
WebGPU
```

mais aucun module du Core ne dépend de lui.

---

# 185. Preview Operation

Le Workbench peut demander :

```text
asset preview
```

Le runtime peut éventuellement produire :

```text
thumbnail
low-poly preview
temporary GLB
```

sans transformer le Core en renderer.

---

# 186. Future Web / Remote Runtime

Le protocole RPC étant abstrait du transport, un futur runtime distant est possible.

```text
Local:
JSON-RPC over IPC

Future:
RPC over secure network transport
```

Mais ce n’est pas le mode par défaut.

---

# 187. Transport Interface

```typescript
interface ITransport {
    connect(): Promise<void>;
    send(payload: Uint8Array): Promise<void>;
    close(): Promise<void>;
}
```

---

# 188. Future Protocol Upgrade

Si JSON-RPC devient limitant :

```text
control plane remains RPC
binary payload moves to shared memory or binary framing
```

On évite de réécrire l’API métier.

---

# 189. JSON-RPC Scope

JSON-RPC transporte :

```text
method
parameters
handles
small results
diagnostics
events
```

Pas :

```text
huge geometry buffers
textures
gigabyte assets
```

---

# 190. Native Handle Safety

Un handle doit être validé par :

```text
session
type
generation
object id
```

afin d’éviter les références vers des objets libérés.

---

# 191. Handle Encoding Future

Exemple interne :

```text
asset:<session>:<generation>:<id>
```

---

# 192. Error Model RPC

Exemple :

```json
{
  "code": "SKELETON_INCOMPATIBLE",
  "message": "Target skeleton is missing required joints.",
  "details": {
    "missing_joints": [
      "mixamorig:LeftHandIndex1"
    ]
  }
}
```

---

# 193. RPC Error Categories

```text
ProtocolError
InvalidRequest
InvalidHandle
AssetError
AdapterError
OperationError
ValidationError
ResourceError
InternalError
```

---

# 194. Logging Separation

Runtime logs :

```text
native diagnostic logs
```

Frontend logs :

```text
UI presentation
```

Le client peut s’abonner aux événements.

---

# 195. AI Integration

L’IA ne modifie pas directement les buffers.

Elle peut :

```text
plan operation graph
suggest parameters
explain diagnostics
choose transfer strategy
route assets
generate workflows
```

Les opérations sont exécutées par le moteur déterministe.

---

# 196. AI Planner Flow

```text
User Intent
↓
AI Planner
↓
Operation Graph
↓
Validation
↓
Unified3D Runtime
↓
Deterministic Results
```

---

# 197. Workbench Node Generation

Le Workbench pourra associer :

```text
OperationDescriptor
+
Node UI Definition
```

pour générer des nœuds cohérents.

---

# 198. Node Metadata

Exemple :

```json
{
  "operation": "geometry.compare",
  "category": "3D/Analysis",
  "title": "Geometry Compatibility Comparator"
}
```

---

# 199. Portage ComfyUI → Workbench

Le portage idéal devient :

```text
existing operation
+
new frontend wrapper
```

et non :

```text
rewrite Python node logic in TypeScript
```

---

# 200. Règles architecturales figées v0.2

```text
[DECISION]
Core = C++20

[DECISION]
Runtime = separate native process

[DECISION]
Workbench = TypeScript frontend

[DECISION]
Workbench ↔ Runtime = RPC over local IPC

[DECISION]
Initial RPC = JSON-RPC 2.0

[DECISION]
Windows IPC = Named Pipe

[DECISION]
Linux/macOS IPC = Unix Domain Socket

[DECISION]
Assets remain in native runtime memory

[DECISION]
Frontends manipulate handles

[DECISION]
Heavy buffers are not serialized to JSON

[DECISION]
Operation != Node

[DECISION]
ComfyUI and Workbench share operations

[DECISION]
Renderer optional

[DECISION]
Native format data preserved

[DECISION]
Autodesk SDK backend optional and external
```

---

# 201. Questions ouvertes pour la version 0.3

```text
Exact C++ namespace layout
Object ownership implementation
Stable C ABI
Plugin ABI
Shared memory transport
Binary RPC framing
glTF native C++ backend selection
USD backend selection
Internal binary asset format
Operation graph serialization
Asset persistence between runtime restarts
Workbench node schema
Retargeting semantic bone model
GPU compute acceleration
Distributed runtime
```

---

# 202. Roadmap immédiate recommandée

Avant d’implémenter le framework complet :

```text
1. Stabiliser FBX Analyzer Autodesk
2. Implémenter GLB Analyzer
3. Définir Unified Analysis Schema 1.0
4. Implémenter Geometry Comparator
5. Valider les différences réelles FBX / GLB
6. Écrire les premières classes Core uniquement à partir des besoins observés
7. Prototyper unified3d-runtime
8. Prototyper JSON-RPC via Named Pipe
9. Créer @unified3d/client
10. Brancher un premier nœud Workbench
```

---

# 203. Premier prototype Runtime recommandé

Méthodes minimales :

```text
runtime.hello
runtime.shutdown

asset.load
asset.release

geometry.analyze

operation.status
operation.cancel
```

---

# 204. Premier prototype TypeScript recommandé

```typescript
const client = await Unified3DClient.connect();

const asset = await client.asset.load(
    "C:/Models/thief-walking.fbx"
);

const analysis =
    await client.geometry.analyze(asset);

console.log(analysis.geometry.triangleCount);
```

---

# 205. Critère de réussite Runtime v0

Le prototype est validé si :

```text
Workbench TypeScript starts
↓
Runtime starts
↓
Named Pipe connects
↓
Asset loads
↓
FBX analysis runs
↓
Analysis returns
↓
Workbench remains responsive
```

---

# 206. Critère de réussite globale v0.2

La spécification v0.2 est considérée suffisamment cadrée pour commencer l’implémentation du Core lorsque :

```text
Unified Analysis Schema
Geometry Comparator contract
Handle model
RPC contract
Runtime lifetime
TypeScript client contract
Operation model
```

sont stabilisés.

---

# 207. Conclusion

`Unified3D` n’est plus seulement défini comme une bibliothèque de conversion 3D.

La version 0.2 le définit comme une **infrastructure complète de traitement 3D headless**.

Architecture finale visée :

```text
                         Unified3D
                            │
                    Native C++ Core
                            │
            ┌───────────────┼────────────────┐
            │               │                │
       FBX Adapter      glTF Adapter     USD Adapter
            │
      Autodesk / ufbx
                            │
                            ▼
                   Unified3D Runtime
                            │
                     RPC / IPC Gateway
                            │
           ┌────────────────┼────────────────┐
           │                │                │
           ▼                ▼                ▼
      TypeScript SDK    Python SDK          CLI
           │                │
           ▼                ▼
       Workbench          ComfyUI
```

Le renderer reste extérieur :

```text
Unified3D Core
    │
    └── Optional Viewer
```

Le projet doit pouvoir réaliser des opérations complexes sur des fichiers 3D sans ouvrir un DCC, sans initialiser un environnement de rendu et sans transférer inutilement de gigantesques buffers dans les interfaces utilisateur.

Le cas fondateur reste :

```text
Original High-Quality GLB
+
Rigged / Animated FBX
↓
Analyze
↓
Compare
↓
Transfer Skin
↓
Transfer Animation
↓
Validate
↓
High-Quality Rigged Animated GLB
```

Cette chaîne doit démontrer que des workflows 3D traditionnellement réalisés dans des applications lourdes peuvent être convertis en opérations rapides, déterministes, scriptables et composables sous forme de nœuds.
