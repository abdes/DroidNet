// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.SceneInspector.Environment;

/// <summary>Owns the related tone mapping, grading and bloom values, without creating another edit lifetime.</summary>
public sealed partial class PostProcessingSectionViewModel : ObservableObject
{
    /// <summary>Initializes a new instance of the <see cref="PostProcessingSectionViewModel"/> class.</summary>
    /// <param name="owner">The borrowed scene edit lifetime.</param>
    internal PostProcessingSectionViewModel(SceneEnvironmentEditOwner owner) => this.EditOwner = owner;

    [ObservableProperty]
    public partial ToneMappingMode ToneMapping { get; set; }

    [ObservableProperty]
    public partial double DisplayGamma { get; set; }

    [ObservableProperty]
    public partial double Saturation { get; set; }

    [ObservableProperty]
    public partial double Contrast { get; set; }

    [ObservableProperty]
    public partial double VignetteIntensity { get; set; }

    [ObservableProperty]
    public partial double BloomIntensity { get; set; }

    [ObservableProperty]
    public partial double BloomThreshold { get; set; }

    /// <summary>Gets the borrowed scene edit owner.</summary>
    public SceneEnvironmentEditOwner EditOwner { get; }

    /// <summary>Gets existing tone mapper choices in their original order.</summary>
    public IReadOnlyList<ToneMappingMode> ToneMappingModes { get; } = [ToneMappingMode.AcesFitted, ToneMappingMode.Filmic, ToneMappingMode.Reinhard, ToneMappingMode.None];

    /// <summary>Gets a value indicating whether mapper-dependent fields apply.</summary>
    public bool IsToneMappingControlsVisible => this.ToneMapping != ToneMappingMode.None;

    /// <summary>Gets display-transfer feedback.</summary>
    public InspectorFieldDiagnostic DisplayGammaDiagnostic => this.EditOwner.Diagnostics.Get(SceneProperties.SceneEnvironment.DisplayGamma.Id);

    /// <summary>Gets grading feedback.</summary>
    public InspectorFieldDiagnostic SaturationDiagnostic => this.EditOwner.Diagnostics.Get(SceneProperties.SceneEnvironment.Saturation.Id);

    /// <summary>Gets grading feedback.</summary>
    public InspectorFieldDiagnostic ContrastDiagnostic => this.EditOwner.Diagnostics.Get(SceneProperties.SceneEnvironment.Contrast.Id);

    /// <summary>Gets vignette feedback.</summary>
    public InspectorFieldDiagnostic VignetteIntensityDiagnostic => this.EditOwner.Diagnostics.Get(SceneProperties.SceneEnvironment.VignetteIntensity.Id);

    /// <summary>Gets bloom feedback.</summary>
    public InspectorFieldDiagnostic BloomIntensityDiagnostic => this.EditOwner.Diagnostics.Get(SceneProperties.SceneEnvironment.BloomIntensity.Id);

    /// <summary>Gets bloom feedback.</summary>
    public InspectorFieldDiagnostic BloomThresholdDiagnostic => this.EditOwner.Diagnostics.Get(SceneProperties.SceneEnvironment.BloomThreshold.Id);

    /// <summary>Displays authored effects under the parent's model-refresh guard.</summary>
    /// <param name="value">The authored effect snapshot.</param>
    internal void Refresh(PostProcessEnvironmentData value)
    {
        this.ToneMapping = value.ToneMapper;
        this.DisplayGamma = value.DisplayGamma;
        this.Saturation = value.Saturation;
        this.Contrast = value.Contrast;
        this.VignetteIntensity = value.VignetteIntensity;
        this.BloomIntensity = value.BloomIntensity;
        this.BloomThreshold = value.BloomThreshold;
    }

    partial void OnToneMappingChanged(ToneMappingMode value)
    {
        this.OnPropertyChanged(nameof(this.IsToneMappingControlsVisible));
        this.EditOwner.Apply(SceneProperties.SceneEnvironment.ToneMapping, value);
    }

    partial void OnDisplayGammaChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.DisplayGamma, value);

    partial void OnSaturationChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.Saturation, value);

    partial void OnContrastChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.Contrast, value);

    partial void OnVignetteIntensityChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.VignetteIntensity, value);

    partial void OnBloomIntensityChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.BloomIntensity, value);

    partial void OnBloomThresholdChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.BloomThreshold, value);
}
