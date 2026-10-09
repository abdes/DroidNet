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

/// <summary>Composes the backdrop choice and the sky sphere and background fields it shows.</summary>
[ViewModel(typeof(BackdropSectionViewModel))]
public sealed partial class BackdropSectionView
{
    /// <summary>Initializes a new instance of the <see cref="BackdropSectionView"/> class.</summary>
    public BackdropSectionView()
    {
        this.InitializeComponent();
        this.ViewModelChanged += (_, _) => this.CubemapFlyout.Hide();
        this.Unloaded += (_, _) => this.CubemapFlyout.Hide();
    }

    /// <summary>Registers this view's controls by stable browsing identity.</summary>
    /// <param name="registry">The host's presentation registry.</param>
    internal void Register(InspectorSceneFieldRegistry registry)
    {
        registry.Section("Backdrop", this, this.BackdropSection);
        registry.Field("Backdrop", this.BackdropCard);
        registry.Field("SkySphereCubemap", this.CubemapCard);
        registry.Field("SkySphereRotation", this.RotationCard);
        registry.Field("SolidColor", this.SolidColorCard);
        registry.Field("SolidColorLightsScene", this.SolidColorLightsSceneCard);
        registry.Field("SkySphereIlluminance", this.IlluminanceCard);
        registry.Field("SkySphereIntensity", this.IntensityCard);
        registry.Field("SkySphereTint", this.TintCard);
    }

    private void CubemapItem_Click(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: AssetPickerRow row })
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
            "SolidColor" => SceneSkyFields.SolidColor,
            "SkySphereTint" => SceneSkyFields.SkySphereTintRgb,
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
