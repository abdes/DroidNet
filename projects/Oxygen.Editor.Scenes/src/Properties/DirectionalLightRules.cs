// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>
/// The rules of a directional light: its values keep the light rules together (<see cref="LightValidation"/>), and one
/// atmosphere source slot has at most one light in the scene. Each value's range is its descriptor's.
/// </summary>
internal sealed class DirectionalLightRules : IComponentRules
{
    /// <inheritdoc/>
    public string Kind => SceneSchema.DirectionalLight.Kind;

    /// <inheritdoc/>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> Descriptors => SceneProperties.DirectionalLight.ById;

    /// <inheritdoc/>
    public object? TargetOf(Scene scene, SceneNode? node) => node?.Components.OfType<DirectionalLightComponent>().FirstOrDefault();

    /// <inheritdoc/>
    public SceneDiagnostic? Validate(Scene scene, IReadOnlyList<PropertyTargetEdit> edits)
    {
        var candidates = new Dictionary<NodeId, DirectionalLightData>();
        var slotChanged = false;
        foreach (var (node, source, candidate) in ComponentCandidates.Of<DirectionalLightComponent>(scene, edits, this.Descriptors))
        {
            var data = (DirectionalLightData)candidate.Dehydrate();
            if (LightValidation.Validate(data) is { } violation)
            {
                return SceneDiagnostic.Error(SceneEditCodes.PropertiesInconsistent, violation.Message, node);
            }

            candidates[node] = data;
            slotChanged |= data.AtmosphereSlot != source.AtmosphereSlot;
        }

        return slotChanged ? SlotConflict(scene, candidates) : null;
    }

    /// <inheritdoc/>
    /// <remarks>A light added to a node that is not rotated points down at its default angle instead of along the horizon.</remarks>
    public IReadOnlyList<ISceneEdit> OnAdd(SceneNode node)
    {
        if (node.Transform.LocalRotation != Quaternion.Identity)
        {
            return [];
        }

        var euler = TransformConverter.QuaternionToEulerDegrees(DirectionalLightComponent.DefaultLocalRotation);
        var rotation = new PropertyEdit.Builder()
            .Set(SceneSchema.Transform.RotationX, euler.X)
            .Set(SceneSchema.Transform.RotationY, euler.Y)
            .Set(SceneSchema.Transform.RotationZ, euler.Z)
            .Build();
        return [new SetProperties(new Dictionary<NodeId, PropertyEdit> { [node.Id] = rotation }, "Point Light Down")];
    }

    /// <summary>Checks that each atmosphere slot keeps one owner once the candidate lights take their slots.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="candidates">The directional lights to check in place of the scene's, by node; a node may have none yet.</param>
    /// <returns>The conflict, naming the light that takes an owned slot; <see langword="null"/> when there is none.</returns>
    internal static SceneDiagnostic? SlotConflict(Scene scene, IReadOnlyDictionary<NodeId, DirectionalLightData> candidates)
    {
        // Lights that keep their slot own it first; a light moving into an owned slot is the one in conflict.
        var owners = new Dictionary<AtmosphereLightSlot, string>();
        var moving = new List<(NodeId Node, string Name, AtmosphereLightSlot Slot)>();
        foreach (var node in scene.AllNodes)
        {
            var current = node.Components.OfType<DirectionalLightComponent>().FirstOrDefault()?.AtmosphereSlot ?? AtmosphereLightSlot.None;
            var slot = candidates.TryGetValue(node.Id, out var candidate) ? candidate.AtmosphereSlot : current;
            if (slot == AtmosphereLightSlot.None)
            {
                continue;
            }

            if (slot == current)
            {
                _ = owners.TryAdd(slot, node.Metadata.Name);
            }
            else
            {
                moving.Add((node.Id, node.Metadata.Name, slot));
            }
        }

        foreach (var (node, name, slot) in moving)
        {
            if (!owners.TryAdd(slot, name))
            {
                return SceneDiagnostic.Error(SceneEditCodes.AtmosphereSlotOccupied, $"Atmosphere slot {slot} is already owned by '{owners[slot]}'.", node);
            }
        }

        return null;
    }
}
