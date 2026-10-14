// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Editable view state for one authored exposure compensation key.</summary>
internal sealed partial class ExposureCompensationKeyViewModel(ExposureCompensationKeyData key) : ObservableObject
{
    public event EventHandler? Changed;

    [ObservableProperty]
    public partial double MeteredEv { get; set; } = key.MeteredEv;

    [ObservableProperty]
    public partial double CompensationEv { get; set; } = key.CompensationEv;

    public ExposureCompensationKeyData ToData() => new((float)this.MeteredEv, (float)this.CompensationEv);

    partial void OnMeteredEvChanged(double value) => this.Changed?.Invoke(this, EventArgs.Empty);

    partial void OnCompensationEvChanged(double value) => this.Changed?.Invoke(this, EventArgs.Empty);
}
