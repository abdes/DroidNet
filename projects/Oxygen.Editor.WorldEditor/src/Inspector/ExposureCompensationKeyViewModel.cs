// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Editable view state for one authored exposure compensation key.</summary>
internal sealed partial class ExposureCompensationKeyViewModel : ObservableObject
{
    public ExposureCompensationKeyViewModel(ExposureCompensationKeyData key)
    {
        this.MeteredEv = key.MeteredEv;
        this.CompensationEv = key.CompensationEv;
    }

    public event EventHandler? Changed;

    [ObservableProperty]
    public partial float MeteredEv { get; set; }

    [ObservableProperty]
    public partial float CompensationEv { get; set; }

    partial void OnMeteredEvChanged(float value) => this.Changed?.Invoke(this, EventArgs.Empty);

    partial void OnCompensationEvChanged(float value) => this.Changed?.Invoke(this, EventArgs.Empty);

    public ExposureCompensationKeyData ToData() => new(this.MeteredEv, this.CompensationEv);
}
