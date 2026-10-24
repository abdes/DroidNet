// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.Output;

/// <summary>
///     The Output panel's view: the editor's log and the player's. The console's options are bound two-way to the
///     <see cref="OutputViewModel" />, which keeps the preferences.
/// </summary>
[ViewModel(typeof(OutputViewModel))]
public sealed partial class OutputView
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="OutputView" /> class.
    /// </summary>
    public OutputView()
    {
        this.InitializeComponent();
    }
}
