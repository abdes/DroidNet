// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Windows.UI;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// ViewModel for V0.1 directional light inspector editing.
/// </summary>
public partial class DirectionalLightViewModel(
    ISceneDocumentCommandService? commandService = null,
    Func<SceneDocumentCommandContext?>? commandContextProvider = null) : ComponentPropertyEditor
{
    private ICollection<SceneNode>? selectedItems;
    private readonly SemaphoreSlim editGate = new(initialCount: 1, maxCount: 1);
    private bool isApplyingEditorValues;

    [ObservableProperty]
    public partial float ColorR { get; set; } = 1f;

    [ObservableProperty]
    public partial float ColorG { get; set; } = 1f;

    [ObservableProperty]
    public partial float ColorB { get; set; } = 1f;

    [ObservableProperty]
    public partial float IntensityLux { get; set; }

    [ObservableProperty]
    public partial bool IsSunLight { get; set; }

    [ObservableProperty]
    public partial bool EnvironmentContribution { get; set; }

    [ObservableProperty]
    public partial bool CastsShadows { get; set; }

    [ObservableProperty]
    public partial bool AffectsWorld { get; set; }

    [ObservableProperty]
    public partial float AngularSizeRadians { get; set; }

    [ObservableProperty]
    public partial float ExposureCompensation { get; set; }

    /// <summary>
    /// Gets a brush for the light color swatch.
    /// </summary>
    public SolidColorBrush ColorBrush => new(this.ColorValue);

    /// <summary>
    /// Gets the current light color as a WinUI color.
    /// </summary>
    public Color ColorValue => Color.FromArgb(255, ToByte(this.ColorR), ToByte(this.ColorG), ToByte(this.ColorB));

    /// <inheritdoc />
    public override string Header => "Directional Light";

    /// <inheritdoc />
    public override string Description => "Sun-style light, environment contribution, and shadowing.";

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        this.selectedItems = items;
        var lights = items.Select(static node => node.Components.OfType<DirectionalLightComponent>().FirstOrDefault()).OfType<DirectionalLightComponent>().ToList();
        if (lights.Count == 0)
        {
            return;
        }

        var light = lights[0];
        this.isApplyingEditorValues = true;
        try
        {
            this.ColorR = light.Color.X;
            this.ColorG = light.Color.Y;
            this.ColorB = light.Color.Z;
            this.IntensityLux = light.IntensityLux;
            this.IsSunLight = light.IsSunLight;
            this.EnvironmentContribution = light.EnvironmentContribution;
            this.CastsShadows = light.CastsShadows;
            this.AffectsWorld = light.AffectsWorld;
            this.AngularSizeRadians = light.AngularSizeRadians;
            this.ExposureCompensation = light.ExposureCompensation;
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }

        this.NotifyColorChanged();
    }

    /// <summary>
    /// Applies the selected color from the color picker.
    /// </summary>
    public void SetColor(Color color)
    {
        var r = color.R / 255f;
        var g = color.G / 255f;
        var b = color.B / 255f;
        if (NearlyEqual(this.ColorR, r) && NearlyEqual(this.ColorG, g) && NearlyEqual(this.ColorB, b))
        {
            return;
        }

        this.isApplyingEditorValues = true;
        try
        {
            this.ColorR = r;
            this.ColorG = g;
            this.ColorB = b;
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }

        this.NotifyColorChanged();
        this.ApplyLightEdit(ColorEdit(new Vector3(r, g, b)));
    }

    partial void OnColorRChanged(float value) => this.ApplyColorAxisEdit(value, this.ColorG, this.ColorB);

    partial void OnColorGChanged(float value) => this.ApplyColorAxisEdit(this.ColorR, value, this.ColorB);

    partial void OnColorBChanged(float value) => this.ApplyColorAxisEdit(this.ColorR, this.ColorG, value);

    partial void OnIntensityLuxChanged(float value) => this.ApplyLightEdit(new DirectionalLightEdit(
        Optional<Vector3>.Unspecified,
        Optional<float>.Supplied(value),
        Optional<bool>.Unspecified,
        Optional<bool>.Unspecified,
        Optional<bool>.Unspecified,
        Optional<bool>.Unspecified,
        Optional<float>.Unspecified,
        Optional<float>.Unspecified));

    partial void OnIsSunLightChanged(bool value) => this.ApplyLightEdit(BoolEdit(isSunLight: value));

    partial void OnEnvironmentContributionChanged(bool value) => this.ApplyLightEdit(BoolEdit(environmentContribution: value));

    partial void OnCastsShadowsChanged(bool value) => this.ApplyLightEdit(BoolEdit(castsShadows: value));

    partial void OnAffectsWorldChanged(bool value) => this.ApplyLightEdit(BoolEdit(affectsWorld: value));

    partial void OnAngularSizeRadiansChanged(float value) => this.ApplyLightEdit(new DirectionalLightEdit(
        Optional<Vector3>.Unspecified,
        Optional<float>.Unspecified,
        Optional<bool>.Unspecified,
        Optional<bool>.Unspecified,
        Optional<bool>.Unspecified,
        Optional<bool>.Unspecified,
        Optional<float>.Supplied(value),
        Optional<float>.Unspecified));

    partial void OnExposureCompensationChanged(float value) => this.ApplyLightEdit(new DirectionalLightEdit(
        Optional<Vector3>.Unspecified,
        Optional<float>.Unspecified,
        Optional<bool>.Unspecified,
        Optional<bool>.Unspecified,
        Optional<bool>.Unspecified,
        Optional<bool>.Unspecified,
        Optional<float>.Unspecified,
        Optional<float>.Supplied(value)));

    private void ApplyColorAxisEdit(float r, float g, float b)
    {
        this.NotifyColorChanged();
        this.ApplyLightEdit(ColorEdit(new Vector3(r, g, b)));
    }

    private void ApplyLightEdit(DirectionalLightEdit edit)
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
        _ = this.ApplyLightEditAsync(context, nodes, edit);
    }

    private async Task ApplyLightEditAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<SceneNode> nodes,
        DirectionalLightEdit edit)
    {
        await this.editGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var result = await commandService!.EditDirectionalLightAsync(
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
            // Live-sync failures are published by the command service. Keep the
            // property editor alive if an async command path rejects or throws.
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

    private static DirectionalLightEdit ColorEdit(Vector3 color)
        => new(
            Optional<Vector3>.Supplied(color),
            Optional<float>.Unspecified,
            Optional<bool>.Unspecified,
            Optional<bool>.Unspecified,
            Optional<bool>.Unspecified,
            Optional<bool>.Unspecified,
            Optional<float>.Unspecified,
            Optional<float>.Unspecified);

    private static DirectionalLightEdit BoolEdit(bool? isSunLight = null, bool? environmentContribution = null, bool? castsShadows = null, bool? affectsWorld = null)
        => new(
            Optional<Vector3>.Unspecified,
            Optional<float>.Unspecified,
            isSunLight.HasValue ? Optional<bool>.Supplied(isSunLight.Value) : Optional<bool>.Unspecified,
            environmentContribution.HasValue ? Optional<bool>.Supplied(environmentContribution.Value) : Optional<bool>.Unspecified,
            castsShadows.HasValue ? Optional<bool>.Supplied(castsShadows.Value) : Optional<bool>.Unspecified,
            affectsWorld.HasValue ? Optional<bool>.Supplied(affectsWorld.Value) : Optional<bool>.Unspecified,
            Optional<float>.Unspecified,
            Optional<float>.Unspecified);

    private void NotifyColorChanged()
    {
        this.OnPropertyChanged(nameof(this.ColorValue));
        this.OnPropertyChanged(nameof(this.ColorBrush));
    }

    private static byte ToByte(float value)
        => (byte)Math.Clamp(MathF.Round(Math.Clamp(value, 0f, 1f) * 255f), 0f, 255f);

    private static bool NearlyEqual(float left, float right)
        => MathF.Abs(left - right) <= 0.0001f;
}
