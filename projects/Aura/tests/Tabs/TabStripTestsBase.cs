// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using AwesomeAssertions;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Controls.Tests;

public class TabStripTestsBase : VisualUserInterfaceTests
{
    protected const double PreferredItemWidth = 100.0;
    protected const double TabStripHeight = 40.0;

    /// <summary>
    ///     Waits for the composition/rendering pipeline to complete.
    /// </summary>
    protected static async Task WaitForRenderCompletion()
        => _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { })
            .ConfigureAwait(true);

    /// <summary>
    ///     Verifies that a template part with the given name exists and is of the expected type.
    /// </summary>
    /// <param name="tabStrip">The TabStrip instance to check for the template part.</param>
    /// <param name="partName">The name of the template part to verify.</param>
    /// <typeparam name="T">The expected type of the template part, must be a FrameworkElement.</typeparam>
    protected static void CheckPartIsThere<T>(TabStrip tabStrip, string partName)
        where T : FrameworkElement
    {
        var part = tabStrip.FindDescendant<T>(e => string.Equals(e.Name, partName, StringComparison.Ordinal));
        _ = part.Should().NotBeNull($"Template part '{partName}' of type {typeof(T).Name} should be present");
    }

    /// <summary>
    ///     Creates a TabItem with the specified properties for testing.
    /// </summary>
    /// <param name="header">The header text for the TabItem.</param>
    /// <param name="isPinned">Whether the TabItem is pinned.</param>
    /// <param name="isSelected">Whether the TabItem is selected.</param>
    /// <param name="isClosable">Whether the TabItem is closable.</param>
    protected static TabItem CreateTabItem(string header, bool isPinned = false, bool isSelected = false, bool isClosable = true)
        => new()
        {
            Header = header,
            IsPinned = isPinned,
            IsSelected = isSelected,
            IsClosable = isClosable,
        };

    /// <summary>
    ///     Creates a TabStrip with the specified number of tabs (not loaded into UI). Use
    ///     LoadTestContentAsync from the test class to load it.
    /// </summary>
    /// <param name="count">Total number of tabs to create.</param>
    /// <param name="pinnedCount">Number of tabs that should be pinned (must be &lt;=
    /// count).</param>
    /// <returns>A TabStrip instance with items added.</returns>
    protected async Task<TestableTabStrip> CreateAndLoadTabStripAsync(int count, int pinnedCount = 0)
    {
        var tabStrip = this.CreateTabStrip(count, pinnedCount);

        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        return tabStrip;
    }

    protected TestableTabStrip CreateTabStrip(int count, int pinnedCount = 0)
    {
        var tabStrip = new TestableTabStrip()
        {
            TabWidthPolicy = TabWidthPolicy.Equal,
            PreferredItemWidth = PreferredItemWidth,
            Height = TabStripHeight,
            MinHeight = TabStripHeight,
            MaxHeight = TabStripHeight,
            LoggerFactory = this.LoggerFactory,
        };

        // Add items
        for (var i = 0; i < count; i++)
        {
            var item = new TabItem
            {
                Header = string.Create(CultureInfo.InvariantCulture, $"Tab {i + 1}"),
                IsPinned = i < pinnedCount,
            };
            tabStrip.Items.Add(item);
        }

        return tabStrip;
    }
}
