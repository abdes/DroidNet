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
    // The glyphs are owned by the Expander template (Expander.xaml): down arrow when expanded, right arrow when collapsed.
    private const string ExpandedGlyph = "\uE70D";
    private const string CollapsedGlyph = "\uE76C";

    private Controls.Expander? expander;
    private TestVisualStateManager? vsm;

    [TestMethod]
    public Task InitializesCollapsedByDefault_Async()
        => EnqueueAsync(
            () => _ = this.expander!.IsExpanded.Should().BeFalse());

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
    [DataRow(true, ExpandedGlyph)]
    [DataRow(false, CollapsedGlyph)]
    public Task IconShowsTheGlyphOfTheCurrentState_Async(bool expanded, string glyph) => EnqueueAsync(
    () =>
    {
        // Setup: arrive at the state from the opposite one, so the glyph has to change.
        this.expander!.IsExpanded = !expanded;

        // Act
        this.expander.IsExpanded = expanded;

        // Assert
        var icon = this.expander.FindDescendant<FontIcon>(e => string.Equals(e.Name, "Icon", StringComparison.Ordinal));
        _ = icon.Should().NotBeNull();
        _ = icon!.Glyph.Should().Be(glyph);
    });

    [TestMethod]
    public Task UpdatesVisualStateToExpanded_Async() => EnqueueAsync(
    () =>
    {
        // Act
        this.expander!.IsExpanded = true;

        // Assert
        _ = this.vsm!.GetCurrentStates(this.expander).Should().Contain(Controls.Expander.ExpandedVisualState);
        _ = this.vsm.GetCurrentStates(this.expander).Should().NotContain(Controls.Expander.CollapsedVisualState);
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
        _ = this.vsm!.GetCurrentStates(this.expander).Should().Contain(Controls.Expander.CollapsedVisualState);
        // The state manager logs every state entered; the latest one is the current state.
        _ = this.vsm.GetCurrentStates(this.expander)!.Last().Should().Be(Controls.Expander.CollapsedVisualState);
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
                    e => string.Equals(e.Name, Controls.Expander.ActiveElement, StringComparison.Ordinal));
                _ = vsmTarget.Should().NotBeNull();
                VisualStateManager.SetCustomVisualStateManager(vsmTarget, this.vsm);

                taskCompletionSource.SetResult();
            });
        await taskCompletionSource.Task.ConfigureAwait(true);
    }
}
