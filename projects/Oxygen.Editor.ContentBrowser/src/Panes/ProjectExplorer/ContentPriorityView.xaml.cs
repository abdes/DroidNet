// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>Displays the explicitly requested content-priority editor.</summary>
[ViewModel(typeof(ContentPriorityViewModel))]
public sealed partial class ContentPriorityView
{
    /// <summary>Initializes a new instance of the <see cref="ContentPriorityView"/> class.</summary>
    public ContentPriorityView() => this.InitializeComponent();
}
