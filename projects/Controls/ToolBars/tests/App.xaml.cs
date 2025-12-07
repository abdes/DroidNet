// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;

namespace DroidNet.Controls.ToolBars.Tests;

[ExcludeFromCodeCoverage]
public sealed partial class App : VisualUserInterfaceTestsApp
{
    /// <summary>
    /// Initializes a new instance of the <see cref="App"/> class and loads XAML resources.
    /// </summary>
    public App()
    {
        this.InitializeComponent();
    }
}
