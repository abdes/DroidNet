// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// The View for the Project Layout pane in the <see cref="ContentBrowserView" />.
/// </summary>
[ViewModel(typeof(ProjectLayoutViewModel))]
public sealed partial class ProjectLayoutView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ProjectLayoutView"/> class.
    /// </summary>
    public ProjectLayoutView()
    {
        this.InitializeComponent();
    }
}
