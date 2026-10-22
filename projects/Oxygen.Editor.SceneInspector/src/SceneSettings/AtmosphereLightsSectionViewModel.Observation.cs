// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

/// <summary>Refreshes current environment values and only the affected sun-reference dependencies.</summary>
public sealed partial class AtmosphereLightsSectionViewModel
{
    private readonly HashSet<SceneNode> observedNodes = [];
    private readonly HashSet<DirectionalLightComponent> observedLights = [];
    private readonly HashSet<TransformComponent> observedTransforms = [];
    private NodeId? observedSunId;
    private NodeId? observedSecondarySunId;

    private void AttachSceneObservers()
    {
        if (this.scene is not { } current)
        {
            return;
        }

        current.PropertyChanged += this.OnSceneModelChanged;
        this.observedSunId = this.FindAtmosphereSource(AtmosphereLightSlot.Primary)?.Id;
        this.observedSecondarySunId = this.FindAtmosphereSource(AtmosphereLightSlot.Secondary)?.Id;
        current.RootNodes.CollectionChanged += this.OnSceneTopologyChanged;
        foreach (var node in current.RootNodes)
        {
            this.ObserveSubtree(node);
        }
    }

    private void DetachSceneObservers()
    {
        if (this.scene is { } current)
        {
            current.PropertyChanged -= this.OnSceneModelChanged;
            current.RootNodes.CollectionChanged -= this.OnSceneTopologyChanged;
        }

        foreach (var node in this.observedNodes.ToArray())
        {
            this.UnobserveSubtree(node);
        }
    }

    private void ObserveSubtree(SceneNode node)
    {
        if (!this.observedNodes.Add(node))
        {
            return;
        }

        node.Hierarchy.Children.CollectionChanged += this.OnSceneTopologyChanged;
        node.Components.CollectionChanged += this.OnSunComponentsChanged;
        node.Metadata.PropertyChanged += this.OnSunNodeChanged;
        foreach (var light in node.Components.OfType<DirectionalLightComponent>())
        {
            if (this.observedLights.Add(light))
            {
                light.PropertyChanged += this.OnLightAssignmentChanged;
            }
        }

        if (this.observedTransforms.Add(node.Transform))
        {
            node.Transform.PropertyChanged += this.OnSourceTransformChanged;
        }

        foreach (var child in node.Hierarchy.Children)
        {
            this.ObserveSubtree(child);
        }
    }

    private void UnobserveSubtree(SceneNode node)
    {
        if (!this.observedNodes.Remove(node))
        {
            return;
        }

        node.Hierarchy.Children.CollectionChanged -= this.OnSceneTopologyChanged;
        node.Components.CollectionChanged -= this.OnSunComponentsChanged;
        node.Metadata.PropertyChanged -= this.OnSunNodeChanged;
        foreach (var light in node.Components.OfType<DirectionalLightComponent>())
        {
            if (this.observedLights.Remove(light))
            {
                light.PropertyChanged -= this.OnLightAssignmentChanged;
            }
        }

        if (this.observedTransforms.Remove(node.Transform))
        {
            node.Transform.PropertyChanged -= this.OnSourceTransformChanged;
        }

        foreach (var child in node.Hierarchy.Children)
        {
            this.UnobserveSubtree(child);
        }
    }

    private void OnSourceTransformChanged(object? sender, PropertyChangedEventArgs args)
        => this.RefreshAtmosphereSourceEditors();

    private void OnLightAssignmentChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.IsNullOrEmpty(args.PropertyName) || string.Equals(args.PropertyName, nameof(DirectionalLightComponent.AtmosphereSlot), StringComparison.Ordinal))
        {
            this.observedSunId = this.FindAtmosphereSource(AtmosphereLightSlot.Primary)?.Id;
            this.observedSecondarySunId = this.FindAtmosphereSource(AtmosphereLightSlot.Secondary)?.Id;
            this.lightAssignments?.ModelChanged(SceneSchema.DirectionalLight.AtmosphereSlot.Id);
            this.RefreshSunDependencies();
        }
        else
        {
            this.RefreshAtmosphereSourceEditors();
        }
    }

    private void OnSceneModelChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (ReferenceEquals(sender, this.scene)
            && (string.IsNullOrEmpty(args.PropertyName) || string.Equals(args.PropertyName, nameof(Scene.Environment), StringComparison.Ordinal)))
        {
            this.RefreshSunDependencies();
            var primaryId = this.FindAtmosphereSource(AtmosphereLightSlot.Primary)?.Id;
            var secondaryId = this.FindAtmosphereSource(AtmosphereLightSlot.Secondary)?.Id;
            if (this.observedSunId != primaryId || this.observedSecondarySunId != secondaryId)
            {
                this.observedSunId = primaryId;
                this.observedSecondarySunId = secondaryId;
                this.lightAssignments?.ModelChanged(SceneSchema.DirectionalLight.AtmosphereSlot.Id);
            }
        }
    }

    private void OnSceneTopologyChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (!ReferenceEquals(sender, this.scene?.RootNodes) && !this.observedNodes.Any(node => ReferenceEquals(sender, node.Hierarchy.Children)))
        {
            return;
        }

        if (args.Action == NotifyCollectionChangedAction.Reset)
        {
            this.DetachSceneObservers();
            this.AttachSceneObservers();
        }
        else
        {
            foreach (var node in args.OldItems?.OfType<SceneNode>() ?? [])
            {
                this.UnobserveSubtree(node);
            }

            foreach (var node in args.NewItems?.OfType<SceneNode>() ?? [])
            {
                this.ObserveSubtree(node);
            }
        }

        this.RefreshSunDependencies();
    }

    private void OnSunComponentsChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        var currentLights = this.observedNodes.SelectMany(node => node.Components.OfType<DirectionalLightComponent>()).ToHashSet();
        foreach (var removed in this.observedLights.Except(currentLights).ToArray())
        {
            removed.PropertyChanged -= this.OnLightAssignmentChanged;
            this.observedLights.Remove(removed);
        }

        foreach (var added in currentLights.Except(this.observedLights))
        {
            added.PropertyChanged += this.OnLightAssignmentChanged;
            this.observedLights.Add(added);
        }

        var currentTransforms = this.observedNodes.Select(static node => node.Transform).ToHashSet();
        foreach (var removed in this.observedTransforms.Except(currentTransforms).ToArray())
        {
            removed.PropertyChanged -= this.OnSourceTransformChanged;
            this.observedTransforms.Remove(removed);
        }

        foreach (var added in currentTransforms.Except(this.observedTransforms))
        {
            added.PropertyChanged += this.OnSourceTransformChanged;
            this.observedTransforms.Add(added);
        }

        if (this.observedNodes.Any(node => ReferenceEquals(sender, node.Components)))
        {
            this.RefreshSunDependencies();
        }
    }

    private void OnSunNodeChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (sender is not MetadataComponent || !this.observedNodes.Any(node => ReferenceEquals(node.Metadata, sender)))
        {
            return;
        }

        this.RefreshAtmosphereSourceEditors();
        if (string.IsNullOrEmpty(args.PropertyName) || string.Equals(args.PropertyName, nameof(MetadataComponent.Name), StringComparison.Ordinal))
        {
            this.RefreshSunDependencies();
        }
    }

    private void RefreshSunDependencies()
    {
        var applying = this.isApplyingEditorValues;
        this.isApplyingEditorValues = true;
        try
        {
            var previousPrimaryTargets = this.SunOptions.Select(option => option.NodeId).ToHashSet();
            var previousSecondaryTargets = this.SecondarySunOptions.Select(option => option.NodeId).ToHashSet();
            var primaryId = this.FindAtmosphereSource(AtmosphereLightSlot.Primary)?.Id;
            var secondaryId = this.FindAtmosphereSource(AtmosphereLightSlot.Secondary)?.Id;
            this.RebuildSunOptions(primaryId, secondaryId);
            if (!previousPrimaryTargets.SetEquals(this.SunOptions.Select(option => option.NodeId))
                || !previousSecondaryTargets.SetEquals(this.SecondarySunOptions.Select(option => option.NodeId)))
            {
                this.lightAssignments?.ModelChanged(SceneSchema.DirectionalLight.AtmosphereSlot.Id);
            }
        }
        finally
        {
            this.isApplyingEditorValues = applying;
        }
    }
}
