// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace DroidNet.Docking.Workspace;

/// <summary>Docking tree optimization.</summary>
public partial class Docker
{
    private int isConsolidating;

    /// <summary>
    /// Consolidates the docking tree upwards starting from the specified segment.
    /// </summary>
    /// <param name="startingSegment">The layout segment from which to start the consolidation.</param>
    /// <remarks>
    /// This method ensures that the docking tree is optimized by consolidating nodes upwards. If a consolidation is already in progress,
    /// the method will not start a new consolidation. Instead, it will continue the ongoing consolidation.
    /// </remarks>
    public void ConsolidateUp(LayoutSegment? startingSegment)
    {
        if (startingSegment is null)
        {
            return;
        }

        // $"Consolidating the docking tree up, starting from {startingSegment}"

        // If a consolidation is ongoing, some manipulations of the docking tree may trigger consolidation again. This
        // method should not be re-entrant. Instead, we just ignore the new request and continue the ongoing
        // consolidation.
        if (Interlocked.Exchange(ref this.isConsolidating, 1) == 1)
        {
            // "Already consolidating..."
            return;
        }

        try
        {
            var startingNode = this.FindNode(startingSegment);
            Debug.Assert(
                startingNode != null,
                $"request to start consolidation with a segment that is not in a node: {startingSegment}");
            var result = this.ConsolidateIfNeeded(startingNode);
            while (result is not null)
            {
                result = this.ConsolidateIfNeeded(result);
            }
        }
        finally
        {
            _ = Interlocked.Exchange(ref this.isConsolidating, 0);
        }
    }

    private static DockingTreeNode? CheckCollapsibleLeafNode(DockingTreeNode node)
    {
        if (!node.IsLeaf)
        {
            return null;
        }

        if (node is { Value: DockGroup { Docks.Count: 0 }, Parent: not null })
        {
            return new RemoveNodeFromParent(node, node.Parent).Execute();
        }

        // Potential for collapsing into parent or merger with sibling
        return node.Sibling is null or { IsLeaf: true } ? node.Parent : null;
    }

    private static DockingTreeNode? SimplifyChildren(DockingTreeNode node)
    {
        if (node.Left is null && node.Right is null)
        {
            return node.Parent;
        }

        // Single child parent
        if (node.Left is not null && node.Right is not null)
        {
            // Parent with two leaf children
            if (node is { Left.IsLeaf: true, Right.IsLeaf: true })
            {
                // and compatible orientations => merge the children
                if ((node.Left.Value.Orientation is DockGroupOrientation.Undetermined ||
                     node.Left.Value.Orientation == node.Value.Orientation) &&
                    (node.Right.Value.Orientation == DockGroupOrientation.Undetermined ||
                     node.Right.Value.Orientation == node.Value.Orientation))
                {
                    return new MergeLeafParts(node).Execute();
                }
            }
        }
        else
        {
            var child = node.Left ?? node.Right;
            Debug.Assert(child is not null, "at least left or right should not be null");

            // with compatible orientation => assimilate child into parent
            if (child.Value.Orientation == DockGroupOrientation.Undetermined ||
                node.Value.Orientation == DockGroupOrientation.Undetermined ||
                child.Value.Orientation == node.Value.Orientation)
            {
                // Assimilate child, unless it is the center group node.
                return child.Value is CenterGroup
                    ? null
                    : new AssimilateChild(child: child, parent: node).Execute();
            }
        }

        return null;
    }

    private DockingTreeNode? OptimizedEmptyEdge(DockingTreeNode node)
    {
        if (node is { Value: EdgeGroup, Left: null, Right.Value: TrayGroup } ||
            (node.Right is null && node.Left?.Value is TrayGroup))
        {
            // Clear the edge node from the edges table.
            var edgeEntry = this.edges.First(entry => entry.Value == node.Value);
            this.edges[edgeEntry.Key] = null;

            // Remove the edge node from the docking tree.
            Debug.Assert(node.Parent is not null, $"unexpected edge node {node} without a parent");
            return new RemoveNodeFromParent(node, node.Parent).Execute();
        }

        return null;
    }

    private DockingTreeNode? ConsolidateIfNeeded(DockingTreeNode node)
    {
        // Center group cannot be optimized.
        if (node.Value is CenterGroup)
        {
            return null;
        }

        // Chain call optimization until one of them returns the next node to be optimized.
        return this.OptimizedEmptyEdge(node)
               ?? CheckCollapsibleLeafNode(node)
               ?? SimplifyChildren(node);
    }

    private abstract class DockingTreeOptimization
    {
        protected abstract string Description { get; }

        public DockingTreeNode Execute()
        {
#if DEBUG
            Debug.WriteLine($"Optimize: {this.Description}");
            Debug.WriteLine("  <<<< BEFORE");
            this.DumpImpactedSubTree();
#endif
            var result = this.DoExecute();
#if DEBUG
            Debug.WriteLine("  >>>> AFTER");
            this.DumpImpactedSubTree();
#endif
            return result;
        }

        protected abstract DockingTreeNode DoExecute();

        protected abstract void DumpImpactedSubTree();
    }

    private sealed class RemoveNodeFromParent(DockingTreeNode node, DockingTreeNode parent) : DockingTreeOptimization
    {
        protected override string Description => "remove empty group from parent";

        protected override DockingTreeNode DoExecute()
        {
            parent.RemoveChild(node);
            return parent;
        }

        protected override void DumpImpactedSubTree() => parent.Dump(initialIndentLevel: 1);
    }

    private sealed class AssimilateChild(DockingTreeNode child, DockingTreeNode parent) : DockingTreeOptimization
    {
        protected override string Description => "assimilate child";

        protected override DockingTreeNode DoExecute()
        {
            parent.AssimilateChild(child);
            return parent;
        }

        protected override void DumpImpactedSubTree() => parent.Dump(initialIndentLevel: 1);
    }

    private sealed class MergeLeafParts(DockingTreeNode group) : DockingTreeOptimization
    {
        protected override string Description => "merge leaf parts";

        protected override DockingTreeNode DoExecute()
        {
            group.MergeLeafParts();
            return group;
        }

        protected override void DumpImpactedSubTree() => group.Dump(initialIndentLevel: 1);
    }
}
