// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Routing.WinUI.Tests;

/// <summary>
/// Provides application-specific behavior to supplement the default Application
/// class.
/// </summary>
[ExcludeFromCodeCoverage]
public partial class App
{
    /// <summary>
    /// Initializes a new instance of the <see cref="App" /> class. This is the first line of
    /// authored code executed, and as such is the logical equivalent of main() or WinMain().
    /// </summary>
    public App()
    {
        this.InitializeComponent();
    }
}
