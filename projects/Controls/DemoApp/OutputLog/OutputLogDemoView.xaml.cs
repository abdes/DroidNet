// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.
namespace DroidNet.Controls.Demo.OutputLog;

/// <summary>
/// An empty page that can be used on its own or navigated to within a Frame.
/// </summary>
[ViewModel(typeof(OutputLogDemoViewModel))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "must be public due to source generated ViewModel property")]
public sealed partial class OutputLogDemoView : Page
{
    /// <summary>
    /// Initializes a new instance of the <see cref="OutputLogDemoView"/> class.
    /// </summary>
    public OutputLogDemoView()
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
