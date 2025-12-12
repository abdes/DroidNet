// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.OutputConsole;

/// <summary>
///     Page that hosts the OutputConsole demo UI.
/// </summary>
[ViewModel(typeof(OutputConsoleDemoViewModel))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "must be public due to source generated ViewModel property")]
public sealed partial class OutputConsoleDemoView
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="OutputConsoleDemoView"/> class.
    /// </summary>
    public OutputConsoleDemoView()
    {
        this.InitializeComponent();
    }

    private void GridView_ItemClick(object sender, ItemClickEventArgs e)
    {
        _ = sender; // unused

        if (e.ClickedItem is string logLevel)
        {
            this.ViewModel?.MakeLogCommand.Execute(logLevel);
        }
    }
}
