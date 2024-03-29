// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics;
using DroidNet.Docking.Utils;

public abstract class LayoutEngine
{
    private readonly Stack<Flow> flows = new();

    protected Flow CurrentFlow => this.flows.Peek();

    public virtual object Build(IDockGroup root)
    {
        // Always clear the layout state stack before a new Build to get rid of the previous state.
        this.flows.Clear();

        var rootFlow = this.StartLayout(root);
        this.PushFlow(rootFlow);
        this.Layout(root);
        Debug.Assert(this.flows.Count == 1, "some pushes were not matched by pops");
        Debug.WriteLine($"=== Final state: {this.CurrentFlow}");
        this.EndLayout();
        return rootFlow;
    }

    protected abstract Flow StartLayout(IDockGroup root);

    protected abstract void PlaceDock(IDock dock);

    protected abstract void PlaceTray(IDockTray tray);

    protected abstract Flow StartFlow(IDockGroup group);

    protected abstract void EndFlow();

    protected abstract void EndLayout();

    private static bool ShouldShowGroup(IDockGroup group)
    {
        if (CheckShowGroupRecursive(group))
        {
            return true;
        }

        Debug.WriteLine($"Skipping {group}");
        return false;
    }

    private static bool CheckShowGroupRecursive(IDockGroup? group) => group switch
    {
        null => false,
        IDockTray tray => !tray.IsEmpty,
        _ => group.IsEmpty
            ? CheckShowGroupRecursive(group.First) || CheckShowGroupRecursive(group.Second)
            : group.Docks.Any(d => d.State != DockingState.Minimized),
    };

    private void PushFlow(Flow state)
    {
        Debug.WriteLine($"==> {state}");
        this.flows.Push(state);
    }

    private void PopFlow()
    {
        _ = this.flows.Pop();
        Debug.WriteLine($"<== {(this.flows.Count != 0 ? this.flows.Peek() : string.Empty)}");
    }

    private void Layout(IDockGroup group)
    {
        if (!ShouldShowGroup(group))
        {
            return;
        }

        Debug.Assert(group != null, "if group should be shown then it must be != null");

        Debug.WriteLine($"Layout started for Group {group}");

        var restoreState = this.ReorientFlowIfNeeded(group);

        if (!group.IsEmpty)
        {
            // A group with docks -> Layout the docks as items in the vector grid.
            foreach (var dock in group.Docks.Where(d => d.State != DockingState.Minimized))
            {
                this.PlaceDock(dock);
            }
        }
        else
        {
            this.HandlePart(group.First);
            this.HandlePart(group.Second);
        }

        if (restoreState)
        {
            this.EndFlow();
            this.PopFlow();
        }

        Debug.WriteLine($"Layout ended for Group {group}");
    }

    private void HandlePart(IDockGroup? part)
    {
        switch (part)
        {
            case null:
                return;

            case IDockTray { IsEmpty: false } tray:
                Debug.Assert(
                    (tray.IsVertical && this.CurrentFlow.IsHorizontal) ||
                    (!tray.IsVertical && this.CurrentFlow.IsVertical),
                    $"expecting tray orientation {part.Orientation} to be orthogonal to flow direction {this.CurrentFlow.Direction}");

                // Place the tray if it's not empty.
                this.PlaceTray(tray);
                break;

            default:
                this.Layout(part);
                break;
        }
    }

    private bool ReorientFlowIfNeeded(IDockGroup group)
    {
        // For the sake of layout, if a group has Docks and only one of them is
        // pinned, we consider the group's orientation as Undetermined. That
        // way, we don't create a new grid for that group.
        var groupOrientation = group.Orientation;
        if (!group.IsEmpty && group.Docks.Where(d => d.State == DockingState.Pinned).Take(2).Count() == 1)
        {
            Debug.WriteLine("Group has only one pinned dock, considering it as DockGroupOrientation.Undetermined");
            groupOrientation = DockGroupOrientation.Undetermined;
        }

        var state = this.CurrentFlow;
        if (groupOrientation == DockGroupOrientation.Undetermined ||
            state.Direction == group.Orientation.ToFlowDirection())
        {
            return false;
        }

        this.PushFlow(this.StartFlow(group));
        return true;
    }

    protected abstract class Flow(IDockGroup group)
    {
        public string Description { get; init; } = string.Empty;

        public FlowDirection Direction { get; } = group.Orientation.ToFlowDirection();

        public bool IsHorizontal => this.Direction == FlowDirection.LeftToRight;

        public bool IsVertical => this.Direction == FlowDirection.TopToBottom;

        public override string ToString() => this.Description;
    }
}
