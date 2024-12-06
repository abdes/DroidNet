// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Oxygen.Editor.WorldEditor.ViewModels;

namespace Oxygen.Editor.WorldEditor.Views;

/// <summary>
/// Represents the view for rendering content in the World Editor.
/// </summary>
[ViewModel(typeof(RendererViewModel))]
public sealed partial class RendererView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="RendererView"/> class.
    /// </summary>
    public RendererView()
    {
        this.InitializeComponent();
    }
}
