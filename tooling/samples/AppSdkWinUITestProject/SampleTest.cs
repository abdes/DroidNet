// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.UI.Xaml.Controls;
using Microsoft.VisualStudio.TestTools.UnitTesting.AppContainer;

namespace DroidNet.Samples.Tests;

[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class SampleTest
{
    [UITestMethod]
    public void TestMethod1()
    {
        var grid = new Grid();

        _ = grid.Should().NotBeNull();
    }
}
