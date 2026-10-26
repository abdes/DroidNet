// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     Describes a command button of a dialog's footer. The button is enabled while the command can execute.
/// </summary>
/// <param name="Label">The button text, which is also its accessible name.</param>
/// <param name="Command">The command the button executes, without a parameter.</param>
public sealed record DialogCommand(string Label, ICommand Command);
