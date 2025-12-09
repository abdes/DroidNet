// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.WorldEditor.Services;

/// <summary>
///     Service responsible for synchronizing scene data between the editor's managed model
///     and the native rendering engine.
/// </summary>
/// <remarks>
///     This service abstracts all engine-specific operations, providing a clean boundary
///     between the UI layer and the rendering engine. It supports both coarse-grained
///     operations (full component sync) and fine-grained updates (individual property changes).
/// </remarks>
public interface ISceneEngineSync
{
    /// <summary>
    ///     Synchronizes an entire scene with the engine, creating all nodes and establishing the hierarchy.
    /// </summary>
    /// <param name="scene">The scene to synchronize.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task SyncSceneAsync(Scene scene);

    /// <summary>
    ///     Creates a new scene node in the engine.
    /// </summary>
    /// <param name="node">The scene node to create.</param>
    /// <param name="parentGuid">Optional parent node GUID. If null, the node becomes a root node.</param>
    /// <returns>A task that completes when the node is created and initialized.</returns>
    public Task CreateNodeAsync(SceneNode node, Guid? parentGuid = null);

    /// <summary>
    ///     Removes a scene node from the engine.
    /// </summary>
    /// <param name="nodeId">The GUID of the node to remove.</param>
    /// <returns>A task that completes when the node is removed.</returns>
    public Task RemoveNodeAsync(Guid nodeId);

    /// <summary>
    ///     Removes a scene node hierarchy (node and all descendants) from the engine.
    /// </summary>
    /// <param name="rootNodeId">The GUID of the hierarchy root to remove.</param>
    /// <returns>A task that completes when the hierarchy is removed.</returns>
    public Task RemoveNodeHierarchyAsync(Guid rootNodeId);

    /// <summary>
    ///     Removes multiple scene node hierarchies (nodes and all descendants) from the engine.
    /// </summary>
    /// <param name="rootNodeIds">The GUIDs of the hierarchy roots to remove.</param>
    /// <returns>A task that completes when the hierarchies are removed.</returns>
    public Task RemoveNodeHierarchiesAsync(IReadOnlyList<Guid> rootNodeIds);

    /// <summary>
    ///     Reparents a node in the engine to the provided parent (null = root).
    /// </summary>
    /// <param name="nodeId">Node id to reparent.</param>
    /// <param name="newParentGuid">Optional new parent id, or <c>null</c> to make a root node.</param>
    /// <param name="preserveWorldTransform">If true, preserve world transform rather than local.</param>
    public Task ReparentNodeAsync(Guid nodeId, Guid? newParentGuid, bool preserveWorldTransform = false);

    /// <summary>
    ///     Reparents multiple node hierarchies to the same parent in the engine.
    /// </summary>
    /// <param name="nodeIds">Hierarchy roots to move.</param>
    /// <param name="newParentGuid">Optional new parent id, or <c>null</c> to make roots.</param>
    /// <param name="preserveWorldTransform">If true, preserve world transform rather than local.</param>
    public Task ReparentHierarchiesAsync(IReadOnlyList<Guid> nodeIds, Guid? newParentGuid, bool preserveWorldTransform = false);

    // ============================================================================
    // Transform Operations
    // ============================================================================

    /// <summary>
    ///     Updates the transform of an existing scene node in the engine.
    /// </summary>
    /// <param name="node">The scene node with updated transform data.</param>
    /// <returns>A task that completes when the transform is updated.</returns>
    public Task UpdateNodeTransformAsync(SceneNode node);

    // ============================================================================
    // Geometry Operations - Coarse-Grained
    // ============================================================================

    /// <summary>
    ///     Attaches or replaces the entire geometry component on a scene node.
    /// </summary>
    /// <param name="node">The scene node that will receive the geometry.</param>
    /// <param name="geometry">The geometry component to attach.</param>
    /// <returns>A task that completes when the geometry is attached.</returns>
    /// <remarks>
    ///     This replaces the entire geometry mesh. For granular updates to materials or
    ///     LODs, use the specific update methods instead.
    /// </remarks>
    public Task AttachGeometryAsync(SceneNode node, GeometryComponent geometry);

    /// <summary>
    ///     Detaches all geometry from a scene node.
    /// </summary>
    /// <param name="nodeId">The GUID of the node.</param>
    /// <returns>A task that completes when the geometry is detached.</returns>
    public Task DetachGeometryAsync(Guid nodeId);

    // ============================================================================
    // Geometry Operations - Fine-Grained Material Updates
    // ============================================================================

    /// <summary>
    ///     Updates a component-level material override slot for a geometry component.
    /// </summary>
    /// <param name="nodeId">The GUID of the node containing the geometry.</param>
    /// <param name="slot">The override slot to apply (e.g., MaterialsSlot with new material).</param>
    /// <returns>A task that completes when the override is applied.</returns>
    /// <remarks>
    ///     Component-level overrides apply to the entire geometry. For LOD or submesh-specific
    ///     overrides, use <see cref="UpdateTargetedMaterialOverrideAsync"/>.
    /// </remarks>
    public Task UpdateMaterialOverrideAsync(Guid nodeId, OverrideSlot slot);

    /// <summary>
    ///     Updates a targeted material override for a specific LOD and/or submesh.
    /// </summary>
    /// <param name="nodeId">The GUID of the node containing the geometry.</param>
    /// <param name="lodIndex">LOD index to target (-1 for all LODs).</param>
    /// <param name="submeshIndex">Submesh index to target (-1 for all submeshes).</param>
    /// <param name="slot">The override slot to apply.</param>
    /// <returns>A task that completes when the targeted override is applied.</returns>
    /// <remarks>
    ///     Allows fine-grained control over specific parts of the geometry. For example,
    ///     you can apply different materials to different submeshes or LOD levels.
    /// </remarks>
    public Task UpdateTargetedMaterialOverrideAsync(
        Guid nodeId,
        int lodIndex,
        int submeshIndex,
        OverrideSlot slot);

    /// <summary>
    ///     Removes a component-level material override.
    /// </summary>
    /// <param name="nodeId">The GUID of the node.</param>
    /// <param name="slotType">The type of override slot to remove (e.g., typeof(MaterialsSlot)).</param>
    /// <returns>A task that completes when the override is removed.</returns>
    public Task RemoveMaterialOverrideAsync(Guid nodeId, Type slotType);

    /// <summary>
    ///     Removes a targeted material override.
    /// </summary>
    /// <param name="nodeId">The GUID of the node.</param>
    /// <param name="lodIndex">LOD index that was targeted.</param>
    /// <param name="submeshIndex">Submesh index that was targeted.</param>
    /// <param name="slotType">The type of override slot to remove.</param>
    /// <returns>A task that completes when the targeted override is removed.</returns>
    public Task RemoveTargetedMaterialOverrideAsync(
        Guid nodeId,
        int lodIndex,
        int submeshIndex,
        Type slotType);

    // ============================================================================
    // Geometry Operations - LOD & Rendering Updates
    // ============================================================================

    /// <summary>
    ///     Updates the LOD (Level of Detail) policy for a geometry component.
    /// </summary>
    /// <param name="nodeId">The GUID of the node.</param>
    /// <param name="lodSlot">The LevelOfDetailSlot containing the LOD policy.</param>
    /// <returns>A task that completes when the LOD policy is updated.</returns>
    /// <remarks>
    ///     This updates how the engine selects which LOD to render (e.g., distance-based,
    ///     screen-space error, or fixed LOD).
    /// </remarks>
    public Task UpdateLodPolicyAsync(Guid nodeId, LevelOfDetailSlot lodSlot);

    /// <summary>
    ///     Updates rendering settings (e.g., visibility) for a geometry component.
    /// </summary>
    /// <param name="nodeId">The GUID of the node.</param>
    /// <param name="renderingSlot">The RenderingSlot containing rendering settings.</param>
    /// <returns>A task that completes when the rendering settings are updated.</returns>
    public Task UpdateRenderingSettingsAsync(Guid nodeId, RenderingSlot renderingSlot);

    /// <summary>
    ///     Updates lighting settings (e.g., shadow casting/receiving) for a geometry component.
    /// </summary>
    /// <param name="nodeId">The GUID of the node.</param>
    /// <param name="lightingSlot">The LightingSlot containing lighting settings.</param>
    /// <returns>A task that completes when the lighting settings are updated.</returns>
    /// <remarks>
    ///     Controls whether the geometry casts shadows and receives shadows from other objects.
    /// </remarks>
    public Task UpdateLightingSettingsAsync(Guid nodeId, LightingSlot lightingSlot);
}
