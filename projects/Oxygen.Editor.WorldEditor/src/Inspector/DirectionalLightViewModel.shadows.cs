// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Owns cascade input applicability without rewriting stored authoring values.</summary>
public sealed partial class DirectionalLightViewModel
{
    /// <summary>Gets a value indicating whether generated distribution affects every selected light.</summary>
    public bool IsDistributionExponentApplicable => this.HasUniformSplitMode(DirectionalCsmSplitMode.Generated)
        && this.MinimumCascadeCount > 1;

    /// <summary>Gets a value indicating whether the first manual boundary affects every selected light.</summary>
    public bool IsCascadeDistance1Applicable => this.IsCascadeDistanceApplicable(1);

    /// <summary>Gets a value indicating whether the second manual boundary affects every selected light.</summary>
    public bool IsCascadeDistance2Applicable => this.IsCascadeDistanceApplicable(2);

    /// <summary>Gets a value indicating whether the third manual boundary affects every selected light.</summary>
    public bool IsCascadeDistance3Applicable => this.IsCascadeDistanceApplicable(3);

    /// <summary>Gets a value indicating whether the fourth manual boundary affects every selected light.</summary>
    public bool IsCascadeDistance4Applicable => this.IsCascadeDistanceApplicable(4);

    private int MinimumCascadeCount => !this.cascadeCountBinding.IsMixed ? this.cascadeCountBinding.Value
        : this.selectedItems?.SelectMany(node => node.Components.OfType<DirectionalLightComponent>())
            .Select(light => light.CascadeCount).DefaultIfEmpty(0).Min() ?? 0;

    private bool HasUniformSplitMode(DirectionalCsmSplitMode mode)
        => this.splitModeBinding.HasValue && !this.splitModeBinding.IsMixed && this.SplitMode == mode;

    private bool IsCascadeDistanceApplicable(int cascade)
        => this.HasUniformSplitMode(DirectionalCsmSplitMode.ManualDistances) && this.MinimumCascadeCount > cascade;

    private void NotifyCascadeApplicabilityChanged()
    {
        this.OnPropertyChanged(nameof(this.IsDistributionExponentApplicable));
        this.OnPropertyChanged(nameof(this.IsCascadeDistance1Applicable));
        this.OnPropertyChanged(nameof(this.IsCascadeDistance2Applicable));
        this.OnPropertyChanged(nameof(this.IsCascadeDistance3Applicable));
        this.OnPropertyChanged(nameof(this.IsCascadeDistance4Applicable));
    }
}
