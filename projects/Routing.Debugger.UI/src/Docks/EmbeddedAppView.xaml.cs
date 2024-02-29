// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Docks;

using DroidNet.Routing.Generators;

/// <summary>
/// A simple content control that displays the embedded application content or
/// an error message if content loading fails.
/// </summary>
[ViewModel(typeof(EmbeddedAppViewModel))]
public sealed partial class EmbeddedAppView
{
    public EmbeddedAppView() => this.InitializeComponent();

    public override string? ToString() => $"{nameof(EmbeddedAppView)}]";
}
