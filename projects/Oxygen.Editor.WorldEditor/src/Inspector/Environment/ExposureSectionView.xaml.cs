// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Inspector.Presentation;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns exposure disclosure, numeric and texture flyout adapters.</summary>
[ViewModel(typeof(ExposureSectionViewModel))]
public sealed partial class ExposureSectionView
{
    /// <summary>Initializes a new instance of the <see cref="ExposureSectionView"/> class.</summary>
    public ExposureSectionView()
    {
        this.InitializeComponent();
        this.ViewModelChanged += (_, _) => this.MeteringMaskFlyout.Hide();
        this.Unloaded += (_, _) => this.MeteringMaskFlyout.Hide();
    }

    /// <summary>Registers exposure fields, applicability notes and disclosures.</summary>
    /// <param name="registry">The host's presentation registry.</param>
    internal void Register(InspectorSceneFieldRegistry registry)
    {
        registry.Section("Exposure", this, this.ExposureSection);
        registry.Disclosures.Add("MeteringLimits", this.MeteringLimitsDisclosure);
        registry.Disclosures.Add("Adaptation", this.AdaptationDisclosure);
        registry.Disclosures.Add("HistogramCalibration", this.HistogramCalibrationDisclosure);
        registry.Disclosures.Add("ExposureShaping", this.ExposureShapingDisclosure);
        registry.Field("ExposureEnabled", this.ExposureEnabledCard);
        registry.Field("ExposureMode", this.ExposureModeCard);
        registry.Field("ManualExposureEv", this.ManualExposureEvCard);
        registry.Field("ExposureCompensation", this.ExposureCompensationCard);
        registry.Field("AutoExposureMeteringMode", this.AutoExposureMeteringModeCard, this.AutoExposureMeteringModeApplicabilityNote);
        registry.Field("AutoExposureMinEv", this.AutoExposureMinEvCard);
        registry.Field("AutoExposureMaxEv", this.AutoExposureMaxEvCard);
        registry.Field("AutoExposureTargetLuminance", this.AutoExposureTargetLuminanceCard);
        registry.Field("AutoExposureSpotMeterRadius", this.AutoExposureSpotMeterRadiusCard);
        registry.Field("AutoExposureSpeedUp", this.AutoExposureSpeedUpCard);
        registry.Field("AutoExposureSpeedDown", this.AutoExposureSpeedDownCard);
        registry.Field("AutoExposureTransitionDistanceEv", this.AutoExposureTransitionDistanceEvCard);
        registry.Field("ExposureKey", this.ExposureKeyCard);
        registry.Field("AutoExposureLowPercentile", this.AutoExposureLowPercentileCard);
        registry.Field("AutoExposureHighPercentile", this.AutoExposureHighPercentileCard);
        registry.Field("AutoExposureMinLogLuminance", this.AutoExposureMinLogLuminanceCard);
        registry.Field("AutoExposureLogLuminanceRange", this.AutoExposureLogLuminanceRangeCard);
        registry.Field("AutoExposureBlackInfluence", this.AutoExposureBlackInfluenceCard);
        registry.Field("AutoExposureMeteringMask", this.AutoExposureMeteringMaskCard, this.AutoExposureMeteringMaskApplicabilityNote);
        registry.Field("AutoExposureCompensationCurve", this.AutoExposureCompensationCurveCard, this.AutoExposureCompensationCurveApplicabilityNote);
    }

    private void NumberEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.EditOwner.BeginEditSession(field, args.InteractionKind);
        }
    }

    private void NumberEditCompleted(object? sender, NumberBoxEditSessionEventArgs args) => this.ViewModel?.EditOwner.CompleteEditSession(args);

    private void ClearMeteringMask_Click(object sender, RoutedEventArgs args) => this.ViewModel?.SetMeteringMask(textureUri: null);

    private void MeteringMaskItem_Click(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: AssetPickerRow row } && row.Item.IsEnabled)
        {
            this.ViewModel?.SetMeteringMask(row.Item.Uri);
            this.MeteringMaskFlyout.Hide();
        }
    }
}
