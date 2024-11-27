// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ProjectBrowser.Templates;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// Provides data for the <see cref="ProjectTemplatesGrid.ItemActivated"/> event.
/// </summary>
public class TemplateItemActivatedEventArgs(ITemplateInfo templateInfo) : EventArgs
{
    /// <summary>
    /// Gets the template information associated with the activated item.
    /// </summary>
    public ITemplateInfo TemplateInfo => templateInfo;
}
