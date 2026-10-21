// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Oxygen.Editor.World.Inspector.Presentation;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Composes the smaller related effects without redundant per-effect models.</summary>
[ViewModel(typeof(PostProcessingSectionViewModel))]
public sealed partial class PostProcessingSectionView
{
    /// <summary>Initializes a new instance of the <see cref="PostProcessingSectionView"/> class.</summary>
    public PostProcessingSectionView() => this.InitializeComponent();

    /// <summary>Registers the three related effects using their existing native sections.</summary>
    /// <param name="registry">The host's presentation registry.</param>
    internal void Register(InspectorSceneFieldRegistry registry)
    {
        registry.Section("ToneMapping", this, this.ToneMappingSection);
        registry.Section("ColorGrading", this, this.ColorGradingSection);
        registry.Section("Bloom", this, this.BloomSection);
        registry.Field("ToneMapper", this.ToneMapperCard);
        registry.Field("DisplayGamma", this.DisplayGammaCard);
        registry.Field("Saturation", this.SaturationCard);
        registry.Field("Contrast", this.ContrastCard);
        registry.Field("VignetteIntensity", this.VignetteIntensityCard);
        registry.Field("BloomIntensity", this.BloomIntensityCard);
        registry.Field("BloomThreshold", this.BloomThresholdCard);
    }

    private void NumberEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.EditOwner.BeginEditSession(field, args.InteractionKind);
        }
    }

    private void NumberEditCompleted(object? sender, NumberBoxEditSessionEventArgs args) => this.ViewModel?.EditOwner.CompleteEditSession(args);
}
