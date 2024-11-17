// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Routing.Debugger.UI.Docks;

/// <summary>
/// A simple content control that displays the embedded application content or
/// an error message if content loading fails.
/// </summary>
[ViewModel(typeof(EmbeddedAppViewModel))]
public sealed partial class EmbeddedAppView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="EmbeddedAppView"/> class.
    /// </summary>
    public EmbeddedAppView()
    {
        this.InitializeComponent();
    }

    /// <inheritdoc/>
    public override string ToString() => $"{nameof(EmbeddedAppView)}]";
}
