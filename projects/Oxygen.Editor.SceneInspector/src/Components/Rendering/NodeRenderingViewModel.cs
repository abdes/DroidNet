// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.SceneInspector.Components.Rendering;

/// <summary>
/// The "Rendering" section, which edits the node-flags component every node has: authored Scene Visibility, geometry
/// Cast/Receive Shadows and, under Advanced, Static and Ray-cast selectable, as explicit local values on every selected
/// node. Ignore Parent Transform, also a node flag, is edited in the Transform section.
/// </summary>
/// <remarks>
/// The shadow flags affect only the node's own geometry, so the section shows them only when every selected node has
/// geometry.
/// </remarks>
public sealed partial class NodeRenderingViewModel : ComponentPropertyEditor, IDisposable
{
    private readonly InspectorEditSessionCoordinator? edits;
    private readonly List<IInspectorBindingRefresh> registrations = [];
    private readonly PropertyBinding<bool> isVisibleBinding = new(SceneProperties.NodeFlags.IsVisibleDescriptor);
    private readonly PropertyBinding<bool> castsShadowsBinding = new(SceneProperties.NodeFlags.CastsShadowsDescriptor);
    private readonly PropertyBinding<bool> receivesShadowsBinding = new(SceneProperties.NodeFlags.ReceivesShadowsDescriptor);
    private readonly PropertyBinding<bool> isStaticBinding = new(SceneProperties.NodeFlags.IsStaticDescriptor);
    private readonly PropertyBinding<bool> isRayCastingSelectableBinding = new(SceneProperties.NodeFlags.IsRayCastingSelectableDescriptor);
    private ICollection<SceneNode>? selectedItems;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="NodeRenderingViewModel"/> class.</summary>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    public NodeRenderingViewModel(
        Func<SceneAuthoring?>? authoringProvider = null)
    {
        if (authoringProvider is not null)
        {
            this.edits = new(authoringProvider, "Edit Rendering", this.RefreshValues);
        }

        this.Register(this.isVisibleBinding, nameof(this.IsVisible));
        this.Register(this.castsShadowsBinding, nameof(this.CastsShadows));
        this.Register(this.receivesShadowsBinding, nameof(this.ReceivesShadows));
        this.Register(this.isStaticBinding, nameof(this.IsStatic));
        this.Register(this.isRayCastingSelectableBinding, nameof(this.IsRayCastingSelectable));
    }

    /// <summary>Gets or sets a value indicating whether the node is visible in the scene.</summary>
    public bool IsVisible
    {
        get => this.isVisibleBinding.Value;
        set => InspectorBindingRequests.Request(this.isVisibleBinding, value);
    }

    /// <summary>Gets or sets a value indicating whether the node geometry casts shadows.</summary>
    public bool CastsShadows
    {
        get => this.castsShadowsBinding.Value;
        set => InspectorBindingRequests.Request(this.castsShadowsBinding, value);
    }

    /// <summary>Gets or sets a value indicating whether the node geometry receives shadows.</summary>
    public bool ReceivesShadows
    {
        get => this.receivesShadowsBinding.Value;
        set => InspectorBindingRequests.Request(this.receivesShadowsBinding, value);
    }

    /// <summary>Gets or sets a value indicating whether the node is static, an optimization hint.</summary>
    public bool IsStatic
    {
        get => this.isStaticBinding.Value;
        set => InspectorBindingRequests.Request(this.isStaticBinding, value);
    }

    /// <summary>Gets or sets a value indicating whether ray casting can select the node.</summary>
    public bool IsRayCastingSelectable
    {
        get => this.isRayCastingSelectableBinding.Value;
        set => InspectorBindingRequests.Request(this.isRayCastingSelectableBinding, value);
    }

    /// <summary>Gets a value indicating whether every selected node has geometry, so the shadow flags apply.</summary>
    public bool ShowsShadowFlags { get; private set; }

    /// <inheritdoc />
    public override string Header => "Rendering";

    /// <inheritdoc />
    public override string Description => "Scene visibility, geometry shadow participation and node hints.";

    /// <inheritdoc />
    internal override InspectorFieldDiagnostics? ValidationFeedback => this.edits?.Diagnostics;

    /// <summary>Gets completion of submitted flag edits.</summary>
    internal Task PendingEdits => this.edits?.Pending ?? Task.CompletedTask;

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        foreach (var registration in this.registrations)
        {
            registration.Dispose();
        }

        this.edits?.Dispose();
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        var targets = items.ToDictionary(static node => node.Id, static node => (object?)node.Flags);
        var nodes = targets.Keys.ToArray();
        this.edits?.Bind(nodes);
        this.selectedItems = items;
        var showsShadowFlags = items.Count > 0 && items.All(HasGeometry);
        if (showsShadowFlags != this.ShowsShadowFlags)
        {
            this.ShowsShadowFlags = showsShadowFlags;
            this.OnPropertyChanged(nameof(this.ShowsShadowFlags));
        }

        foreach (var registration in this.registrations)
        {
            registration.Refresh(nodes, id => targets.GetValueOrDefault(id));
        }
    }

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled) => this.edits?.SetInputEnabled(enabled);

    private static bool HasGeometry(SceneNode node) => node.Components.OfType<GeometryComponent>().Any();

    private void Register<T>(PropertyBinding<T> binding, string property)
        => this.registrations.Add(new InspectorBindingRegistration<T>(
            binding,
            this.edits,
            () => this.IsInputEnabled && !this.disposed,
            this.RefreshValues,
            () => this.OnPropertyChanged(property)));

    private void RefreshValues()
    {
        if (this.selectedItems is { } items)
        {
            this.UpdateValues(items);
        }
    }
}
