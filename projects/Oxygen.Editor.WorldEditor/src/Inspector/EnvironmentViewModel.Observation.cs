// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Refreshes current environment values and only the affected sun-reference dependencies.</summary>
public partial class EnvironmentViewModel
{
    private readonly HashSet<SceneNode> observedNodes = [];
    private Guid? observedSunId;
    private readonly HashSet<DirectionalLightComponent> observedLights = [];

    private void AttachSceneObservers()
    {
        if (this.scene is not { } current)
        {
            return;
        }

        current.PropertyChanged += this.OnSceneModelChanged;
        this.observedSunId = this.FindPrimarySource()?.Id;
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

        node.Children.CollectionChanged += this.OnSceneTopologyChanged;
        node.Components.CollectionChanged += this.OnSunComponentsChanged;
        node.PropertyChanged += this.OnSunNodeChanged;
        foreach (var light in node.Components.OfType<DirectionalLightComponent>())
        {
            if (this.observedLights.Add(light)) light.PropertyChanged += this.OnLightAssignmentChanged;
        }
        foreach (var child in node.Children)
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

        node.Children.CollectionChanged -= this.OnSceneTopologyChanged;
        node.Components.CollectionChanged -= this.OnSunComponentsChanged;
        node.PropertyChanged -= this.OnSunNodeChanged;
        foreach (var light in node.Components.OfType<DirectionalLightComponent>())
        {
            if (this.observedLights.Remove(light)) light.PropertyChanged -= this.OnLightAssignmentChanged;
        }
        foreach (var child in node.Children)
        {
            this.UnobserveSubtree(child);
        }
    }

    private void OnLightAssignmentChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.IsNullOrEmpty(args.PropertyName) || args.PropertyName == nameof(DirectionalLightComponent.AtmosphereSlot))
        {
            this.observedSunId = this.FindPrimarySource()?.Id;
            this.lightAssignments?.ModelChanged(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot.Id);
            this.RefreshSunDependencies();
        }
    }

    private void OnSceneModelChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (ReferenceEquals(sender, this.scene)
            && (string.IsNullOrEmpty(args.PropertyName) || string.Equals(args.PropertyName, nameof(Scene.Environment), StringComparison.Ordinal)))
        {
            this.RefreshFromScene();
            if (this.observedSunId != this.FindPrimarySource()?.Id)
            {
                this.observedSunId = this.FindPrimarySource()?.Id;
                this.lightAssignments?.ModelChanged(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot.Id);
            }
        }
    }

    private void OnSceneTopologyChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (!ReferenceEquals(sender, this.scene?.RootNodes) && !this.observedNodes.Any(node => ReferenceEquals(sender, node.Children)))
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

        if (this.observedNodes.Any(node => ReferenceEquals(sender, node.Components)))
        {
            this.RefreshSunDependencies();
        }
    }

    private void OnSunNodeChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (sender is SceneNode node && this.observedNodes.Contains(node)
            && (string.IsNullOrEmpty(args.PropertyName) || string.Equals(args.PropertyName, nameof(SceneNode.Name), StringComparison.Ordinal)))
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
            var previousTargets = this.SunOptions.Select(option => option.NodeId).ToHashSet();
            this.RebuildSunOptions(this.FindPrimarySource()?.Id);
            if (!previousTargets.SetEquals(this.SunOptions.Select(option => option.NodeId)))
            {
                this.lightAssignments?.ModelChanged(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot.Id);
            }
        }
        finally
        {
            this.isApplyingEditorValues = applying;
        }
    }
}
