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
            var startingNode = this.FindNode((node) => node.Item == startingSegment);
            Debug.Assert(
                startingNode != null,
                $"request to start consolidation with a segment that is not in a node: {startingSegment}");
            var result = ConsolidateIfNeeded(startingNode);
            while (result is not null)
            {
                result = ConsolidateIfNeeded(result);
            }
        }
        finally
        {
            _ = Interlocked.Exchange(ref this.isConsolidating, 0);
        }
    }

    private static DockingTreeNode? ConsolidateIfNeeded(DockingTreeNode node)
    {
        // Center and edge groups cannot be optimized
        if (node.Item is CenterGroup or EdgeGroup)
        {
            return null;
        }

        if (node.IsLeaf)
        {
            if (node.Item is DockGroup dockGroup && dockGroup.Docks.Count == 0 && node.Parent is not null)
            {
                return new RemovenodeFromParent(node, node.Parent).Execute();
            }

            // Potential for collapsing into parent or merger with sibling
            return node.Sibling is null or DockingTreeNode { IsLeaf: true } ? node.Parent : null;
        }

        // Single child parent
        if (node.Left is null || node.Right is null)
        {
            var child = node.Left ?? node.Right;
            Debug.Assert(child is not null, "leaf case should be handled before");

            // with compatible orientation => assimilate child into parent
            if (child.Item.Orientation == DockGroupOrientation.Undetermined ||
                node.Item.Orientation == DockGroupOrientation.Undetermined ||
                child.Item.Orientation == node.Item.Orientation)
            {
                return new AssimilateChild(child: child, parent: node).Execute();
            }
        }
        else
        {
            // Parent with two leaf children
            if (node is { Left: DockingTreeNode { IsLeaf: true }, Right: DockingTreeNode { IsLeaf: true } })
            {
                // and compatible orientations => merge the children
                if ((node.Left.Item.Orientation is DockGroupOrientation.Undetermined ||
                     node.Left.Item.Orientation == node.Item.Orientation) &&
                    (node.Right.Item.Orientation == DockGroupOrientation.Undetermined ||
                     node.Right.Item.Orientation == node.Item.Orientation))
                {
                    return new MergeLeafParts(node).Execute();
                }
            }
        }

        return null;
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
