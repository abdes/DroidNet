// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Detail;
using DroidNet.Docking.Utils;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(VectorLayoutEngine))]
public class LayoutEngineTests : VerifyBase
{
    [TestMethod]
    public Task TestLayoutBuilder()
    {
        using var docker = new Docker();

        docker.DockToCenter(CenterDock.New());

        using var left1 = ToolDock.New();
        left1.AddDockable(Dockable.New("left1"));
        docker.DockToRoot(left1, AnchorPosition.Left);

        using var left2 = ToolDock.New();
        left2.AddDockable(Dockable.New("left2"));
        docker.Dock(left2, new AnchorBottom(left1.Dockables[0]));

        docker.DockToRoot(ToolDock.New(), AnchorPosition.Left, true);
        docker.DockToRoot(ToolDock.New(), AnchorPosition.Left);
        docker.DockToRoot(ToolDock.New(), AnchorPosition.Top);
        docker.DockToRoot(ToolDock.New(), AnchorPosition.Bottom, true);

        using var right1 = ToolDock.New();
        right1.AddDockable(Dockable.New("right1"));
        docker.DockToRoot(right1, AnchorPosition.Right);
        docker.Dock(ToolDock.New(), new Anchor(AnchorPosition.Right, right1.Dockables[0]));
        docker.Dock(ToolDock.New(), new Anchor(AnchorPosition.Bottom, right1.Dockables[0]));

        docker.Root.DumpGroup();

        var gridLayout = new VectorLayoutEngine();
        var result = gridLayout.Build(docker.Root);

        gridLayout.DumpLayout();

        return this.Verify(result).UseDirectory("Snapshots");
    }

    private sealed class VectorLayoutEngine : LayoutEngine
    {
        private new GridLayoutState CurrentState => (GridLayoutState)base.CurrentState;

        private Vector CurrentVector => this.CurrentState.CurrentFlow;

        public new Vector Build(IDockGroup root) => ((GridLayoutState)base.Build(root)).CurrentFlow;

        public void DumpLayout()
        {
            DumpLayoutItemRecursive(this.CurrentVector, 0);

            return;

            static void DumpLayoutItemRecursive(object? item, int indentLevel)
            {
                var indent = new string(' ', indentLevel * 2); // 2 spaces per indent level

                switch (item)
                {
                    case null:
                        return;

                    case IDockTray tray:
                        Debug.WriteLine($"{indent}{tray}");
                        break;

                    case IDock dock:
                        Debug.WriteLine($"{indent}Dock {dock}, {dock.Anchor}");
                        break;

                    case Vector vector:
                        Debug.WriteLine($"{indent}Vector flow: {vector.Direction} {{");
                        vector.Items.ForEach(child => DumpLayoutItemRecursive(child, indentLevel + 1));
                        Debug.WriteLine($"{indent}}}");
                        break;

                    default:
                        throw new ArgumentException($"unexpected item type {item.GetType()}", nameof(item));
                }
            }
        }

        protected override LayoutState StartLayout(IDockGroup root)
            => new GridLayoutState
            {
                Description = $"Root Grid {root}",
                CurrentFlow = new Vector()
                {
                    Direction = OrientationToFlowDirection(root.Orientation),
                },
            };

        protected override void PlaceDock(IDock dock)
        {
            this.CurrentVector.Items.Add(dock);
            Debug.WriteLine($"Place Dock: {dock}");
        }

        protected override void PlaceTray(IDockTray tray)
        {
            this.CurrentVector.Items.Add(tray);
            Debug.WriteLine($"Place Tray: {tray}");
        }

        protected override LayoutState StartFlow(IDockGroup group)
        {
            Debug.WriteLine($"New Grid for: {group}");
            var newFlow = new Vector { Direction = OrientationToFlowDirection(group.Orientation) };
            this.CurrentVector.Items.Add(newFlow);
            return new GridLayoutState()
            {
                Description = $"Grid for {group}",
                CurrentFlow = newFlow,
            };
        }

        protected override void EndFlow() => Debug.WriteLine($"Close flow {this.CurrentVector}");

        protected override void EndLayout() => Debug.WriteLine($"Layout ended");

        private static FlowDirection OrientationToFlowDirection(DockGroupOrientation orientation)
            => orientation == DockGroupOrientation.Undetermined
                ? FlowDirection.LeftToRight // Default flow direction if orientation is undetermined
                : orientation.ToFlowDirection();

        public sealed class Vector
        {
            public required FlowDirection Direction { get; init; }

            public List<object> Items { get; } = [];
        }

        private sealed class GridLayoutState : LayoutState
        {
            public required Vector CurrentFlow { get; init; }

            public override FlowDirection FlowDirection => this.CurrentFlow.Direction;
        }
    }

    private sealed class CenterDock : Dock
    {
        public override bool CanMinimize => false;

        public override bool CanClose => false;

        public static CenterDock New() => (CenterDock)Factory.CreateDock(typeof(CenterDock));
    }
}
