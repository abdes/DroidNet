// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public partial class PropertiesExpander
{
    /// <summary>
    /// Fires when the SettingsExpander is opened
    /// </summary>
    public event EventHandler? Expanded;

    /// <summary>
    /// Fires when the expander is closed
    /// </summary>
    public event EventHandler? Collapsed;
}
