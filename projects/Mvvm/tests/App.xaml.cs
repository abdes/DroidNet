// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm;

using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;

/// <summary>Provides application-specific behavior to supplement the default Application class.</summary>
[ExcludeFromCodeCoverage]
public partial class App
{
    /// <summary>Initializes a new instance of the <see cref="App" /> class.</summary>
    public App() => this.InitializeComponent();

    /// <inheritdoc />
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Microsoft.VisualStudio.TestPlatform.TestExecutor.UnitTestClient.CreateDefaultUI();

        // Replace back with e.Arguments when https://github.com/microsoft/microsoft-ui-xaml/issues/3368 is fixed.
        // Last checked on 03/19/2024
        Microsoft.VisualStudio.TestPlatform.TestExecutor.UnitTestClient.Run(Environment.CommandLine);
    }
}
