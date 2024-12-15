// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Input;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
/// Represents a custom grid that supports setting an input cursor, which is <see langword="protected"/> for a <see cref="Grid"/>.
/// </summary>
/// <remarks>
/// We need to change the cursor when we are changing the value in a number box through mouse dragging.
/// </remarks>
internal partial class CustomGrid : Grid
{
    /// <summary>
    /// Gets or sets the input cursor for the grid.
    /// </summary>
    public InputCursor? InputCursor
    {
        get => this.ProtectedCursor;
        set => this.ProtectedCursor = value;
    }
}
