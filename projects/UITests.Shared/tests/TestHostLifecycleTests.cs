// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Tests;

/// <summary>Exercises window-free dispatch and realized-content reuse in the shared host.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
public sealed class TestHostLifecycleTests : VisualUserInterfaceTests
{
    /// <summary>Fixture setup and cleanup can run without loading content.</summary>
    /// <returns>The dispatched operation.</returns>
    [TestMethod]
    public Task DispatcherOnlyExecution() => EnqueueAsync(() =>
    {
        _ = VisualUserInterfaceTestsApp.DispatcherQueue.HasThreadAccess.Should().BeTrue();
        _ = VisualUserInterfaceTestsApp.ContentRoot.Should().BeNull();
        VisualUserInterfaceTestsApp.ContentRoot = null;
    });

    /// <summary>Replacing realized content preserves the window and unloads its previous content.</summary>
    /// <returns>The content lifecycle check.</returns>
    [TestMethod]
    public Task ReusesWindowAcrossContentLoads() => EnqueueAsync(async () =>
    {
        var first = new TextBlock { Text = "First content" };
        await LoadTestContentAsync(first).ConfigureAwait(true);
        var window = VisualUserInterfaceTestsApp.MainWindow;
        _ = first.XamlRoot.Should().NotBeNull();

        await UnloadTestContentAsync(first).ConfigureAwait(true);
        _ = first.IsLoaded.Should().BeFalse();
        _ = VisualUserInterfaceTestsApp.ContentRoot.Should().BeNull();

        var second = new TextBlock { Text = "Second content" };
        await LoadTestContentAsync(second).ConfigureAwait(true);
        _ = VisualUserInterfaceTestsApp.MainWindow.Should().BeSameAs(window);
        _ = window.Content.Should().BeSameAs(second);
        _ = second.XamlRoot.Should().NotBeNull();
    });
}
