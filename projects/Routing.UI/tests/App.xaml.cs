// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.UI;

using Microsoft.UI.Xaml;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

/// <summary>
/// Provides application-specific behavior to supplement the default Application
/// class.
/// </summary>
public partial class TestApp
{
    /// <summary>
    /// Initializes a new instance of the <see cref="TestApp" /> class.
    /// </summary>
    public TestApp() => this.InitializeComponent();

    /// <summary>
    /// Invoked when the application is launched normally by the end user.  Other entry
    /// points
    /// will be used such as when the application is launched to open a specific file.
    /// </summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Microsoft.VisualStudio.TestPlatform.TestExecutor.UnitTestClient.CreateDefaultUI();

        // Replace back with e.Arguments when https://github.com/microsoft/microsoft-ui-xaml/issues/3368 is fixed
        Microsoft.VisualStudio.TestPlatform.TestExecutor.UnitTestClient.Run(Environment.CommandLine);
    }
}
