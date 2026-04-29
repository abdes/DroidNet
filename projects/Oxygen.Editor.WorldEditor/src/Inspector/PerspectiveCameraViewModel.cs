// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// ViewModel for V0.1 perspective camera inspector editing.
/// </summary>
public partial class PerspectiveCameraViewModel(
    ISceneDocumentCommandService? commandService = null,
    Func<SceneDocumentCommandContext?>? commandContextProvider = null) : ComponentPropertyEditor
{
    private ICollection<SceneNode>? selectedItems;
    private readonly SemaphoreSlim editGate = new(initialCount: 1, maxCount: 1);
    private bool isApplyingEditorValues;

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
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        this.selectedItems = items;
        var cameras = items.Select(static node => node.Components.OfType<PerspectiveCamera>().FirstOrDefault()).OfType<PerspectiveCamera>().ToList();

        this.isApplyingEditorValues = true;
        try
        {
            this.SetFloatValue(cameras, static camera => camera.FieldOfView, value => this.FieldOfView = value, value => this.FieldOfViewIsIndeterminate = value);
            this.SetFloatValue(cameras, static camera => camera.NearPlane, value => this.NearPlane = value, value => this.NearPlaneIsIndeterminate = value);
            this.SetFloatValue(cameras, static camera => camera.FarPlane, value => this.FarPlane = value, value => this.FarPlaneIsIndeterminate = value);
            this.SetFloatValue(cameras, static camera => camera.AspectRatio, value => this.AspectRatio = value, value => this.AspectRatioIsIndeterminate = value);
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }
    }

    partial void OnFieldOfViewChanged(float value)
        => this.ApplyCameraEdit(new PerspectiveCameraEdit(
            Optional<float>.Supplied(value),
            Optional<float>.Unspecified,
            Optional<float>.Unspecified,
            Optional<float>.Unspecified));

    partial void OnNearPlaneChanged(float value)
        => this.ApplyCameraEdit(new PerspectiveCameraEdit(
            Optional<float>.Unspecified,
            Optional<float>.Unspecified,
            Optional<float>.Supplied(value),
            Optional<float>.Unspecified));

    partial void OnFarPlaneChanged(float value)
        => this.ApplyCameraEdit(new PerspectiveCameraEdit(
            Optional<float>.Unspecified,
            Optional<float>.Unspecified,
            Optional<float>.Unspecified,
            Optional<float>.Supplied(value)));

    partial void OnAspectRatioChanged(float value)
        => this.ApplyCameraEdit(new PerspectiveCameraEdit(
            Optional<float>.Unspecified,
            Optional<float>.Supplied(value),
            Optional<float>.Unspecified,
            Optional<float>.Unspecified));

    private void ApplyCameraEdit(PerspectiveCameraEdit edit)
    {
        if (this.isApplyingEditorValues || this.selectedItems is null || this.selectedItems.Count == 0)
        {
            return;
        }

        if (commandService is null || commandContextProvider?.Invoke() is not { } context)
        {
            return;
        }

        var nodes = this.selectedItems.ToList();
        _ = this.ApplyCameraEditAsync(context, nodes, edit);
    }

    private async Task ApplyCameraEditAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<SceneNode> nodes,
        PerspectiveCameraEdit edit)
    {
        await this.editGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var result = await commandService!.EditPerspectiveCameraAsync(
                context,
                nodes.Select(static node => node.Id).ToList(),
                edit,
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
        catch
        {
            // The command layer publishes sync diagnostics. Keep the inspector
            // usable even when a live-update command is rejected asynchronously.
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

    private void SetFloatValue(
        IReadOnlyList<PerspectiveCamera> cameras,
        Func<PerspectiveCamera, float> selector,
        Action<float> setValue,
        Action<bool> setIndeterminate)
    {
        if (cameras.Count == 0)
        {
            setIndeterminate(false);
            setValue(0f);
            return;
        }

        var first = selector(cameras[0]);
        var mixed = cameras.Any(camera => !NearlyEqual(selector(camera), first));
        setIndeterminate(mixed);
        setValue(mixed ? 0f : first);
    }

    private static bool NearlyEqual(float left, float right)
        => MathF.Abs(left - right) <= 0.0001f;
}
