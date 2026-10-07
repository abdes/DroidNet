// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Inspector.Editing;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Node "Rendering" section: authored Scene Visibility and geometry Cast/Receive Shadows, edited as
/// explicit local values on every selected node.
/// </summary>
/// <remarks>
/// Every authored node carries its own values, so the flags only matter where the node itself is
/// rendered: Scene Visibility for geometry and lights, the shadow flags for geometry. The section
/// applies only when every selected node has geometry or a light, and shows the shadow flags only
/// when every selected node has geometry.
/// </remarks>
public sealed partial class NodeRenderingViewModel : ComponentPropertyEditor, IDisposable
{
    private readonly InspectorEditSessionCoordinator? edits;
    private readonly List<IInspectorBindingRefresh> registrations = [];
    private readonly PropertyBinding<bool> isVisibleBinding = new(SceneDocumentCommandService.NodeRendering.IsVisibleDescriptor);
    private readonly PropertyBinding<bool> castsShadowsBinding = new(SceneDocumentCommandService.NodeRendering.CastsShadowsDescriptor);
    private readonly PropertyBinding<bool> receivesShadowsBinding = new(SceneDocumentCommandService.NodeRendering.ReceivesShadowsDescriptor);
    private ICollection<SceneNode>? selectedItems;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="NodeRenderingViewModel"/> class.</summary>
    /// <param name="commandService">Optional command service used to apply flag edits.</param>
    /// <param name="commandContextProvider">Optional provider for the active scene command context.</param>
    public NodeRenderingViewModel(
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null)
    {
        if (commandService is not null && commandContextProvider is not null)
        {
            this.edits = new(commandService, commandContextProvider, "Edit Rendering", this.RefreshValues);
        }

        this.Register(this.isVisibleBinding, nameof(this.IsVisible));
        this.Register(this.castsShadowsBinding, nameof(this.CastsShadows));
        this.Register(this.receivesShadowsBinding, nameof(this.ReceivesShadows));
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

    /// <summary>Gets a value indicating whether every selected node has geometry, so the shadow flags apply.</summary>
    public bool ShowsShadowFlags { get; private set; }

    /// <inheritdoc />
    public override string Header => "Rendering";

    /// <inheritdoc />
    public override string Description => "Scene visibility and geometry shadow participation.";

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
        var targets = items.ToDictionary(static node => node.Id, static node => (object?)node);
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

    /// <summary>
    /// Gets a value indicating whether the section applies to a selection: every node has geometry or a light.
    /// </summary>
    /// <param name="items">The selected nodes.</param>
    /// <returns><see langword="true"/> when the rendering flags affect every selected node.</returns>
    internal static bool AppliesTo(ICollection<SceneNode> items)
        => items.Count > 0 && items.All(static node => HasGeometry(node) || node.Components.OfType<LightComponent>().Any());

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
