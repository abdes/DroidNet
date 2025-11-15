// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Tests.Expander;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Expander")]
[TestCategory("UITest")]
public class ExpanderTests : VisualUserInterfaceTests
{
    private Controls.Expander? expander;
    private TestVisualStateManager? vsm;

    [TestMethod]
    public Task InitializesCollapsedByDefault_Async()
        => EnqueueAsync(
            () => _ = this.expander!.IsExpanded.Should().BeFalse());

    [TestMethod]
    public Task TransitionsToExpandedState_Async() => EnqueueAsync(
    () =>
    {
        // Act
        this.expander!.IsExpanded = true;

        // Assert
        _ = this.expander.IsExpanded.Should().BeTrue();
    });

    [TestMethod]
    public Task TransitionsToCollapsedState_Async() => EnqueueAsync(
    () =>
    {
        // Act
        this.expander!.IsExpanded = true;
        this.expander.IsExpanded = false;

        // Assert
        _ = this.expander.IsExpanded.Should().BeFalse();
    });

    [TestMethod]
    public Task FiresExpandEventWhenToggleCalled_Async() => EnqueueAsync(
    () =>
    {
        // Setup
        var eventFired = false;
        this.expander!.Expand += (_, _) => eventFired = true;

        // Act
        this.expander.Toggle();

        // Assert
        _ = eventFired.Should().BeTrue();
    });

    [TestMethod]
    public Task FiresCollapseEventWhenToggleCalled_Async() => EnqueueAsync(
    () =>
    {
        // Setup
        this.expander!.IsExpanded = true;
        var eventFired = false;
        this.expander.Collapse += (_, _) => eventFired = true;

        // Act
        this.expander.Toggle();

        // Assert
        _ = eventFired.Should().BeTrue();
    });

    [TestMethod]
    public Task IconGlyphChangesWhenExpanded_Async() => EnqueueAsync(
    () =>
    {
        // Act
        var icon = this.expander!.FindDescendant<FontIcon>(e => string.Equals(e.Name, "Icon", StringComparison.Ordinal));
        var initialGlyph = icon?.Glyph;
        this.expander!.IsExpanded = true;

        // Assert
        _ = icon.Should().NotBeNull();
        _ = icon!.Glyph.Should().NotBe(initialGlyph);
    });

    [TestMethod]
    public Task IconGlyphChangesWhenCollapsed_Async() => EnqueueAsync(
    () =>
    {
        // Setup
        this.expander!.IsExpanded = true;

        // Act
        var icon = this.expander.FindDescendant<FontIcon>(e => string.Equals(e.Name, "Icon", StringComparison.Ordinal));
        var initialGlyph = icon?.Glyph;
        this.expander.IsExpanded = false;

        // Assert
        _ = icon.Should().NotBeNull();
        _ = icon!.Glyph.Should().NotBe(initialGlyph);
    });

    [TestMethod]
    public Task UpdatesVisualStateToExpanded_Async() => EnqueueAsync(
    () =>
    {
        // Act
        this.expander!.IsExpanded = true;

        // Assert
        _ = this.vsm!.GetCurrentStates(this.expander).Should().Contain("Expanded");
    });

    [TestMethod]
    public Task UpdatesVisualStateToCollapsed_Async() => EnqueueAsync(
    () =>
    {
        // Setup
        this.expander!.IsExpanded = true;

        // Act
        this.expander.IsExpanded = false;

        // Assert
        _ = this.vsm!.GetCurrentStates(this.expander).Should().Contain("Collapsed");
    });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(false);

        var taskCompletionSource = new TaskCompletionSource();
        _ = EnqueueAsync(
            async () =>
            {
                this.expander = new Controls.Expander();
                await LoadTestContentAsync(this.expander).ConfigureAwait(true);

                this.vsm = new TestVisualStateManager();
                var vsmTarget = this.expander.FindDescendant<Grid>(
                    e => string.Equals(e.Name, "PART_ActiveElement", StringComparison.Ordinal));
                _ = vsmTarget.Should().NotBeNull();
                VisualStateManager.SetCustomVisualStateManager(vsmTarget, this.vsm);

                taskCompletionSource.SetResult();
            });
        await taskCompletionSource.Task.ConfigureAwait(true);
    }
}
