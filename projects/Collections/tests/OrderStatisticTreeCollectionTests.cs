// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace DroidNet.Collections.Tests;

[TestClass]
public sealed class OrderStatisticTreeCollectionTests
{
    [TestMethod]
    public void Add_Count_Contains()
    {
        var tree = new OrderStatisticTreeCollection<int>();
        _ = tree.Should().BeEmpty();
        tree.Add(3);
        tree.Add(1);
        tree.Add(2);
        _ = tree.Should().HaveCount(3).And.Contain(1);
        _ = tree.Should().NotContain(4);
    }

    [TestMethod]
    public void RankAndSelect_Basic()
    {
        var tree = new OrderStatisticTreeCollection<int>();
        for (var i = 0; i < 10; i++)
        {
            tree.Add(i * 2);
        }

        _ = tree.Rank(-1).Should().Be(0);
        _ = tree.Rank(0).Should().Be(0);
        _ = tree.Rank(3).Should().Be(2);
        _ = tree.Should().HaveCount(10);
        _ = tree.Select(0).Should().Be(0);
        _ = tree.Select(5).Should().Be(2 * 5);
    }

    [TestMethod]
    public void Remove_And_Order()
    {
        var tree = new OrderStatisticTreeCollection<int>();
        var values = new[] { 5, 1, 8, 3, 6 };
        foreach (var v in values)
        {
            tree.Add(v);
        }

        _ = tree.Should().HaveCount(5);
        _ = tree.Remove(3).Should().BeTrue();
        _ = tree.Should().NotContain(3);
        _ = tree.Should().HaveCount(4);
        var ordered = tree.ToList();
        _ = ordered.Should().Equal([1, 5, 6, 8]);
    }

    [TestMethod]
    public void Select_OutOfRange_Throws()
    {
        var tree = new OrderStatisticTreeCollection<int>
        {
            1,
            2,
        };
        Action act1 = () => tree.Select(-1);
        _ = act1.Should().Throw<ArgumentOutOfRangeException>();
        Action act2 = () => tree.Select(tree.Count);
        _ = act2.Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public void Duplicates_Add_Remove_Behavior()
    {
        var tree = new OrderStatisticTreeCollection<int>
        {
            5,
            5,
            5,
        };
        _ = tree.Should().HaveCount(3);

        // Rank should count elements strictly less than the given value
        _ = tree.Rank(5).Should().Be(0);

        // In-order traversal should contain three 5s
        var list = tree.ToList();
        _ = list.Should().HaveCount(3).And.OnlyContain(n => n == 5);

        // Remove one instance at a time
        _ = tree.Remove(5).Should().BeTrue();
        _ = tree.Should().HaveCount(2);
        list = [.. tree];
        _ = list.Should().HaveCount(2).And.OnlyContain(n => n == 5);

        _ = tree.Remove(5).Should().BeTrue();
        _ = tree.Remove(5).Should().BeTrue();
        _ = tree.Remove(5).Should().BeFalse();
        _ = tree.Should().BeEmpty();
    }

    [TestMethod]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA5394:Do not use insecure randomness", Justification = "Deterministic non-security test uses seeded Random for reproducibility.")]
    public void InOrder_Is_Sorted_For_Random_Inserts()
    {
        var tree = new OrderStatisticTreeCollection<int>();
        var rnd = new Random(42);
        var items = new List<int>();
        for (var i = 0; i < 100; i++)
        {
            var v = rnd.Next(0, 1000);
            items.Add(v);
            tree.Add(v);
        }

        var expected = items.Order().ToList();
        var actual = tree.ToList();
        _ = actual.Should().Equal(expected);
    }

    [TestMethod]
    public void Rank_Select_Invariant()
    {
        var tree = new OrderStatisticTreeCollection<int>();
        for (var i = 0; i < 20; i++)
        {
            tree.Add(i);
        }

        for (var i = 0; i < tree.Count; i++)
        {
            var val = tree.Select(i);
            var r = tree.Rank(val);

            // Rank(val) should be the index of the first occurrence of val, which must be <= i
            _ = r.Should().BeLessThanOrEqualTo(i);

            // And selecting at rank should return the same value
            _ = tree.Select(r).Should().Be(val);
        }
    }

    [TestMethod]
    public void CustomComparer_Produces_Different_Order()
    {
        var descComparer = Comparer<int>.Create((a, b) => b.CompareTo(a));
        var tree = new OrderStatisticTreeCollection<int>(descComparer);
        var values = new[] { 1, 3, 2 };
        foreach (var v in values)
        {
            tree.Add(v);
        }

        var list = tree.ToList();
        _ = list.Should().Equal([3, 2, 1]);
    }
}
