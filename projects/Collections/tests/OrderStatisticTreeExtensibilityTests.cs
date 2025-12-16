// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;

namespace DroidNet.Collections.Tests;

[TestClass]
public sealed class OrderStatisticTreeExtensibilityTests
{
    [TestMethod]
    public void Extensibility_SummationTree_maintains_sums_on_add()
    {
        var tree = new SummationTree
        {
            // Add 10: Sum=10
            10,
        };
        _ = tree.GetTotalSum().Should().Be(10);

        // Add 5: Sum=15
        tree.Add(5);
        _ = tree.GetTotalSum().Should().Be(15);

        // Structure likely:
        //   10 (Sum 15)
        //  /
        // 5 (Sum 5)

        // Add 20: Sum=35
        tree.Add(20);
        _ = tree.GetTotalSum().Should().Be(35);
    }

    [TestMethod]
    public void Extensibility_SummationTree_maintains_sums_after_rotations()
    {
        var tree = new SummationTree();

        // Inserting efficiently sorted data usually triggers rotations in Red-Black trees
        var values = Enumerable.Range(1, 100).ToList();

        // Shuffle to cause random rotations, or strict order to force specific balancing
        // Let's just add them. 1..100 sum is n*(n+1)/2 = 5050
        foreach (var v in values)
        {
            tree.Add(v);
        }

        _ = tree.GetTotalSum().Should().Be(5050);
        _ = tree.Count.Should().Be(100);
    }

    [TestMethod]
    public void Extensibility_SummationTree_maintains_sums_on_remove()
    {
        var tree = new SummationTree
        {
            10,
            20,
            5, // Total 35
        };

        _ = tree.Remove(20);
        _ = tree.GetTotalSum().Should().Be(15);
        _ = tree.Count.Should().Be(2);

        _ = tree.Remove(10);
        _ = tree.GetTotalSum().Should().Be(5);
        _ = tree.Count.Should().Be(1);

        _ = tree.Remove(5);
        _ = tree.GetTotalSum().Should().Be(0);
        _ = tree.Count.Should().Be(0);
    }

    [TestMethod]
    [Timeout(2000, CooperativeCancellation = true)]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA5394:Do not use insecure randomness", Justification = "Deterministic non-security test uses seeded Random for reproducibility.")]
    public void Extensibility_SummationTree_random_mutation_stress_test()
    {
        var tree = new SummationTree();
        var rng = new Random(12345);
        var expectedSum = 0L;
        var items = new List<int>();

        for (var i = 0; i < 50; i++)
        {
            // Random operations: 70% add, 30% remove
            if (items.Count > 0 && rng.NextDouble() < 0.3)
            {
                // Remove random item
                var index = rng.Next(items.Count);
                var val = items[index];

                // We must remove one instance from our list and from tree
                // List removal is minimal effort for tracking expected sum
                items.RemoveAt(index);
                expectedSum -= val;

                _ = tree.Remove(val);
            }
            else
            {
                // Add
                var val = rng.Next(1000);
                items.Add(val);
                expectedSum += val;
                tree.Add(val);
            }

            _ = tree.GetTotalSum().Should().Be(expectedSum, string.Create(CultureInfo.InvariantCulture, $"after iteration {i}"));
        }
    }

    /// <summary>
    /// A custom tree that tracks the sum of values in the subtree.
    /// This proves that we can augment the tree with custom aggregation logic that is maintained during updates.
    /// </summary>
    private sealed class SummationTree : OrderStatisticTreeCollection<int>
    {
        public long GetTotalSum() => (this.Root as SumNode)?.SubtreeSum ?? 0;

        // Helper to inspect internal node state.
        // We use the base Select to find the value, but we can't easily get the Node object from public API.
        // For testing, just relying on root total sum is a strong indicator, but let's see if we can expose more if needed.
        // Actually, we can just walk the tree if we exposed root, but root is protected.
        // Since we are inside the derived class, we can access 'root'.
        public long GetSumAtRank(int rank) =>
            (this.SelectNode(rank) as SumNode)?.SubtreeSum ?? -1;

        protected override Node CreateNode(int value) => new SumNode(value);

        protected override void OnNodeUpdated(Node node)
        {
            base.OnNodeUpdated(node);

            if (node is SumNode sumNode)
            {
                var leftSum = (sumNode.Left as SumNode)?.SubtreeSum ?? 0;
                var rightSum = (sumNode.Right as SumNode)?.SubtreeSum ?? 0;
                sumNode.SubtreeSum = sumNode.Value + leftSum + rightSum;
            }
        }

        private Node? SelectNode(int index)
        {
            if (index < 0 || index >= this.Count)
            {
                return null;
            }

            var x = this.Root;
            while (true)
            {
                var leftSize = x!.Left?.SubtreeSize ?? 0;
                if (index < leftSize)
                {
                    x = x.Left;
                }
                else
                {
                    if (index == leftSize)
                    {
                        return x;
                    }

                    index -= leftSize + 1;
                    x = x.Right;
                }
            }
        }

        private sealed class SumNode(int value) : Node(value)
        {
            public long SubtreeSum { get; set; } = value;
        }
    }
}
