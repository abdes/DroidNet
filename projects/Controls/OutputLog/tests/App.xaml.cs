// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Controls.Tests;

/// <summary>Loads this suite's resources into the shared WinUI test application.</summary>
[ExcludeFromCodeCoverage]
public partial class TestApp
{
    /// <summary>Initializes a new instance of the <see cref="TestApp"/> class.</summary>
    public TestApp() => this.InitializeComponent();
}
