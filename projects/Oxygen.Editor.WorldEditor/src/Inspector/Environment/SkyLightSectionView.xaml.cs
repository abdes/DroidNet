// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Oxygen.Editor.World.Inspector.Controls;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Inspector.Presentation;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Composes the sky light's source, multipliers and disclosures.</summary>
[ViewModel(typeof(SkyLightSectionViewModel))]
public sealed partial class SkyLightSectionView
{
    /// <summary>Initializes a new instance of the <see cref="SkyLightSectionView"/> class.</summary>
    public SkyLightSectionView()
    {
        this.InitializeComponent();
        this.ViewModelChanged += (_, _) => this.CubemapFlyout.Hide();
        this.Unloaded += (_, _) => this.CubemapFlyout.Hide();
    }

    /// <summary>Registers this view's controls and disclosures by stable browsing identity.</summary>
    /// <param name="registry">The host's presentation registry.</param>
    internal void Register(InspectorSceneFieldRegistry registry)
    {
        registry.Section("SkyLight", this, this.SkyLightSection);
        registry.Disclosures.Add("LowerHemisphere", this.LowerHemisphereDisclosure);
        registry.Disclosures.Add("SkyLightAdvanced", this.AdvancedDisclosure);
        registry.Field("SkyLightEnabled", this.EnabledCard);
        registry.Field("SkyLightSource", this.SourceCard);
        registry.Field("SkyLightCubemap", this.CubemapCard);
        registry.Field("SkyLightCubemapAngle", this.CubemapAngleCard);
        registry.Field("SkyLightCubemapIlluminance", this.CubemapIlluminanceCard);
        registry.Field("SkyLightIntensity", this.IntensityCard);
        registry.Field("SkyLightTint", this.TintCard);
        registry.Field("SkyLightDiffuse", this.DiffuseIntensityCard);
        registry.Field("SkyLightSpecular", this.SpecularIntensityCard);
        registry.Field("SkyLightLowerHemisphereIsSolidColor", this.LowerHemisphereIsSolidColorCard);
        registry.Field("SkyLightLowerHemisphereColor", this.LowerHemisphereColorCard);
        registry.Field("SkyLightLowerHemisphereBlend", this.LowerHemisphereBlendAlphaCard);
        registry.Field("SkyLightVolumetricScattering", this.VolumetricScatteringIntensityCard);
        registry.Field("SkyLightAffectReflections", this.AffectReflectionsCard);
    }

    private void CubemapItem_Click(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: AssetPickerRow row } && row.Item.IsEnabled)
        {
            this.ViewModel?.SetCubemap(row.Item.Uri);
            this.CubemapFlyout.Hide();
        }
    }

    private void ClearCubemap_Click(object sender, RoutedEventArgs args) => this.ViewModel?.SetCubemap(cubemap: null);

    private void OnRgbColorPicked(object? sender, InspectorRgbColorPickedEventArgs args)
    {
        var property = (sender as FrameworkElement)?.Tag switch
        {
            "SkyLightTint" => SceneSkyFields.SkyLightTintRgb,
            "SkyLightLowerHemisphereColor" => SceneSkyFields.SkyLightLowerHemisphereColor,
            _ => null,
        };
        if (property is not null && args.Owner is SceneEnvironmentEditOwner owner)
        {
            owner.Apply(property, args.Color);
        }
    }

    private void NumberEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.EditOwner.BeginEditSession(field, args.InteractionKind);
        }
    }

    private void NumberEditCompleted(object? sender, NumberBoxEditSessionEventArgs args) => this.ViewModel?.EditOwner.CompleteEditSession(args);

    private void VectorEditStarted(object? sender, VectorBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.EditOwner.BeginEditSession($"{field}.{args.Component}", args.InteractionKind);
        }
    }

    private void VectorEditCompleted(object? sender, VectorBoxEditSessionEventArgs args)
        => this.ViewModel?.EditOwner.CompleteEditSession(new(args.InteractionKind, args.CompletionKind));
}
