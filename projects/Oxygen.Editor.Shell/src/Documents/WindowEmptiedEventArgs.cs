// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>The window whose last tab has left it.</summary>
/// <param name="window">The window, which has no tab now.</param>
public sealed class WindowEmptiedEventArgs(WindowId window) : EventArgs
{
    /// <summary>Gets the window, which has no tab now.</summary>
    public WindowId Window { get; } = window;
}
