// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Services;

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
    /// <param name="cancellationToken">Cancellation token to abort stale scene synchronization.</param>
    /// <returns><see langword="true"/> when the scene was synchronized into the engine; otherwise <see langword="false"/>.</returns>
    public Task<bool> SyncSceneAsync(Scene scene, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Synchronizes an entire scene with the engine once the engine is running.
    /// </summary>
    /// <param name="scene">The scene to synchronize.</param>
    /// <param name="cancellationToken">
    ///     Cancellation token to abort waiting (e.g., when a newer scene load supersedes this one).
    /// </param>
    /// <returns><see langword="true"/> when the scene was synchronized into the engine; otherwise <see langword="false"/>.</returns>
    public Task<bool> SyncSceneWhenReadyAsync(Scene scene, CancellationToken cancellationToken = default);

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
    /// <param name="newParentGuid">Optional new parent id, or <see langword="null"/> to make a root node.</param>
    /// <param name="preserveWorldTransform">If true, preserve world transform rather than local.</param>
    public Task ReparentNodeAsync(Guid nodeId, Guid? newParentGuid, bool preserveWorldTransform = false);

    /// <summary>
    ///     Reparents multiple node hierarchies to the same parent in the engine.
    /// </summary>
    /// <param name="nodeIds">Hierarchy roots to move.</param>
    /// <param name="newParentGuid">Optional new parent id, or <see langword="null"/> to make roots.</param>
    /// <param name="preserveWorldTransform">If true, preserve world transform rather than local.</param>
    public Task ReparentHierarchiesAsync(IReadOnlyList<Guid> nodeIds, Guid? newParentGuid, bool preserveWorldTransform = false);

    // ============================================================================
    // TransformComponent Operations
    // ============================================================================

    /// <summary>
    ///     Updates the transform of an existing scene node in the engine and returns a classified sync outcome.
    /// </summary>
    public Task<SyncOutcome> UpdateNodeTransformAsync(Scene scene, SceneNode node, CancellationToken cancellationToken = default);

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
    ///     Attaches or replaces the geometry component on a scene node and returns a classified sync outcome.
    /// </summary>
    public Task<SyncOutcome> AttachGeometryAsync(Scene scene, SceneNode node, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Detaches all geometry from a scene node and returns a classified sync outcome.
    /// </summary>
    public Task<SyncOutcome> DetachGeometryAsync(Scene scene, Guid nodeId, CancellationToken cancellationToken = default);

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
    // Light Operations
    // ============================================================================

    /// <summary>
    ///     Attaches or replaces the light component on a scene node and returns a classified sync outcome.
    /// </summary>
    public Task<SyncOutcome> AttachLightAsync(Scene scene, SceneNode node, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Detaches the light component from a scene node and returns a classified sync outcome.
    /// </summary>
    public Task<SyncOutcome> DetachLightAsync(Scene scene, Guid nodeId, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Attaches or replaces the light component on a scene node.
    /// </summary>
    /// <param name="node">The scene node that will receive the light.</param>
    /// <param name="light">The light component to attach.</param>
    /// <returns>A task that completes when the light is attached.</returns>
    public Task AttachLightAsync(SceneNode node, LightComponent light);

    /// <summary>
    ///     Detaches the light component from a scene node.
    /// </summary>
    /// <param name="nodeId">The GUID of the node.</param>
    /// <returns>A task that completes when the light is detached.</returns>
    public Task DetachLightAsync(Guid nodeId);

    // ============================================================================
    // Camera Operations
    // ============================================================================

    /// <summary>
    ///     Attaches or replaces the camera component on a scene node and returns a classified sync outcome.
    /// </summary>
    public Task<SyncOutcome> AttachCameraAsync(Scene scene, SceneNode node, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Detaches the camera component from a scene node and returns a classified sync outcome.
    /// </summary>
    public Task<SyncOutcome> DetachCameraAsync(Scene scene, Guid nodeId, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Attaches or replaces the camera component on a scene node.
    /// </summary>
    /// <param name="node">The scene node that will receive the camera.</param>
    /// <param name="camera">The camera component to attach.</param>
    /// <returns>A task that completes when the camera is attached.</returns>
    public Task AttachCameraAsync(SceneNode node, CameraComponent camera);

    /// <summary>
    ///     Detaches the camera component from a scene node.
    /// </summary>
    /// <param name="nodeId">The GUID of the node.</param>
    /// <returns>A task that completes when the camera is detached.</returns>
    public Task DetachCameraAsync(Guid nodeId);

    // ============================================================================
    // Geometry Operations - Fine-Grained Material Updates
    // ============================================================================

    /// <summary>
    ///     Updates a material slot for a geometry component and returns a classified sync outcome.
    /// </summary>
    public Task<SyncOutcome> UpdateMaterialSlotAsync(
        Scene scene,
        SceneNode node,
        int slotIndex,
        Uri? materialUri,
        CancellationToken cancellationToken = default);

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

    /// <summary>
    ///     Updates live environment settings and returns per-field sync support.
    /// </summary>
    public Task<EnvironmentSyncResult> UpdateEnvironmentAsync(
        Scene scene,
        SceneEnvironmentData environment,
        CancellationToken cancellationToken = default);

    /// <summary>
    ///     Returns true when an open edit session should issue a throttled preview sync for a node.
    /// </summary>
    public bool ShouldIssuePreviewSync(Guid sceneId, Guid nodeId, DateTimeOffset observedAt);

    /// <summary>
    ///     Runs a preview sync for the latest in-flight edit value only when the preview throttle allows it.
    /// </summary>
    public Task<SyncOutcome?> TryPreviewSyncAsync(
        Guid sceneId,
        Guid nodeId,
        DateTimeOffset observedAt,
        Func<CancellationToken, Task<SyncOutcome>> sync,
        CancellationToken cancellationToken = default);

    /// <summary>
    ///     Completes an edit session's terminal sync and clears preview throttle state for a node.
    /// </summary>
    public bool CompleteTerminalSync(Guid sceneId, Guid nodeId);

    /// <summary>
    ///     Clears preview throttle state and runs the edit session's one terminal sync.
    /// </summary>
    public Task<SyncOutcome> CompleteTerminalSyncAsync(
        Guid sceneId,
        Guid nodeId,
        Func<CancellationToken, Task<SyncOutcome>> sync,
        CancellationToken cancellationToken = default);

    /// <summary>
    ///     Cancels an edit session's preview sync state for a node.
    /// </summary>
    public void CancelPreviewSync(Guid sceneId, Guid nodeId);

    /// <summary>
    ///     Clears preview throttle state and runs the edit session's revert sync.
    /// </summary>
    public Task<SyncOutcome> CancelPreviewSyncAsync(
        Guid sceneId,
        Guid nodeId,
        Func<CancellationToken, Task<SyncOutcome>> revertSync,
        CancellationToken cancellationToken = default);
}
