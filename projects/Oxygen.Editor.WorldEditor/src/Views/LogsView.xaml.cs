// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.WorldEditor.ViewModels;

namespace Oxygen.Editor.WorldEditor.Views;

/// <summary>
/// Represents the view for displaying logs in the World Editor.
/// </summary>
[ViewModel(typeof(LogsViewModel))]
public sealed partial class LogsView : UserControl
{
    /// <summary>
    /// Initializes a new instance of the <see cref="LogsView"/> class.
    /// </summary>
    public LogsView()
    {
        this.InitializeComponent();
    }
}
