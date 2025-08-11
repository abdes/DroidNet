# Data Module Test Enhancement Plan

Tracking table for improving test coverage, quality, and style consistency in `src/Oxygen/Data`.

Legend:

- Priority: P1 (critical correctness), P2 (important completeness), P3 (style / maintainability)
- Type: NewTest, Refactor, Style, Consolidation
- Status: Todo / InProgress / Done / Deferred

| ID | Status | Action | Rationale / Gap Addressed | Type | Priority | Owner | Notes / Acceptance Criteria |
|----|--------|--------|---------------------------|------|----------|-------|-----------------------------|
| 1 | ✅ Done | Add MeshBuilderErrorTest_BuildWithoutSubMesh_Throws | Invariant: mesh must have ≥1 submesh; currently untested | NewTest | P1 | | Implemented (EXPECT_DEATH, message fragment) |
| 2 | ✅ Done | Add MeshBuilderErrorTest_BuildWithoutStorage_Throws | Building before selecting storage mode untested | NewTest | P1 | | Implemented (EXPECT_DEATH) |
| 3 | ✅ Done | Add MeshBuilderErrorTest_WithIndicesOnlyThenBuild_Throws | Mesh cannot exist with indices but missing vertices | NewTest | P1 | | Implemented (EXPECT_DEATH) |
| 4 | ✅ Done | Add MeshBuilderErrorTest_WithVerticesOnlyThenBuild_Throws | Vertices-only mesh invalid; enforced by death test | NewTest | P1 | | Implemented (EXPECT_DEATH) |
| 5 | ✅ Done | Add MeshBuilderErrorTest_ReferencedBuffers_SizeMismatch_Throws | Index buffer size/stride alignment invariant not exercised | NewTest | P1 | | Implemented; added Build validation |
| 6 | ✅ Done | Add MeshViewDeathTest_ZeroIndexCountPositiveVertexCount_Throws | Only combined zero counts tested | NewTest | P1 | | Implemented (EXPECT_DEATH, checks 'at least one index') |
| 7 | ✅ Done | Add MeshViewDeathTest_ZeroVertexCountPositiveIndexCount_Throws | Only combined zero counts tested | NewTest | P1 | | Implemented (EXPECT_DEATH, checks 'at least one vertex') |
| 8 | ✅ Done | Add MeshViewDeathTest_EdgeOutOfRange_LastIndexPastEnd_Throws | Off-by-one boundary slice not covered | NewTest | P1 | | Implemented (EXPECT_DEATH, 'index range exceeds') |
| 9 | ✅ Done | Add MeshViewBasicTest_16BitIndices_WidenedIterationMatches | 16-bit widening path untested | NewTest | P1 | | Implemented (referenced storage R16UInt, Widened() matches) |
|10 | ✅ Done | Add SubMeshDeathTest_BuilderAddsSubMeshWithNoViews_Throws | Current test uses custom subclass; builder path untested | NewTest | P1 | | Implemented (logic_error via EndSubMesh without WithMeshView) |
|11 | ✅ Done | Add MaterialAssetBasicTest_CreateDebug_ReturnsValidMaterial | Debug factory untested | NewTest | P2 | | Implemented (stages, texture/shader counts) |
|12 | ✅ Done | Add VertexHashTest_QuantizedHash_DivergentBeyondEpsilon | Only equality within epsilon tested | NewTest | P2 | | Implemented (different hash & inequality) |
|13 | ✅ Done | Add AssetKeyBasicTest_GenerateDistinct_StableStringHash | Asset identity currently untested | NewTest | P2 | | Implemented (32 generated keys; distinct value/string/hash; deterministic) |
|14 | ✅ Done | Add BufferResourceDeathTest_IndexBufferSizeNotAligned_Throws | Alignment invariant missing | NewTest | P2 | | Implemented (construct misaligned index BufferResource -> EXPECT_DEATH on stride check) |
|15 | ✅ Done | Add MeshBoundingSphereTest_ComputedSphereContainsAllVertices | Bounding sphere (if implemented) untested | NewTest | P2 | | Implemented (owned + referenced storage; all vertices within radius) |
|16 | ✅ Done | Add ProceduralMeshBoundaryTest_SphereMinimumValidSegments | Boundary acceptance vs rejection | NewTest | P2 | | Implemented (2 or 3 invalid edges; (3,3) minimum valid) |
|17 | ✅ Done | Add ProceduralMeshBoundaryTest_PlaneMinimumResolution | Edge of validity not tested | NewTest | P2 | | Implemented (invalid: x=0,z=0,size<=0; (2,2) valid; (1,1) observed conditional) |
|18 | ✅ Todo | Add MeshBuilderErrorTest_DuplicateBeginSubMeshWithoutEnd_Throws | Misuse sequence not covered | NewTest | P2 | | Call BeginSubMesh twice; expect failure |
|19 | ✅ Todo | Add MeshBuilderErrorTest_EndSubMeshWithoutBegin_Throws | Defensive behavior not tested | NewTest | P2 | | Direct EndSubMesh call invalid |
|20 | ✅ Todo | Consolidate MeshView tests into one file | Reduce duplication (Mesh_test + MeshView_test) | Consolidation | P3 | | Move scenarios; keep focused fixtures |
|21 | ✅ Done | Remove gmock from MeshView/SubMesh tests | Overkill; replaced with real Mesh instances (no mocks) | Refactor | P3 | | Replaced MockMesh + EXPECT/ON_CALL with direct Mesh construction |
|22 | ✅ Done | Remove TestSubMesh subclass usage | Tests internal implementation path | Refactor | P3 | | Rewritten using MeshBuilder; no subclass present |
|23 | ✅ Done | Add AAA comments consistently across all tests | Style guideline compliance | Style | P3 | | Added // Arrange // Act // Assert sections to all Data tests |
|24 | ✅ Done | Add brief //! doc comments for all fixtures | Documentation standard | Style | P3 | | Added //! briefs for all test fixtures (helper + fixture classes) |
|25 | ✅ Done | Wrap all test symbols in anonymous namespace | Prevent ODR / symbol leakage | Style | P3 | | Added anonymous namespace around fixtures/helpers in MeshView/SubMesh tests |
|26 | ✅ Todo | Remove duplicate includes & unused using directives | Cleanliness | Style | P3 | | E.g., duplicate GTest include, unused AllOf |
|27 | ⏳ Todo | Introduce constants for repeated magic numbers | Maintainability | Style | P3 | | constexpr counts (e.g., kCubeVertexCount) |
|28 | ✅ Done | Parameterize procedural invalid inputs | Reduce duplication in ValidInvalidInput | Refactor | P3 | | Implemented arrays + loops in ProceduralMeshes_test.cpp |
|29 | ✅ Done | Document Link_test.cpp purpose with brief comment | Clarify intent | Style | P3 | | Added detailed brief + block doc at top of Link_test.cpp |
|30 | ✅ Done | Add test for IndexBuffer().Widened order & size for 32-bit | Ensure both paths validated | NewTest | P2 | | Added MeshViewIndexTypeTest.ThirtyTwoBitIndices_WidenedMatchesAsU32 |
|31 | ✅ Done | Add test verifying SubMesh material non-null enforced through builder | Completes invariant via public API | NewTest | P1 | | BeginSubMesh nullptr now throws logic_error (test added) |
|32 | ✅ Done | Add test for MeshBuilder mixing storage after starting submesh | Additional misuse path | NewTest | P2 | | Implemented (WithBufferResources after BeginSubMesh -> logic_error) |
|33 | ✅ Done | Verify Build with referenced storage missing index buffer (if API allows separating) | Edge case not tested | NewTest | P2 | | Implemented (Build succeeds w/ vertex buffer only; IndexCount==0) |
|34 | ✅ Done | Update README invariants section after clarifying vertex-only legality | Keep docs consistent | Documentation | P2 | | Adjust text plus tests alignment |
