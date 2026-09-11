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

    private void AttachSceneObservers()
    {
        if (this.scene is not { } current)
        {
            return;
        }

        current.PropertyChanged += this.OnSceneModelChanged;
        this.observedSunId = current.Environment.SunNodeId;
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
        foreach (var child in node.Children)
        {
            this.UnobserveSubtree(child);
        }
    }

    private void OnSceneModelChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (ReferenceEquals(sender, this.scene)
            && (string.IsNullOrEmpty(args.PropertyName) || string.Equals(args.PropertyName, nameof(Scene.Environment), StringComparison.Ordinal)))
        {
            this.RefreshFromScene();
            if (this.observedSunId != this.scene?.Environment.SunNodeId)
            {
                this.observedSunId = this.scene?.Environment.SunNodeId;
                this.edits?.ModelChanged(SceneDocumentCommandService.SceneEnvironment.SunNodeId.Id);
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
            this.RebuildSunOptions(this.scene?.Environment.SunNodeId);
            if (!previousTargets.SetEquals(this.SunOptions.Select(option => option.NodeId)))
            {
                this.edits?.ModelChanged(SceneDocumentCommandService.SceneEnvironment.SunNodeId.Id);
            }
        }
        finally
        {
            this.isApplyingEditorValues = applying;
        }
    }
}
