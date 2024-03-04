// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics;
using DroidNet.Docking.Utils;

public abstract class WorkspaceLayoutBuilder(IDocker docker)
{
    private readonly Stack<LayoutState> states = new();

    protected LayoutState CurrentState => this.states.Peek();

    protected object Build()
    {
        var state = this.StartLayout(docker.Root);
        this.SaveState(state);
        this.Layout(docker.Root);
        Debug.Assert(this.states.Count == 1, $"some pushes were not matched by pops");
        Debug.WriteLine($"=== Final state: {this.CurrentState}");
        this.EndLayout();
        return state;
    }

    protected abstract LayoutState StartLayout(IDockGroup root);

    protected abstract void PlaceDock(IDock dock);

    protected abstract void PlaceTray(IDockTray tray);

    protected abstract LayoutState StartFlow(IDockGroup group);

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

    private void SaveState(LayoutState state)
    {
        Debug.WriteLine($"==> {state}");
        this.states.Push(state);
    }

    private void RestoreState()
    {
        _ = this.states.Pop();
        Debug.WriteLine($"<== {(this.states.Count != 0 ? this.states.Peek() : string.Empty)}");
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
            HandlePart(group.First);
            HandlePart(group.Second);
        }

        if (restoreState)
        {
            this.EndFlow();
            this.RestoreState();
        }

        Debug.WriteLine($"Layout ended for Group {group}");
        return;

        void HandlePart(IDockGroup? part)
        {
            switch (part)
            {
                case null:
                    return;

                case IDockTray { IsEmpty: false } tray:
                    Debug.Assert(
                        (tray.IsVertical &&
                         this.CurrentState.FlowDirection == FlowDirection.LeftToRight) ||
                        (!tray.IsVertical &&
                         this.CurrentState.FlowDirection == FlowDirection.TopToBottom),
                        $"expecting tray orientation {group.Orientation} to be orthogonal to flow direction {this.CurrentState.FlowDirection}");

                    // Place the tray if it's not empty.
                    this.PlaceTray(tray);
                    break;

                default:
                    this.Layout(part);
                    break;
            }
        }
    }

    private bool ReorientFlowIfNeeded(IDockGroup group)
    {
        // For the sake of layout, if a group has Docks and only one of them is
        // pinned, we consider the group's orientation as Undetermined. That
        // way, we don't create a new grid for that group.
        var groupOrientation = group.Orientation;
        if (!group.IsEmpty && group.Docks.Count(d => d.State == DockingState.Pinned) == 1)
        {
            Debug.WriteLine($"Group has only one pinned dock, considering it as DockGroupOrientation.Undetermined");
            groupOrientation = DockGroupOrientation.Undetermined;
        }

        var state = this.CurrentState;
        if (groupOrientation == DockGroupOrientation.Undetermined ||
            state.FlowDirection == group.Orientation.ToFlowDirection())
        {
            return false;
        }

        this.SaveState(this.StartFlow(group));
        return true;
    }

    protected abstract class LayoutState
    {
        public string Description { get; init; } = string.Empty;

        public abstract FlowDirection FlowDirection { get; }

        public override string ToString() => this.Description;
    }
}
