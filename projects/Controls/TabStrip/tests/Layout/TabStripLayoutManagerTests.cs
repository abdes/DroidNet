// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

namespace DroidNet.Controls.Tabs.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("TabStripTests")]
[TestCategory("Layout")]
public class TabStripLayoutManagerTests
{
    // TODO: Add comprehensive unit tests for all policies, edge cases, and invariants as specified in REFACTOR_LAYOUT.md
    [TestMethod]
    public void AutoPolicy_2Pinned2Regular_Width300_NoScroll()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Auto,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, true, 50, 100),
            new(1, true, 50, 100),
            new(2, false, 50, 100),
            new(3, false, 50, 100),
        };
        var request = new LayoutRequest(300, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(4);
        result.Items.Should().Contain(item => item.Index == 0 && item.Width == 100 && item.IsPinned);
        result.Items.Should().Contain(item => item.Index == 1 && item.Width == 100 && item.IsPinned);
        result.Items.Should().Contain(item => item.Index == 2 && item.Width == 100 && !item.IsPinned);
        result.Items.Should().Contain(item => item.Index == 3 && item.Width == 100 && !item.IsPinned);
        result.SumPinnedWidth.Should().Be(200);
        result.SumRegularWidth.Should().Be(200);
        result.NeedsScrolling.Should().BeFalse();
    }

    [TestMethod]
    public void AutoPolicy_3Regular_Width200_Scroll()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Auto,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 100),
            new(1, false, 50, 100),
            new(2, false, 50, 100),
        };
        var request = new LayoutRequest(200, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(3);
        result.Items.Should().AllSatisfy(item => item.Width.Should().Be(100));
        result.SumPinnedWidth.Should().Be(0);
        result.SumRegularWidth.Should().Be(300);
        result.NeedsScrolling.Should().BeTrue();
    }

    [TestMethod]
    public void AutoPolicy_1Item_MaxWidth120()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 120,
            Policy = TabWidthPolicy.Auto,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 150),
        };
        var request = new LayoutRequest(200, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(1);
        result.Items[0].Width.Should().Be(120);
    }

    [TestMethod]
    public void AutoPolicy_NoItems_NoScroll()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Auto,
        };
        var items = new List<LayoutPerItemInput>();
        var request = new LayoutRequest(300, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().BeEmpty();
        result.SumPinnedWidth.Should().Be(0);
        result.SumRegularWidth.Should().Be(0);
        result.NeedsScrolling.Should().BeFalse();
    }

    [TestMethod]
    public void AutoPolicy_3Pinned_Width300_NoScroll()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Auto,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, true, 50, 100),
            new(1, true, 50, 100),
            new(2, true, 50, 100),
        };
        var request = new LayoutRequest(300, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(3);
        result.Items.Should().AllSatisfy(item => item.Width.Should().Be(100));
        result.SumPinnedWidth.Should().Be(300);
        result.SumRegularWidth.Should().Be(0);
        result.NeedsScrolling.Should().BeFalse();
    }

    [TestMethod]
    public void EqualPolicy_2Items_Preferred120_Width300_NoScroll()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            PreferredItemWidth = 120,
            Policy = TabWidthPolicy.Equal,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 100),
            new(1, false, 50, 100),
        };
        var request = new LayoutRequest(300, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(2);
        result.Items.Should().AllSatisfy(item => item.Width.Should().Be(120));
        result.SumPinnedWidth.Should().Be(0);
        result.SumRegularWidth.Should().Be(240);
        result.NeedsScrolling.Should().BeFalse();
    }

    [TestMethod]
    public void EqualPolicy_3Regular_Preferred100_Width200_Scroll()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            PreferredItemWidth = 100,
            Policy = TabWidthPolicy.Equal,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 80),
            new(1, false, 50, 80),
            new(2, false, 50, 80),
        };
        var request = new LayoutRequest(200, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(3);
        result.Items.Should().AllSatisfy(item => item.Width.Should().Be(100));
        result.SumPinnedWidth.Should().Be(0);
        result.SumRegularWidth.Should().Be(300);
        result.NeedsScrolling.Should().BeTrue();
    }

    [TestMethod]
    public void EqualPolicy_1Item_Preferred150_Max120()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 120,
            PreferredItemWidth = 150,
            Policy = TabWidthPolicy.Equal,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 80),
        };
        var request = new LayoutRequest(200, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(1);
        result.Items[0].Width.Should().Be(120);
    }

    [TestMethod]
    public void EqualPolicy_Preferred150_Max120_Clamped()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 120,
            PreferredItemWidth = 150,
            Policy = TabWidthPolicy.Equal,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 80),
        };
        var request = new LayoutRequest(200, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items[0].Width.Should().Be(120);
    }

    [TestMethod]
    public void CompactPolicy_2Regular_Width300_NoShrink_NoScroll()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Compact,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 100),
            new(1, false, 50, 100),
        };
        var request = new LayoutRequest(300, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(2);
        result.Items.Should().AllSatisfy(item => item.Width.Should().Be(100));
        result.NeedsScrolling.Should().BeFalse();
    }

    [TestMethod]
    public void CompactPolicy_3Regular_Width200_ShrinkProportional()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Compact,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 100),
            new(1, false, 50, 100),
            new(2, false, 50, 100),
        };
        var request = new LayoutRequest(200, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(3);

        // Assuming equal shrinking: 300 - 200 = 100 deficit, 100 / 3 ≈ 33.33, so 100 - 33.33 ≈ 66.67
        result.Items.Should().AllSatisfy(item => item.Width.Should().BeApproximately(66.67, 0.01));
        result.NeedsScrolling.Should().BeFalse();
    }

    [TestMethod]
    public void CompactPolicy_2Regular_Min80_Width100_Scroll()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Compact,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 80, 100),
            new(1, false, 80, 100),
        };
        var request = new LayoutRequest(100, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(2);
        result.Items.Should().AllSatisfy(item => item.Width.Should().Be(80));
        result.NeedsScrolling.Should().BeTrue();
    }

    [TestMethod]
    public void CompactPolicy_1Pinned2Regular_Width150_Pinned100_RegularShrink()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Compact,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, true, 50, 100),
            new(1, false, 50, 100),
            new(2, false, 50, 100),
        };
        var request = new LayoutRequest(150, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(3);
        result.Items.Should().Contain(item => item.Index == 0 && item.Width == 100 && item.IsPinned);

        // Regular: sum desired 200, available 150, deficit 50, shrink per item 50/2 = 25, so 100 - 25 = 75
        result.Items.Should().Contain(item => item.Index == 1 && item.Width == 75 && !item.IsPinned);
        result.Items.Should().Contain(item => item.Index == 2 && item.Width == 75 && !item.IsPinned);
        result.NeedsScrolling.Should().BeFalse();
    }

    [TestMethod]
    public void CompactPolicy_3Regular_VaryingMin_Width180_IterativeShrink()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Compact,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 100), // desired 100, min 50
            new(1, false, 80, 100), // desired 100, min 80
            new(2, false, 50, 100), // desired 100, min 50
        };
        var request = new LayoutRequest(180, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        // First iteration: sum 300 > 180, deficit 120, shrink per 40, new widths: 60, 60, 60
        // But item 1 min 80, so clamped to 80, remaining items 0 and 2
        // Second iteration: remaining sum 120, available 180, no deficit, so stop
        // Wait, let's calculate properly.
        // Initial desired: 100,100,100 sum 300 > 180, deficit 120, shrink 40 each
        // Proposed: 60,60,60 but item1 min80, so 60->80, remaining 0 and 2 with sum 120
        // Deficit now 300 - 180 - (100-80 wait, no: after first shrink, the sum is 60+80+60=200 >180, deficit 20, remaining 2 items (0 and 2), shrink 10 each, 60-10=50, 60-10=50
        // So final: 50,80,50
        result.Items.Should().Contain(item => item.Index == 0 && item.Width == 50);
        result.Items.Should().Contain(item => item.Index == 1 && item.Width == 80);
        result.Items.Should().Contain(item => item.Index == 2 && item.Width == 50);
        result.NeedsScrolling.Should().BeFalse();
    }

    [TestMethod]
    public void AnyPolicy_1Item_Min150_Max120_EffectiveMin120()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 120,
            Policy = TabWidthPolicy.Auto,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 150, 200),
        };
        var request = new LayoutRequest(200, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items[0].Width.Should().Be(120);
    }

    [TestMethod]
    public void AutoPolicy_1Item_Desired30_Min50_Width50()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Auto,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 30),
        };
        var request = new LayoutRequest(200, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items[0].Width.Should().Be(50);
    }

    [TestMethod]
    public void EqualPolicy_1Item_Preferred30_Min50_Width50()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            PreferredItemWidth = 30,
            Policy = TabWidthPolicy.Equal,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 80),
        };
        var request = new LayoutRequest(200, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items[0].Width.Should().Be(50);
    }

    [TestMethod]
    public void EqualPolicy_1Pinned1Regular_Preferred120_Width300_NoScroll()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            PreferredItemWidth = 120,
            Policy = TabWidthPolicy.Equal,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, true, 50, 100),  // pinned with desired 100
            new(1, false, 50, 100), // regular with desired 100
        };
        var request = new LayoutRequest(300, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(2);

        // Both should be set to PreferredItemWidth = 120, regardless of pinned status
        result.Items.Should().Contain(item => item.Index == 0 && item.Width == 120 && item.IsPinned);
        result.Items.Should().Contain(item => item.Index == 1 && item.Width == 120 && !item.IsPinned);
        result.SumPinnedWidth.Should().Be(120);
        result.SumRegularWidth.Should().Be(120);
        result.NeedsScrolling.Should().BeFalse();
    }

    [TestMethod]
    public void EqualPolicy_1PinnedHighDesired_Preferred120_Width300_NoScroll()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            PreferredItemWidth = 120,
            Policy = TabWidthPolicy.Equal,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, true, 50, 200),  // pinned with high desired 200, but should be clamped to 120 in Equal mode
            new(1, false, 50, 100), // regular
        };
        var request = new LayoutRequest(300, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(2);

        // Both should be set to PreferredItemWidth = 120, even if pinned has higher desired
        result.Items.Should().Contain(item => item.Index == 0 && item.Width == 120 && item.IsPinned);
        result.Items.Should().Contain(item => item.Index == 1 && item.Width == 120 && !item.IsPinned);
        result.SumPinnedWidth.Should().Be(120);
        result.SumRegularWidth.Should().Be(120);
        result.NeedsScrolling.Should().BeFalse();
    }

    [TestMethod]
    public void FractionalWidths_NoRedistribution_Acceptable()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Compact,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 10, 33.3),
            new(1, false, 10, 33.3),
            new(2, false, 10, 33.3),
        };
        var request = new LayoutRequest(80, items); // sum desired 99.9, available 80, deficit 19.9, shrink ~6.63 each, widths ~26.67

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(3);
        result.Items.Should().AllSatisfy(item => item.Width.Should().BeApproximately(26.67, 0.01));

        // No exact check for sum, as fractional is acceptable
    }

    [TestMethod]
    public void Performance_1000Items_Width5000_Correct()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 20,
            Policy = TabWidthPolicy.Auto,
        };
        var items = new List<LayoutPerItemInput>();
        for (var i = 0; i < 1000; i++)
        {
            items.Add(new LayoutPerItemInput(i, false, 5, 10));
        }

        var request = new LayoutRequest(5000, items);

        // Act
        var result = manager.ComputeLayout(request);

        // Assert
        result.Items.Should().HaveCount(1000);
        result.Items.Should().AllSatisfy(item => item.Width.Should().Be(10));
        result.NeedsScrolling.Should().BeTrue();
    }

    [TestMethod]
    public void PolicyChange_AffectsResults()
    {
        // Arrange
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 100),
            new(1, false, 50, 100),
        };
        var request = new LayoutRequest(150, items);

        var managerAuto = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Auto,
        };
        var managerEqual = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            PreferredItemWidth = 80,
            Policy = TabWidthPolicy.Equal,
        };

        // Act
        var resultAuto = managerAuto.ComputeLayout(request);
        var resultEqual = managerEqual.ComputeLayout(request);

        // Assert
        // Auto: widths 100,100, sum 200 > 150, scrolling true
        resultAuto.NeedsScrolling.Should().BeTrue();

        // Equal: widths 80,80, sum 160 > 150, scrolling true, but different widths
        resultEqual.Items[0].Width.Should().Be(80);
        resultEqual.NeedsScrolling.Should().BeTrue();
    }

    [TestMethod]
    public void IdenticalInputs_ConsistentResults()
    {
        // Arrange
        var manager = new TabStripLayoutManager
        {
            MaxItemWidth = 200,
            Policy = TabWidthPolicy.Auto,
        };
        var items = new List<LayoutPerItemInput>
        {
            new(0, false, 50, 100),
        };
        var request = new LayoutRequest(150, items);

        // Act
        var result1 = manager.ComputeLayout(request);
        var result2 = manager.ComputeLayout(request);

        // Assert
        result1.Should().BeEquivalentTo(result2);
    }
}
