// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace UnitTest;

using FluentAssertions;
using Microsoft.UI.Xaml.Controls;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Microsoft.VisualStudio.TestTools.UnitTesting.AppContainer;

[TestClass]
public class SampleTest
{
    [UITestMethod]
    public void TestMethod1()
    {
        var grid = new Grid();

        _ = grid.Should().NotBeNull();
    }
}
