// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Samples.Tests;

[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class SampleTest : VisualUserInterfaceTests
{
    [TestMethod]
    public Task ControlIsRealizedInTheSharedWindow() => EnqueueAsync(async () =>
    {
        var grid = new Grid { Width = 160, Height = 80 };
        await LoadTestContentAsync(grid).ConfigureAwait(true);

        _ = grid.IsLoaded.Should().BeTrue();
        _ = grid.XamlRoot.Should().NotBeNull();
        _ = grid.ActualWidth.Should().BeGreaterThan(0);
    });
}
