// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.WorldEditor.ViewModels;

namespace Oxygen.Editor.WorldEditor.Views;

/// <summary>
/// Represents the view for displaying scene details in the World Editor.
/// </summary>
[ViewModel(typeof(SceneDetailsViewModel))]
public sealed partial class SceneDetailsView : UserControl
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SceneDetailsView"/> class.
    /// </summary>
    public SceneDetailsView()
    {
        this.InitializeComponent();
    }
}
