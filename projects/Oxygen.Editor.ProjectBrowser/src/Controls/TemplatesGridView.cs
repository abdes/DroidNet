// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// A custom control for viewing the project templates in a grid which can be
/// sorted by the last time the template was used.
/// </summary>
public sealed partial class TemplatesGridView : GridView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="TemplatesGridView"/> class.
    /// </summary>
    public TemplatesGridView()
    {
        this.DefaultStyleKey = typeof(TemplatesGridView);

        this.Loaded += (_, _) => this.SelectedIndex = this.Items.Count > 0 ? 0 : -1;
    }
}
