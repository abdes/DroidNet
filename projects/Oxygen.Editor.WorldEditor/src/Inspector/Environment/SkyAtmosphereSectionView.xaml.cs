// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Oxygen.Editor.World.Inspector.Controls;
using Oxygen.Editor.World.Inspector.Presentation;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns atmosphere composition, numeric adapters and Aerial Start realization/focus.</summary>
[ViewModel(typeof(SkyAtmosphereSectionViewModel))]
public sealed partial class SkyAtmosphereSectionView
{
    private bool focusPending;

    /// <summary>Initializes a new instance of the <see cref="SkyAtmosphereSectionView"/> class.</summary>
    public SkyAtmosphereSectionView()
    {
        this.InitializeComponent();
        this.AerialStartInput.Loaded += (_, _) => this.TryFocusPending();
        this.ViewModelChanged += (_, _) => this.focusPending = false;
        this.Unloaded += (_, _) => this.focusPending = false;
    }

    /// <summary>Occurs only after the requested field accepts focus.</summary>
    internal event EventHandler? FieldFocused;

    /// <summary>Registers this view's controls and disclosures by stable browsing identity.</summary>
    /// <param name="registry">The host's presentation registry.</param>
    internal void Register(InspectorSceneFieldRegistry registry)
    {
        registry.Section("SkyAtmosphere", this, this.SkyAtmosphereSection);
        registry.Disclosures.Add("PlanetGround", this.PlanetGroundDisclosure);
        registry.Disclosures.Add("Scattering", this.ScatteringDisclosure);
        registry.Disclosures.Add("AerialPerspective", this.AerialPerspectiveDisclosure);
        registry.Field("AtmosphereEnabled", this.AtmosphereEnabledCard);
        registry.Field("SunDiskEnabled", this.SunDiskEnabledCard);
        registry.Field("SkyLuminance", this.SkyLuminanceCard);
        registry.Field("PlanetRadiusKm", this.PlanetRadiusKmCard);
        registry.Field("AtmosphereHeightKm", this.AtmosphereHeightKmCard);
        registry.Field("GroundAlbedo", this.GroundAlbedoCard);
        registry.Field("RayleighScaleHeightKm", this.RayleighScaleHeightKmCard);
        registry.Field("MieScaleHeightKm", this.MieScaleHeightKmCard);
        registry.Field("MieAnisotropy", this.MieAnisotropyCard);
        registry.Field("AerialPerspectiveDistanceScale", this.AerialPerspectiveDistanceScaleCard);
        registry.Field("AerialScatteringStrength", this.AerialScatteringStrengthCard);
        registry.Field("AerialPerspectiveStartDepthMeters", this.AerialStartCard);
        registry.Field("HeightFogContribution", this.HeightFogContributionCard);
    }

    /// <summary>Realizes and focuses the original input, signalling only successful focus.</summary>
    internal void FocusAerialStart()
    {
        this.focusPending = true;
        this.AerialPerspectiveDisclosure.IsExpanded = true;
        _ = this.SkyAtmosphereSection.BringItemIntoView(this.AerialPerspectiveDisclosure);
        this.TryFocusPending();
        _ = this.DispatcherQueue.TryEnqueue(this.TryFocusPending);
    }

    private void TryFocusPending()
    {
        if (this.focusPending && this.IsLoaded && this.AerialStartInput.IsLoaded)
        {
            this.AerialStartInput.StartBringIntoView();
            if (this.AerialStartInput.Focus(FocusState.Programmatic))
            {
                this.focusPending = false;
                this.FieldFocused?.Invoke(this, EventArgs.Empty);
            }
        }
    }

    private void OnRgbColorPicked(object? sender, InspectorRgbColorPickedEventArgs args)
    {
        if (args.Owner is SceneEnvironmentEditOwner owner)
        {
            owner.Apply(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo, args.Color);
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

    private void AerialStartValidate(object? sender, ValidationEventArgs<float> args) => args.IsValid = this.ViewModel?.ValidateAerialStart(args.NewValue) == true;

    private void VectorEditStarted(object? sender, VectorBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.EditOwner.BeginEditSession($"{field}.{args.Component}", args.InteractionKind);
        }
    }

    private void VectorEditCompleted(object? sender, VectorBoxEditSessionEventArgs args) => this.ViewModel?.EditOwner.CompleteEditSession(new(args.InteractionKind, args.CompletionKind));
}
