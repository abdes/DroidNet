// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Tests;

/// <summary>Runs inspector controls in the repository's packaged WinUI test host.</summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "The generated WinUI Application class is public.")]
public partial class App
{
    /// <summary>Initializes a new instance of the <see cref="App"/> class.</summary>
    public App() => this.InitializeComponent();
}
