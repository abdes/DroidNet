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
public sealed partial class OutputConsoleDemoView : Page
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
        if (e.ClickedItem is string logLevel)
        {
            this.ViewModel?.MakeLogCommand.Execute(logLevel);
        }
    }
}
