// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.ToolBars.Tests;

[ExcludeFromCodeCoverage]
[TestClass]
[TestCategory("UITest")]
public class ToolBarItemFocusTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task Separator_IsNotATabStop() => EnqueueAsync(async () =>
    {
        var separator = new ToolBarSeparator();
        await LoadTestContentAsync(new ToolBar { PrimaryItems = { new ToolBarButton { Label = "Before" }, separator } }).ConfigureAwait(true);

        // A control is a tab stop by default; WinUI's AppBarSeparator style turns that off.
        _ = separator.IsTabStop.Should().BeFalse();
    });

    [TestMethod]
    public Task ElementHost_IsNotATabStopButItsContentIs() => EnqueueAsync(async () =>
    {
        var hosted = new Button { Content = "Hosted" };
        var host = new ToolBarElementHost { Content = hosted };
        await LoadTestContentAsync(new ToolBar { PrimaryItems = { host } }).ConfigureAwait(true);

        _ = host.IsTabStop.Should().BeFalse();
        _ = hosted.IsTabStop.Should().BeTrue();
    });
}
