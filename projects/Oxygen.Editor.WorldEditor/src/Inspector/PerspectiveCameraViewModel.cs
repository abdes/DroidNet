// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// ViewModel for V0.1 perspective camera inspector editing.
/// </summary>
public sealed partial class PerspectiveCameraViewModel : ComponentPropertyEditor, IDisposable
{
    private readonly ISceneDocumentCommandService? commandService;
    private readonly Func<SceneDocumentCommandContext?>? commandContextProvider;
    private readonly SemaphoreSlim editGate = new(initialCount: 1, maxCount: 1);
    private readonly PropertyBinding<float> fieldOfViewBinding = new(SceneDocumentCommandService.PerspectiveCamera.FieldOfViewDegreesDescriptor);
    private readonly PropertyBinding<float> nearPlaneBinding = new(SceneDocumentCommandService.PerspectiveCamera.NearPlaneDescriptor);
    private readonly PropertyBinding<float> farPlaneBinding = new(SceneDocumentCommandService.PerspectiveCamera.FarPlaneDescriptor);
    private readonly PropertyBinding<float> aspectRatioBinding = new(SceneDocumentCommandService.PerspectiveCamera.AspectRatioDescriptor);
    private ICollection<SceneNode>? selectedItems;
    private bool isApplyingEditorValues;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="PerspectiveCameraViewModel"/> class.
    /// </summary>
    /// <param name="commandService">Optional command service used to apply camera edits.</param>
    /// <param name="commandContextProvider">Optional provider for the active scene command context.</param>
    public PerspectiveCameraViewModel(
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null)
    {
        this.commandService = commandService;
        this.commandContextProvider = commandContextProvider;
        this.fieldOfViewBinding.ValueRequested += this.OnCameraValueRequested;
        this.nearPlaneBinding.ValueRequested += this.OnCameraValueRequested;
        this.farPlaneBinding.ValueRequested += this.OnCameraValueRequested;
        this.aspectRatioBinding.ValueRequested += this.OnCameraValueRequested;
    }

    [ObservableProperty]
    public partial bool FieldOfViewIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float FieldOfView { get; set; }

    [ObservableProperty]
    public partial bool NearPlaneIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float NearPlane { get; set; }

    [ObservableProperty]
    public partial bool FarPlaneIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float FarPlane { get; set; }

    [ObservableProperty]
    public partial bool AspectRatioIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float AspectRatio { get; set; }

    /// <inheritdoc />
    public override string Header => "Perspective Camera";

    /// <inheritdoc />
    public override string Description => "Defines projection, clipping planes, and aspect ratio.";

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.fieldOfViewBinding.ValueRequested -= this.OnCameraValueRequested;
        this.nearPlaneBinding.ValueRequested -= this.OnCameraValueRequested;
        this.farPlaneBinding.ValueRequested -= this.OnCameraValueRequested;
        this.aspectRatioBinding.ValueRequested -= this.OnCameraValueRequested;
        this.editGate.Dispose();
        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        this.selectedItems = items;
        var targets = items
            .Select(static node => new { Node = node, Camera = node.Components.OfType<PerspectiveCamera>().FirstOrDefault() })
            .Where(static target => target.Camera is not null)
            .ToList();
        var nodeIds = targets.Select(static target => target.Node.Id).ToList();
        var targetsByNode = targets.ToDictionary(static target => target.Node.Id, static target => (object?)target.Camera);

        this.isApplyingEditorValues = true;
        try
        {
            UpdateBinding(this.fieldOfViewBinding, nodeIds, targetsByNode, value => this.FieldOfView = value, value => this.FieldOfViewIsIndeterminate = value);
            UpdateBinding(this.nearPlaneBinding, nodeIds, targetsByNode, value => this.NearPlane = value, value => this.NearPlaneIsIndeterminate = value);
            UpdateBinding(this.farPlaneBinding, nodeIds, targetsByNode, value => this.FarPlane = value, value => this.FarPlaneIsIndeterminate = value);
            UpdateBinding(this.aspectRatioBinding, nodeIds, targetsByNode, value => this.AspectRatio = value, value => this.AspectRatioIsIndeterminate = value);
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }
    }

    private static void UpdateBinding(
        PropertyBinding<float> binding,
        IReadOnlyList<Guid> nodeIds,
        Dictionary<Guid, object?> targetsByNode,
        Action<float> setValue,
        Action<bool> setIndeterminate)
    {
        binding.UpdateFromModel(nodeIds, nodeId => targetsByNode.TryGetValue(nodeId, out var target) ? target : null);
        setIndeterminate(binding.IsMixed);
        setValue(binding.HasValue ? binding.Value : 0f);
    }

    partial void OnFieldOfViewChanged(float value)
        => this.RequestCameraValue(this.fieldOfViewBinding, value);

    partial void OnNearPlaneChanged(float value)
        => this.RequestCameraValue(this.nearPlaneBinding, value);

    partial void OnFarPlaneChanged(float value)
        => this.RequestCameraValue(this.farPlaneBinding, value);

    partial void OnAspectRatioChanged(float value)
        => this.RequestCameraValue(this.aspectRatioBinding, value);

    private void RequestCameraValue(PropertyBinding<float> binding, float value)
    {
        if (this.isApplyingEditorValues)
        {
            return;
        }

        binding.Value = value;
    }

    private void OnCameraValueRequested(object? sender, PropertyBindingChangedEventArgs<float> args)
    {
        if (sender is not PropertyBinding<float> binding)
        {
            return;
        }

        this.ApplyCameraEdit(PropertyEdit.Single(binding.Id, args.NewValue));
    }

    private void ApplyCameraEdit(PropertyEdit edit)
    {
        if (this.isApplyingEditorValues || this.selectedItems is null || this.selectedItems.Count == 0)
        {
            return;
        }

        if (this.commandService is null || this.commandContextProvider?.Invoke() is not { } context)
        {
            return;
        }

        var nodes = this.selectedItems.ToList();
        _ = this.ApplyCameraEditAsync(context, nodes, edit);
    }

    private async Task ApplyCameraEditAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<SceneNode> nodes,
        PropertyEdit edit)
    {
        await this.editGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var result = await this.commandService!.EditPropertiesAsync(
                context,
                nodes.Select(static node => node.Id).ToList(),
                edit,
                "Edit Camera",
                EditSessionToken.OneShot).ConfigureAwait(true);
            if (!result.Succeeded || !this.SelectionMatches(nodes))
            {
                return;
            }

            this.isApplyingEditorValues = true;
            try
            {
                this.UpdateValues(nodes.ToList());
            }
            finally
            {
                this.isApplyingEditorValues = false;
            }
        }
        catch (InvalidOperationException)
        {
            // The command layer publishes sync diagnostics. Keep the inspector
            // usable even when a live-update command is rejected asynchronously.
        }
        catch (OperationCanceledException)
        {
            // The edit was canceled by the active document workflow.
        }
        finally
        {
            _ = this.editGate.Release();
        }
    }

    private bool SelectionMatches(IReadOnlyCollection<SceneNode> nodes)
    {
        if (this.selectedItems is null || this.selectedItems.Count != nodes.Count)
        {
            return false;
        }

        var expectedIds = nodes.Select(static node => node.Id).ToHashSet();
        return this.selectedItems.All(node => expectedIds.Contains(node.Id));
    }
}
