// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using System.Diagnostics;
using DroidNet.Docking;

/// <summary>Docking tree optimization.</summary>
public partial class Docker
{
    private int isConsolidating;

    public void ConsolidateUp(LayoutSegment? startingSegment)
    {
        if (startingSegment is null)
        {
            return;
        }

        Debug.WriteLine($"Consolidating the docking tree up, starting from {startingSegment}");

        // If a consolidation is ongoing, some manipulations of the docking tree may trigger consolidation again. This
        // method should not be re-entrant. Instead, we just ignore the new request and continue the ongoing
        // consolidation.
        if (Interlocked.Exchange(ref this.isConsolidating, 1) == 1)
        {
            Debug.WriteLine("Already consolidating...");
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

        if (node.Value is DockGroup dockGroup && dockGroup.Docks.Count == 0 && node.Parent is not null)
        {
            return new RemovenodeFromParent(node, node.Parent).Execute();
        }

        // Potential for collapsing into parent or merger with sibling
        return node.Sibling is null or DockingTreeNode { IsLeaf: true } ? node.Parent : null;
    }

    private static DockingTreeNode? SimplifyChildren(DockingTreeNode node)
    {
        // Single child parent
        if (node.Left is null || node.Right is null)
        {
            var child = node.Left ?? node.Right;
            Debug.Assert(child is not null, "leaf case should be handled before");

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
        else
        {
            // Parent with two leaf children
            if (node is { Left: DockingTreeNode { IsLeaf: true }, Right: DockingTreeNode { IsLeaf: true } })
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

        return null;
    }

    private DockingTreeNode? OptimizedEmptyEdge(DockingTreeNode node)
    {
        if ((node.Value is EdgeGroup && node.Left is null && node.Right?.Value is TrayGroup) ||
            (node.Right is null && node.Left?.Value is TrayGroup))
        {
            // Clear the edge node from the edges table.
            var edgeEntry = this.edges.First(entry => entry.Value == node.Value);
            this.edges[edgeEntry.Key] = null;

            // Remove the edge node from the docking tree.
            Debug.Assert(node.Parent is not null, $"unexpected edge node {node} without a parent");
            return new RemovenodeFromParent(node, node.Parent).Execute();
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

    private sealed class RemovenodeFromParent(DockingTreeNode node, DockingTreeNode parent) : DockingTreeOptimization
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
