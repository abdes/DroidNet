// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Oxygen.Editor.Pickers;

namespace Oxygen.Editor.SceneInspector.Components.Instances;

/// <summary>Scene Instance section view.</summary>
[ViewModel(typeof(SceneInstanceViewModel))]
public sealed partial class SceneInstanceView
{
    /// <summary>Initializes a new instance of the <see cref="SceneInstanceView"/> class.</summary>
    public SceneInstanceView() => this.InitializeComponent();

    private async void OnSourcePickerOpening(object? sender, EventArgs args)
    {
        _ = sender;
        _ = args;
        Debug.Assert(this.ViewModel is not null, "ViewModel should not be null when the source picker opens.");
        await this.ViewModel.PrepareChoicesAsync().ConfigureAwait(true);
    }

    private async void OnSourceChoiceInvoked(object? sender, AssetChoice choice)
    {
        _ = sender;
        Debug.Assert(this.ViewModel is not null, "ViewModel should not be null when handling a source choice.");
        if (choice.Asset is { } source)
        {
            await this.ViewModel.SwapAsync(source).ConfigureAwait(true);
        }
    }
}
