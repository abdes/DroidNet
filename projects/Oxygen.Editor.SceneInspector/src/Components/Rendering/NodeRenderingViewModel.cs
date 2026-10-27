// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Rendering;

/// <summary>
/// The "Rendering" section, which edits the node-flags component every node has: the source modes of Scene Visibility
/// and geometry Cast Shadows and, under Advanced, Receive Shadows, then the local Static and Ray-cast selectable values.
/// Ignore Parent Transform, also a node flag, is edited in the Transform section.
/// </summary>
/// <remarks>
/// Every node shows the shadow modes: a node without geometry passes them to its children that inherit. Under Inherit,
/// each mode's caption gives the resolved value and the ancestor it comes from.
/// </remarks>
public sealed partial class NodeRenderingViewModel : ComponentPropertyEditor, IDisposable
{
    private static readonly IReadOnlyList<string> ShadowModeNames = ["Inherit", "On", "Off"];

    private readonly InspectorFieldOverride unboundOverride = new(static () => { });
    private readonly InspectorEditSessionCoordinator? edits;
    private readonly List<IInspectorBindingRefresh> registrations = [];
    private readonly PropertyBinding<VisibilitySource> visibilityBinding = new(SceneProperties.NodeFlags.VisibilityDescriptor);
    private readonly PropertyBinding<ShadowSource> castsShadowsBinding = new(SceneProperties.NodeFlags.CastsShadowsDescriptor);
    private readonly PropertyBinding<ShadowSource> receivesShadowsBinding = new(SceneProperties.NodeFlags.ReceivesShadowsDescriptor);
    private readonly PropertyBinding<bool> isStaticBinding = new(SceneProperties.NodeFlags.IsStaticDescriptor);
    private readonly PropertyBinding<bool> isRayCastingSelectableBinding = new(SceneProperties.NodeFlags.IsRayCastingSelectableDescriptor);
    private ICollection<SceneNode>? selectedItems;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="NodeRenderingViewModel"/> class.</summary>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    /// <param name="selectNode">Selects a node, for the button that selects the ancestor a value comes from.</param>
    public NodeRenderingViewModel(
        Func<SceneAuthoring?>? authoringProvider = null,
        Action<NodeId>? selectNode = null)
    {
        if (authoringProvider is not null)
        {
            this.edits = new(authoringProvider, "Edit Rendering", this.RefreshValues, sourceApply: () => this.SourceApply);
        }

        this.Visibility = new NodeFlagSourceField(
            ["Inherit", "Shown", "Hidden"],
            () => Index(this.visibilityBinding),
            index => InspectorBindingRequests.Request(this.visibilityBinding, (VisibilitySource)index),
            static flags => (int)flags.Visibility,
            static flags => flags.ResolveVisibility(),
            selectNode);
        this.CastsShadows = new NodeFlagSourceField(
            ShadowModeNames,
            () => Index(this.castsShadowsBinding),
            index => InspectorBindingRequests.Request(this.castsShadowsBinding, (ShadowSource)index),
            static flags => (int)flags.CastsShadows,
            static flags => flags.ResolveCastsShadows(),
            selectNode);
        this.ReceivesShadows = new NodeFlagSourceField(
            ShadowModeNames,
            () => Index(this.receivesShadowsBinding),
            index => InspectorBindingRequests.Request(this.receivesShadowsBinding, (ShadowSource)index),
            static flags => (int)flags.ReceivesShadows,
            static flags => flags.ResolveReceivesShadows(),
            selectNode);

        this.Register(this.visibilityBinding, this.RefreshFields);
        this.Register(this.castsShadowsBinding, this.RefreshFields);
        this.Register(this.receivesShadowsBinding, this.RefreshFields);
        this.Register(this.isStaticBinding, () => this.OnPropertyChanged(nameof(this.IsStatic)));
        this.Register(this.isRayCastingSelectableBinding, () => this.OnPropertyChanged(nameof(this.IsRayCastingSelectable)));
    }

    /// <summary>Gets the scene visibility source mode.</summary>
    public NodeFlagSourceField Visibility { get; }

    /// <summary>Gets the geometry shadow casting source mode.</summary>
    public NodeFlagSourceField CastsShadows { get; }

    /// <summary>Gets the geometry shadow receiving source mode.</summary>
    public NodeFlagSourceField ReceivesShadows { get; }

    /// <summary>Gets or sets a value indicating whether the node is static, an optimization hint; null when the nodes differ.</summary>
    public bool? IsStatic
    {
        get => ValueOf(this.isStaticBinding);
        set => Request(this.isStaticBinding, value);
    }

    /// <summary>Gets or sets a value indicating whether ray casting can select the node; null when the nodes differ.</summary>
    public bool? IsRayCastingSelectable
    {
        get => ValueOf(this.isRayCastingSelectableBinding);
        set => Request(this.isRayCastingSelectableBinding, value);
    }

    /// <summary>Gets the scene visibility override bar.</summary>
    public InspectorFieldOverride VisibilityOverride => this.Override(this.visibilityBinding.Id.Id);

    /// <summary>Gets the cast shadows override bar.</summary>
    public InspectorFieldOverride CastsShadowsOverride => this.Override(this.castsShadowsBinding.Id.Id);

    /// <summary>Gets the receive shadows override bar.</summary>
    public InspectorFieldOverride ReceivesShadowsOverride => this.Override(this.receivesShadowsBinding.Id.Id);

    /// <summary>Gets the static override bar.</summary>
    public InspectorFieldOverride IsStaticOverride => this.Override(this.isStaticBinding.Id.Id);

    /// <summary>Gets the ray-cast selectable override bar.</summary>
    public InspectorFieldOverride IsRayCastingSelectableOverride => this.Override(this.isRayCastingSelectableBinding.Id.Id);

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
        var targets = items.ToDictionary(this.ReferenceOf, static node => (object?)node.Flags);
        var nodes = targets.Keys.ToArray();
        this.edits?.Bind(nodes);
        this.selectedItems = items;
        foreach (var registration in this.registrations)
        {
            registration.Refresh(nodes, targets.GetValueOrDefault);
        }

        this.RefreshFields();
    }

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled) => this.edits?.SetInputEnabled(enabled);

    private static int Index<T>(PropertyBinding<T> binding)
        where T : struct, Enum
        => binding.IsMixed || !binding.HasValue ? -1 : Convert.ToInt32(binding.Value, CultureInfo.InvariantCulture);

    private static bool? ValueOf(PropertyBinding<bool> binding) => binding.IsMixed || !binding.HasValue ? null : binding.Value;

    // A mixed toggle sends a value only once clicked, so null is never a request.
    private static void Request(PropertyBinding<bool> binding, bool? value)
    {
        if (value is { } flag)
        {
            InspectorBindingRequests.Request(binding, flag);
        }
    }

    private static bool HasGeometry(SceneNode node) => node.Components.OfType<GeometryComponent>().Any();

    // Hiding a node hides its children that inherit.
    private static string? VisibilityNotice(List<SceneNode> nodes)
        => nodes.Count > 0
            && nodes.TrueForAll(static node => node.Flags.Visibility == VisibilitySource.Hidden)
            && nodes.Exists(static node => node.Hierarchy.Children.Any(static child => child.Flags.Visibility == VisibilitySource.Inherit))
                ? "Children that inherit are hidden too."
                : null;

    // A node without geometry casts and receives nothing itself; its modes reach its children that inherit.
    private static string? ShadowNotice(List<SceneNode> nodes)
        => nodes.Count > 0 && !nodes.Exists(HasGeometry) ? "Applies to children that inherit." : null;

    /// <summary>Gets the override bar of a field.</summary>
    /// <param name="ids">The properties the field shows.</param>
    /// <returns>The field's bar.</returns>
    private InspectorFieldOverride Override(params PropertyId[] ids)
        => this.edits?.Overrides.Get(ids) ?? this.unboundOverride;

    private void Register<T>(PropertyBinding<T> binding, Action changed)
        => this.registrations.Add(new InspectorBindingRegistration<T>(
            binding,
            this.edits,
            () => this.IsInputEnabled && !this.disposed,
            this.RefreshValues,
            changed));

    private void RefreshFields()
    {
        var nodes = this.selectedItems?.ToList() ?? [];
        this.Visibility.Refresh(nodes, VisibilityNotice(nodes));
        this.CastsShadows.Refresh(nodes, ShadowNotice(nodes));
        this.ReceivesShadows.Refresh(nodes, ShadowNotice(nodes));
    }

    private void RefreshValues()
    {
        if (this.selectedItems is { } items)
        {
            this.UpdateValues(items);
        }
    }
}
