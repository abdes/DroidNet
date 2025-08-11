# Data Module Test Enhancement Plan

Tracking table for improving test coverage, quality, and style consistency in `src/Oxygen/Data`.

Legend:

- Priority: P1 (critical correctness), P2 (important completeness), P3 (style / maintainability)
- Type: NewTest, Refactor, Style, Consolidation
- Status: Todo / InProgress / Done / Deferred

## Group: GeometryAsset (GeometryAsset.h / related)

| ID | Status | Action | Rationale / Gap Addressed | Type | Priority | Owner | Notes / Acceptance Criteria |
|----|--------|--------|---------------------------|------|----------|-------|-----------------------------|
| 1 | ✅ Done | Add GeometryAssetBasicTest_LodAccessors_ReturnExpected | No tests cover GeometryAsset LOD accessors | NewTest | P2 | | Meshes(), MeshAt valid indices, LodCount() |
| 2 | ✅ Done | Add GeometryAssetErrorTest_MeshAt_OutOfRange_ReturnsNull | Out-of-range LOD access undefined in tests | NewTest | P2 | | MeshAt(large) returns null shared_ptr |
| 3 | ✅ Done | Add GeometryAssetBasicTest_BoundingBoxMatchesDescriptor | Bounding box passthrough unverified | NewTest | P2 | | Compare descriptor min/max to accessors |

## Group: Mesh / MeshBuilder / SubMesh (GeometryAsset.h portions)

| ID | Status | Action | Rationale / Gap Addressed | Type | Priority | Owner | Notes / Acceptance Criteria |
|----|--------|--------|---------------------------|------|----------|-------|-----------------------------|
| 4 | ✅ Done | Add MeshBuilderErrorTest_MixOwnedThenReferencedStorage_Throws | Mixing storage before submesh not tested | NewTest | P1 | | StorageValidation_ProvidesDescriptiveErrorMessage covers |
| 5 | ✅ Done | Add MeshBuilderErrorTest_MixReferencedThenOwnedStorage_Throws | Reverse mixing path not tested | NewTest | P1 | | StorageValidation_MentionsCorrectStorageTypes_ReferencedThenOwned |
| 6 | ✅ Done | Add MeshBuilderErrorTest_BuildWithActiveSubMesh_Throws | Build while submesh in progress not covered | NewTest | P1 | | BuildWithActiveSubMesh_MentionsActive death test |
| 7 | ✅ Done | Add SubMeshBasicTest_MultipleMeshViews_AggregatedCorrectly | Multi-view submesh branch untested | NewTest | P2 | | MultipleMeshViews_AggregatedCorrectly implemented |
| 8 | ✅ Done | Add SubMeshBasicTest_DescriptorBoundsUsed | Descriptor-provided bounds code path untested | NewTest | P2 | | DescriptorBoundsUsed implemented (now passing after ComputeBounds call) |
| 9 | ✅ Done | Add SubMeshBasicTest_ComputedBoundsMatchVertices | Computed bounds path for procedural submesh untested | NewTest | P2 | | ComputedBoundsMatchVertices implemented (now passing) |
|10 | ✅ Done | Add MeshBasicTest_IsValidReflectsSubMeshPresence | IsValid() semantics not directly asserted | NewTest | P2 | | IsValidReflectsSubMeshPresence implemented |
|11 | ✅ Done | Add MeshReferencedStorageTest_16BitIndexDetection_Works | Referenced 16-bit index type detection untested | NewTest | P2 | | SixteenBitIndices_IndexTypeCached test |
|12 | ✅ Done | Add MeshViewBasicTest_VertexOnlyMesh_IndexBufferEmpty | Vertex-only mesh view index buffer semantics untested | NewTest | P2 | | VertexOnlyMesh_IndexBufferEmpty implemented |
|13 | ✅ Done | Add MeshBasicTest_VertexOnlyMesh_IsIndexedFalse | IsIndexed() on vertex-only mesh not asserted | NewTest | P2 | | VertexOnlyMesh_IsIndexedFalse implemented |
|14 | ✅ Done | Add MeshViewBasicTest_VerticesSpanSharesUnderlyingStorage | Zero-copy guarantee not asserted | NewTest | P3 | | VerticesSpanSharesUnderlyingStorage implemented |
|15 | ✅ Done | Add MeshReferencedStorageTest_IndexBufferView_NoCopySizeMatches | Referenced storage view non-copy path untested | NewTest | P3 | | IndexBufferView_NoCopySizeMatches implemented |
|16 | ✅ Done | Add SubMeshBuilderErrorTest_ReuseAfterEnd_Throws | SubMeshBuilder reuse semantics untested | NewTest | P3 | | SubMeshBuilderReuseAfterEnd_Throws implemented |
|17 | ✅ Done | Add SubMeshBuilderErrorTest_DoubleEndSubMesh_Throws | Double-end misuse untested | NewTest | P3 | | SubMeshBuilderDoubleEnd_Throws implemented |
|18 | ✅ Done | Add MeshBuilderErrorMessageTest_MixStorage_ErrorContainsBothTypeNames | Error message specificity not asserted | NewTest | P3 | | StorageValidation_ProvidesDescriptiveErrorMessage covers |
|19 | ✅ Done | Add MeshBuilderErrorMessageTest_BuildWithActiveSubMesh_MentionsActive | Error text clarity not asserted | NewTest | P3 | | BuildWithActiveSubMesh_MentionsActive contains phrase |

## Group: IndexBufferView utilities (detail::IndexBufferView)

| ID | Status | Action | Rationale / Gap Addressed | Type | Priority | Owner | Notes / Acceptance Criteria |
|----|--------|--------|---------------------------|------|----------|-------|-----------------------------|
|20 | ✅ Todo | Add IndexBufferViewTest_SliceElements_ValidProducesCorrectCount | SliceElements positive case untested | NewTest | P3 | | Count/bytes align with slice |
|21 | ✅ Todo | Add IndexBufferViewTest_SliceElements_InvalidReturnsEmpty | Invalid slice path untested | NewTest | P3 | | Out-of-range yields Empty() |
|22 | ✅ Todo | Add IndexBufferViewTest_WidenedIteration_OnSliceMatchesExpected | Widened iteration for slice not covered | NewTest | P3 | | Sequence matches manual extraction |
|23 | ✅ Todo | Add IndexBufferViewInvariantsTest_EmptyWhenTypeNone | Empty() semantics for kNone unverified | NewTest | P3 | | type kNone => Empty()==true |

## Group: BufferResource (BufferResource.h/.cpp)

| ID | Status | Action | Rationale / Gap Addressed | Type | Priority | Owner | Notes / Acceptance Criteria |
|----|--------|--------|---------------------------|------|----------|-------|-----------------------------|
|24 | ✅ Todo | Add BufferResourceBasicTest_ClassificationVariants_Correct | IsFormatted/IsStructured/IsRaw branches untested | NewTest | P2 | | Three descriptor variants verified |
|25 | ✅ Todo | Add BufferResourceFlagsTest_BitwiseCombination_PreservesBits | Flag operator helpers untested | NewTest | P3 | | OR/AND produce expected mask |
|26 | ✅ Todo | Add BufferResourceFlagsTest_ToString_IncludesAllSetFlags | to_string of flags unverified | NewTest | P3 | | All tokens present |
|27 | ✅ Todo | Add BufferResourceMoveTest_MoveConstructor_TransfersOwnership | Move semantics safety untested | NewTest | P3 | | Dest data size>0; source size==0 |
|28 | ✅ Todo | Add BufferResourceBasicTest_DataSizeMatchesDescriptor | Data size vs descriptor mismatch unnoticed | NewTest | P2 | | size_bytes == GetDataSize() |
|29 | ✅ Todo | Add BufferResourceBasicTest_DataOffsetPreserved | GetDataOffset passthrough untested | NewTest | P3 | | Offset equals descriptor value |

## Group: MaterialAsset / ShaderReference (MaterialAsset.h, ShaderReference.h)

| ID | Status | Action | Rationale / Gap Addressed | Type | Priority | Owner | Notes / Acceptance Criteria |
|----|--------|--------|---------------------------|------|----------|-------|-----------------------------|
|30 | ✅ Todo | Add MaterialAssetBasicTest_DefaultMaterialDomainAndFlags | Domain/flags not asserted | NewTest | P2 | | GetMaterialDomain(), GetFlags() expected |
|31 | ✅ Todo | Add MaterialAssetBasicTest_DefaultTextureIndicesUnset | Default texture indices untested | NewTest | P2 | | All texture indices sentinel/zero |
|32 | ✅ Todo | Add MaterialAssetConsistencyTest_ShaderRefsMatchStageMask | Stage mask vs shader_refs size mismatch undetected | NewTest | P2 | | popcount(mask)==shader refs count |
|33 | ✅ Todo | Add MaterialAssetBasicTest_DefaultScalarsStable | Scalar property getters untested | NewTest | P3 | | BaseColor size=4; scalars exact |
|34 | ✅ Todo | Add ShaderReferenceBasicTest_ConstructionAndAccessors | ShaderReference type unused by tests | NewTest | P3 | | Fields accessible, stage matches |

## Group: TextureResource (TextureResource.h/.cpp)

| ID | Status | Action | Rationale / Gap Addressed | Type | Priority | Owner | Notes / Acceptance Criteria |
|----|--------|--------|---------------------------|------|----------|-------|-----------------------------|
|35 | ✅ Todo | Add TextureResourceBasicTest_AccessorsReturnDescriptorValues | TextureResource entirely untested | NewTest | P2 | | Dimensions, format, mip count |
|36 | ✅ Todo | Add TextureResourceErrorTest_InvalidDescriptor_Throws | Invalid texture descriptor path untested | NewTest | P2 | | EXPECT_DEATH/throw on bad params |

## Group: Vertex (Vertex.h hashing/equality)

| ID | Status | Action | Rationale / Gap Addressed | Type | Priority | Owner | Notes / Acceptance Criteria |
|----|--------|--------|---------------------------|------|----------|-------|-----------------------------|
|37 | ✅ Todo | Add VertexHashTest_WithinEpsilon_EqualSameHash | Boundary equality at epsilon not distinct | NewTest | P2 | | Hash/equality hold at epsilon |
|38 | ✅ Todo | Add VertexHashTest_JustBeyondEpsilon_InequalDifferentHash | Boundary beyond epsilon divergence not isolated | NewTest | P2 | | Slight delta breaks equality/hash |
|39 | ✅ Todo | Add VertexHashTest_FieldPerturbations_ChangeHash | Field contribution coverage partial | NewTest | P2 | | Normal/tangent/color changes affect hash |

## Group: Procedural Mesh Generators (Procedural/*.cpp)

| ID | Status | Action | Rationale / Gap Addressed | Type | Priority | Owner | Notes / Acceptance Criteria |
|----|--------|--------|---------------------------|------|----------|-------|-----------------------------|
|40 | ✅ Todo | Add ProceduralMeshBasicTest_ShapesTopologyValid | Per-shape topology invariants missing | NewTest | P2 | | Each shape: verts>0, indices%3==0, in-range |
|41 | ✅ Todo | Add ProceduralMeshQualityTest_ShapesNormalsAndUVsValid | Normal length & UV range not verified | NewTest | P3 | | normals≈1, 0<=uv<=1 |
|42 | ✅ Todo | Add ProceduralMeshQualityTest_Torus_NoDegenerateTriangles | Degenerate triangle avoidance untested | NewTest | P3 | | No duplicate consecutive indices |
|43 | ✅ Todo | Add ProceduralMeshOptimizationTest_ConeCapVertexReuse | Cap vertex reuse optimization untested | NewTest | P3 | | Fewer unique cap verts than faces |

## Group: Converters & AssetKey (ToStringConverters.cpp / AssetKey)

| ID | Status | Action | Rationale / Gap Addressed | Type | Priority | Owner | Notes / Acceptance Criteria |
|----|--------|--------|---------------------------|------|----------|-------|-----------------------------|
|44 | ✅ Todo | Add ToStringConvertersTest_BufferUsageFlags_AllFlagsPresent | to_string coverage for all flags missing | NewTest | P3 | | Each flag token appears once |
|45 | ✅ Todo | Add ToStringConvertersTest_FormatEnum_AllKnownFormatsMapped | Format enum mapping completeness untested | NewTest | P3 | | All known formats produce strings |
|46 | ✅ Todo | Add AssetKeyOrderingTest_LexicalOrderConsistentWithGuid | Ordering operator behavior untested | NewTest | P3 | | Sort order matches string order |
