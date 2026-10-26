// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls.Tests.Expander;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Expander")]
[TestCategory("UITest")]
public class ExpanderInteractionTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task ActiveElement_IsHitTestable_Async() => EnqueueAsync(
    async () =>
    {
        // Setup
        var expander = new Controls.Expander();

        // Act
        await LoadTestContentAsync(expander).ConfigureAwait(true);

        // Assert
        var activeElement = expander.FindDescendant<Grid>(
            e => string.Equals(e.Name, Controls.Expander.ActiveElement, StringComparison.Ordinal));
        _ = activeElement.Should().NotBeNull();
        _ = activeElement!.Background.Should().BeOfType<SolidColorBrush>().Which.Color.Should().Be(Colors.Transparent);
    });

    [TestMethod]
    public Task AutomationPeer_ExposesExpandCollapsePattern_Async() => EnqueueAsync(
    async () =>
    {
        // Setup
        var expander = new Controls.Expander();
        await LoadTestContentAsync(expander).ConfigureAwait(true);

        // Act
        var peer = FrameworkElementAutomationPeer.CreatePeerForElement(expander);
        var provider = peer.GetPattern(PatternInterface.ExpandCollapse) as IExpandCollapseProvider;

        // Assert
        _ = provider.Should().NotBeNull();
        _ = provider!.ExpandCollapseState.Should().Be(ExpandCollapseState.Collapsed);

        expander.IsExpanded = true;
        _ = provider.ExpandCollapseState.Should().Be(ExpandCollapseState.Expanded);
    });

    [TestMethod]
    public Task AutomationPeer_RequestsChangeThroughEvents_Async() => EnqueueAsync(
    async () =>
    {
        // Setup
        var expander = new Controls.Expander();
        await LoadTestContentAsync(expander).ConfigureAwait(true);
        var expandRequests = 0;
        var collapseRequests = 0;
        expander.Expand += (_, _) => expandRequests++;
        expander.Collapse += (_, _) => collapseRequests++;
        var provider = (IExpandCollapseProvider)FrameworkElementAutomationPeer
            .CreatePeerForElement(expander)
            .GetPattern(PatternInterface.ExpandCollapse);

        // Act
        provider.Collapse(); // already collapsed: no request
        provider.Expand();
        expander.IsExpanded = true;
        provider.Expand(); // already expanded: no request
        provider.Collapse();

        // Assert
        _ = expandRequests.Should().Be(1);
        _ = collapseRequests.Should().Be(1);
    });
}
