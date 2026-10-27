// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;

namespace Oxygen.Editor.Controls.PropertyEditors;

/// <summary>A command an overridden row offers in its context menu, such as <c>Apply to Watchtower</c>.</summary>
/// <param name="Label">The menu item's text.</param>
/// <param name="Command">The command the menu item runs.</param>
public sealed record OverrideAction(string Label, ICommand Command);
