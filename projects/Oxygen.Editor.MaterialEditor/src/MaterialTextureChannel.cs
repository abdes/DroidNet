// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.Materials.Properties;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>An editable texture slot: the textures it can be assigned, and how it reads the one it has.</summary>
/// <remarks>Initializes a new instance of the <see cref="MaterialTextureChannel"/> class.</remarks>
/// <param name="channel">The native material schema field.</param>
/// <param name="displayName">The user-facing channel name.</param>
/// <param name="selectionChanged">The owning document edit callback.</param>
/// <param name="edit">Applies an edit of how the slot reads its texture, if the slot can be edited.</param>
/// <param name="resetSampling">Puts the slot's sampling back to the defaults, if the slot can be edited.</param>
/// <param name="applyToAll">Copies the slot's sampling to the other assigned slots, if the slot can be edited.</param>
public sealed partial class MaterialTextureChannel(
    string channel,
    string displayName,
    Action<MaterialTextureChannel, AssetPath?> selectionChanged,
    Action<PropertyEdit>? edit = null,
    Action<MaterialTextureChannel>? resetSampling = null,
    Action<MaterialTextureChannel>? applyToAll = null) : ObservableObject
{
    private const double DegreesPerRadian = 180.0 / Math.PI;
    private static readonly AssetChoice NoTexture = new("None", Asset: null, "Remove the texture", "No texture", IsEnabled: true, Glyph: string.Empty);
    private readonly MaterialSlotProperties slot = MaterialDescriptors.Slot(channel);
    private IReadOnlyDictionary<AssetPath, string> namesByTexture = new Dictionary<AssetPath, string>();
    private AssetPath? selectedTexture;
    private bool isRefreshing;

    /// <summary>Gets the native channel key.</summary>
    public string Channel { get; } = channel;

    /// <summary>Gets the user-facing channel name.</summary>
    public string DisplayName { get; } = displayName;

    /// <summary>Gets the name of the slot's sampling fields, which labels a gesture's history entry.</summary>
    public string SamplingField => this.DisplayName + " sampling";

    /// <summary>Gets the textures the channel can be assigned, grouped as Engine, Packages and Content, with a choice that removes the texture.</summary>
    public AssetPickerModel Choices { get; } = new(static asset => AssetChoice.For(asset, "Texture", ""), NoTexture);

    /// <summary>Gets or sets the assigned texture.</summary>
    public AssetPath? SelectedTexture
    {
        get => this.selectedTexture;
        set
        {
            if (this.SetProperty(ref this.selectedTexture, value))
            {
                this.OnSelectionShown();
                selectionChanged(this, value);
            }
        }
    }

    /// <summary>Gets the picker's text: the assigned texture's name, <c>None</c>, or the missing path.</summary>
    public string SelectedText => this.SelectedTexture is not { } texture
        ? "None"
        : this.namesByTexture.GetValueOrDefault(texture) ?? ("Missing: " + texture.VirtualPath);

    /// <summary>Gets a warning for a missing or invalid assigned texture.</summary>
    public string WarningText => this.SelectedTexture is { } texture && !this.namesByTexture.ContainsKey(texture)
        ? $"Missing or invalid {this.DisplayName} texture: {texture.VirtualPath}"
        : string.Empty;

    /// <summary>Gets whether the warning is visible.</summary>
    public Visibility WarningVisibility => string.IsNullOrEmpty(this.WarningText) ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets whether the sampling row shows: sampling belongs to an assigned texture.</summary>
    public Visibility SamplingVisibility => this.SelectedTexture is null ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets the sampling row's summary: what differs from the defaults.</summary>
    public string SamplingSummary
    {
        get
        {
            var parts = this.SummaryParts().ToArray();
            return parts.Length == 0 ? "Default" : string.Join("  ·  ", parts);
        }
    }

    /// <summary>Gets the UV set row's mark.</summary>
    public OverrideState UvSetMark => Mark(this.UvSetIndex != 0);

    /// <summary>Gets the scale row's mark.</summary>
    public OverrideState ScaleMark => Mark(this.ScaleU != 1.0 || this.ScaleV != 1.0);

    /// <summary>Gets the offset row's mark.</summary>
    public OverrideState OffsetMark => Mark(this.OffsetU != 0.0 || this.OffsetV != 0.0);

    /// <summary>Gets the rotation row's mark.</summary>
    public OverrideState RotationMark => Mark(this.RotationDegrees != 0.0);

    /// <summary>Gets the wrap U row's mark.</summary>
    public OverrideState AddressUMark => Mark(this.AddressUIndex != (int)MaterialTextureAddress.Repeat);

    /// <summary>Gets the wrap V row's mark.</summary>
    public OverrideState AddressVMark => Mark(this.AddressVIndex != (int)MaterialTextureAddress.Repeat);

    /// <summary>Gets the close up filter row's mark.</summary>
    public OverrideState MagFilterMark => Mark(this.MagFilterIndex != (int)MaterialTextureFilter.Linear);

    /// <summary>Gets the far away filter row's mark.</summary>
    public OverrideState MinFilterMark => Mark(this.MinFilterIndex != (int)MaterialTextureFilter.Linear);

    /// <summary>Gets the mip levels row's mark.</summary>
    public OverrideState MipFilterMark => Mark(this.MipFilterIndex != (int)MaterialTextureMipFilter.Linear);

    /// <summary>Gets or sets a value indicating whether another slot has a texture to apply this slot's sampling to.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ApplyToAllCommand))]
    public partial bool HasOtherAssignedSlots { get; set; }

    /// <summary>Gets or sets the UV set the slot reads.</summary>
    [ObservableProperty]
    public partial int UvSetIndex { get; set; }

    /// <summary>Gets or sets the U scale.</summary>
    [ObservableProperty]
    public partial double ScaleU { get; set; } = 1.0;

    /// <summary>Gets or sets the V scale.</summary>
    [ObservableProperty]
    public partial double ScaleV { get; set; } = 1.0;

    /// <summary>Gets or sets the U offset.</summary>
    [ObservableProperty]
    public partial double OffsetU { get; set; }

    /// <summary>Gets or sets the V offset.</summary>
    [ObservableProperty]
    public partial double OffsetV { get; set; }

    /// <summary>Gets or sets the rotation in degrees; the file holds radians.</summary>
    [ObservableProperty]
    public partial double RotationDegrees { get; set; }

    /// <summary>Gets or sets how the texture repeats along U, as an index of <see cref="MaterialTextureAddress"/>.</summary>
    [ObservableProperty]
    public partial int AddressUIndex { get; set; }

    /// <summary>Gets or sets how the texture repeats along V, as an index of <see cref="MaterialTextureAddress"/>.</summary>
    [ObservableProperty]
    public partial int AddressVIndex { get; set; }

    /// <summary>Gets or sets the filter when a texel covers more than a pixel, as an index of <see cref="MaterialTextureFilter"/>.</summary>
    [ObservableProperty]
    public partial int MagFilterIndex { get; set; } = (int)MaterialTextureFilter.Linear;

    /// <summary>Gets or sets the filter when a texel covers less than a pixel, as an index of <see cref="MaterialTextureFilter"/>.</summary>
    [ObservableProperty]
    public partial int MinFilterIndex { get; set; } = (int)MaterialTextureFilter.Linear;

    /// <summary>Gets or sets the filter between mip levels, as an index of <see cref="MaterialTextureMipFilter"/>.</summary>
    [ObservableProperty]
    public partial int MipFilterIndex { get; set; } = (int)MaterialTextureMipFilter.Linear;

    /// <summary>Shows the textures the channel accepts now and the source's binding; an unresolved texture stays visible so it can be replaced or removed.</summary>
    /// <param name="available">The textures the channel accepts.</param>
    /// <param name="binding">The source's current binding, or <see langword="null"/> when the channel has no texture.</param>
    public void Refresh(IReadOnlyList<AssetEntry> available, MaterialTextureBinding? binding)
    {
        ArgumentNullException.ThrowIfNull(available);
        this.namesByTexture = available
            .DistinctBy(static texture => texture.Identity)
            .ToDictionary(static texture => texture.Identity, static texture => texture.DisplayName);
        this.Choices.Update(available);
        _ = this.SetProperty(ref this.selectedTexture, binding?.Texture, nameof(this.SelectedTexture));
        var transform = binding?.UvTransform ?? MaterialUvTransform.Identity;
        var sampler = binding?.Sampler ?? MaterialTextureSampler.Default;
        this.isRefreshing = true;
        try
        {
            this.UvSetIndex = binding?.UvSet ?? 0;
            this.ScaleU = transform.Scale.X;
            this.ScaleV = transform.Scale.Y;
            this.OffsetU = transform.Offset.X;
            this.OffsetV = transform.Offset.Y;
            this.RotationDegrees = transform.RotationRadians * DegreesPerRadian;
            this.AddressUIndex = (int)sampler.AddressU;
            this.AddressVIndex = (int)sampler.AddressV;
            this.MagFilterIndex = (int)sampler.MagFilter;
            this.MinFilterIndex = (int)sampler.MinFilter;
            this.MipFilterIndex = (int)sampler.MipFilter;
        }
        finally
        {
            this.isRefreshing = false;
        }

        this.OnSelectionShown();
        this.OnSamplingShown();
    }

    private static OverrideState Mark(bool differs) => differs ? OverrideState.Full : OverrideState.None;

    private static string Number(double value) => value.ToString("0.###", CultureInfo.CurrentCulture);

    private IEnumerable<string> SummaryParts()
    {
        if (this.UvSetIndex != 0)
        {
            yield return "Set " + this.UvSetIndex.ToString(CultureInfo.CurrentCulture);
        }

        if (this.ScaleMark != OverrideState.None)
        {
            yield return $"Scale {Number(this.ScaleU)}×, {Number(this.ScaleV)}×";
        }

        if (this.OffsetMark != OverrideState.None)
        {
            yield return $"Offset {Number(this.OffsetU)}, {Number(this.OffsetV)}";
        }

        if (this.RotationMark != OverrideState.None)
        {
            yield return $"Rotation {Number(this.RotationDegrees)}°";
        }

        if (this.AddressUMark != OverrideState.None)
        {
            yield return $"{(MaterialTextureAddress)this.AddressUIndex} U";
        }

        if (this.AddressVMark != OverrideState.None)
        {
            yield return $"{(MaterialTextureAddress)this.AddressVIndex} V";
        }

        if (this.MagFilterMark != OverrideState.None)
        {
            yield return "Nearest close up";
        }

        if (this.MinFilterMark != OverrideState.None)
        {
            yield return "Nearest far away";
        }

        if (this.MipFilterMark != OverrideState.None)
        {
            yield return $"Mips {(MaterialTextureMipFilter)this.MipFilterIndex}";
        }
    }

    private void OnSelectionShown()
    {
        this.OnPropertyChanged(nameof(this.SelectedText));
        this.OnPropertyChanged(nameof(this.WarningText));
        this.OnPropertyChanged(nameof(this.WarningVisibility));
        this.OnPropertyChanged(nameof(this.SamplingVisibility));
    }

    private void OnSamplingShown()
    {
        this.OnPropertyChanged(nameof(this.SamplingSummary));
        this.OnPropertyChanged(nameof(this.UvSetMark));
        this.OnPropertyChanged(nameof(this.ScaleMark));
        this.OnPropertyChanged(nameof(this.OffsetMark));
        this.OnPropertyChanged(nameof(this.RotationMark));
        this.OnPropertyChanged(nameof(this.AddressUMark));
        this.OnPropertyChanged(nameof(this.AddressVMark));
        this.OnPropertyChanged(nameof(this.MagFilterMark));
        this.OnPropertyChanged(nameof(this.MinFilterMark));
        this.OnPropertyChanged(nameof(this.MipFilterMark));
    }

    private void Edited(PropertyEdit change)
    {
        this.OnSamplingShown();
        edit?.Invoke(change);
    }

    // A control that has lost its selection reports -1, which is no value of the field.
    private void EditedChoice(int index, PropertyEdit change)
    {
        if (!this.isRefreshing && index >= 0)
        {
            this.Edited(change);
        }
    }

    private void EditedNumber(PropertyEdit change)
    {
        if (!this.isRefreshing)
        {
            this.Edited(change);
        }
    }

    partial void OnUvSetIndexChanged(int value) => this.EditedChoice(value, PropertyEdit.SingleEdit(this.slot.UvSet, value));

    partial void OnScaleUChanged(double value) => this.EditedNumber(PropertyEdit.SingleEdit(this.slot.ScaleU, (float)value));

    partial void OnScaleVChanged(double value) => this.EditedNumber(PropertyEdit.SingleEdit(this.slot.ScaleV, (float)value));

    partial void OnOffsetUChanged(double value) => this.EditedNumber(PropertyEdit.SingleEdit(this.slot.OffsetU, (float)value));

    partial void OnOffsetVChanged(double value) => this.EditedNumber(PropertyEdit.SingleEdit(this.slot.OffsetV, (float)value));

    partial void OnRotationDegreesChanged(double value) => this.EditedNumber(PropertyEdit.SingleEdit(this.slot.RotationRadians, (float)(value / DegreesPerRadian)));

    partial void OnAddressUIndexChanged(int value) => this.EditedChoice(value, PropertyEdit.SingleEdit(this.slot.AddressU, (MaterialTextureAddress)value));

    partial void OnAddressVIndexChanged(int value) => this.EditedChoice(value, PropertyEdit.SingleEdit(this.slot.AddressV, (MaterialTextureAddress)value));

    partial void OnMagFilterIndexChanged(int value) => this.EditedChoice(value, PropertyEdit.SingleEdit(this.slot.MagFilter, (MaterialTextureFilter)value));

    partial void OnMinFilterIndexChanged(int value) => this.EditedChoice(value, PropertyEdit.SingleEdit(this.slot.MinFilter, (MaterialTextureFilter)value));

    partial void OnMipFilterIndexChanged(int value) => this.EditedChoice(value, PropertyEdit.SingleEdit(this.slot.MipFilter, (MaterialTextureMipFilter)value));

    [RelayCommand]
    private void ResetUvSet() => this.UvSetIndex = 0;

    [RelayCommand]
    private void ResetScale() => this.ResetPair(() => (this.ScaleU, this.ScaleV) = (1.0, 1.0), new PropertyEdit.Builder().Set(this.slot.ScaleU, 1.0f).Set(this.slot.ScaleV, 1.0f).Build());

    [RelayCommand]
    private void ResetOffset() => this.ResetPair(() => (this.OffsetU, this.OffsetV) = (0.0, 0.0), new PropertyEdit.Builder().Set(this.slot.OffsetU, 0.0f).Set(this.slot.OffsetV, 0.0f).Build());

    [RelayCommand]
    private void ResetRotation() => this.RotationDegrees = 0.0;

    [RelayCommand]
    private void ResetAddressU() => this.AddressUIndex = (int)MaterialTextureAddress.Repeat;

    [RelayCommand]
    private void ResetAddressV() => this.AddressVIndex = (int)MaterialTextureAddress.Repeat;

    [RelayCommand]
    private void ResetMagFilter() => this.MagFilterIndex = (int)MaterialTextureFilter.Linear;

    [RelayCommand]
    private void ResetMinFilter() => this.MinFilterIndex = (int)MaterialTextureFilter.Linear;

    [RelayCommand]
    private void ResetMipFilter() => this.MipFilterIndex = (int)MaterialTextureMipFilter.Linear;

    [RelayCommand]
    private void ResetSampling() => resetSampling?.Invoke(this);

    [RelayCommand(CanExecute = nameof(HasOtherAssignedSlots))]
    private void ApplyToAll() => applyToAll?.Invoke(this);

    // Two fields reset together are one edit, so one history entry.
    private void ResetPair(Action set, PropertyEdit change)
    {
        this.isRefreshing = true;
        try
        {
            set();
        }
        finally
        {
            this.isRefreshing = false;
        }

        this.Edited(change);
    }
}
