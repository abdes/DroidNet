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
    public async Task TestLayoutBuilder()
    {
        using var docker = new Docker();

        docker.DockToCenter(CenterDock.New());

        using var left1 = ToolDock.New();
        left1.AdoptDockable(Dockable.New("left1"));
        docker.DockToRoot(left1, AnchorPosition.Left);

        using var left2 = ToolDock.New();
        left2.AdoptDockable(Dockable.New("left2"));
        docker.Dock(left2, new AnchorBottom(left1.Dockables[0]));

        docker.DockToRoot(ToolDock.New(), AnchorPosition.Left, minimized: true);
        docker.DockToRoot(ToolDock.New(), AnchorPosition.Left);
        docker.DockToRoot(ToolDock.New(), AnchorPosition.Top);
        docker.DockToRoot(ToolDock.New(), AnchorPosition.Bottom, minimized: true);

        using var right1 = ToolDock.New();
        right1.AdoptDockable(Dockable.New("right1"));
        docker.DockToRoot(right1, AnchorPosition.Right);
        docker.Dock(ToolDock.New(), new Anchor(AnchorPosition.Right, right1.Dockables[0]));
        docker.Dock(ToolDock.New(), new Anchor(AnchorPosition.Bottom, right1.Dockables[0]));

        docker.Root.DumpGroup();

        var gridLayout = new VectorLayoutEngine();
        var result = gridLayout.Build(docker.Root);

        gridLayout.DumpLayout();

        _ = await this.Verify(result).UseDirectory("Snapshots");
    }

    private sealed class VectorLayoutEngine : LayoutEngine
    {
        private Vector CurrentVector => ((GridFlow)this.CurrentFlow).Grid;

        public new Vector Build(IDockGroup root) => ((GridFlow)base.Build(root)).Grid;

        public void DumpLayout() => DumpLayoutItemRecursive(this.CurrentVector, 0);

        protected override Flow StartLayout(IDockGroup root)
            => new GridFlow(root)
            {
                Description = $"Root Grid {root}",
                Grid = new Vector()
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

        protected override Flow StartFlow(IDockGroup group)
        {
            Debug.WriteLine($"New Grid for: {group}");
            var newFlow = new Vector { Direction = OrientationToFlowDirection(group.Orientation) };
            this.CurrentVector.Items.Add(newFlow);
            return new GridFlow(group)
            {
                Description = $"Grid for {group}",
                Grid = newFlow,
            };
        }

        protected override void EndFlow() => Debug.WriteLine($"Close flow {this.CurrentVector}");

        protected override void EndLayout() => Debug.WriteLine("Layout ended");

        private static void DumpLayoutItemRecursive(object? item, int indentLevel)
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

        private static FlowDirection OrientationToFlowDirection(DockGroupOrientation orientation)
            => orientation == DockGroupOrientation.Undetermined
                ? FlowDirection.LeftToRight // Default flow direction if orientation is undetermined
                : orientation.ToFlowDirection();

        public sealed class Vector
        {
            public required FlowDirection Direction { get; init; }

            public List<object> Items { get; } = [];

            public override string ToString()
            {
                var direction = this.Direction == FlowDirection.LeftToRight ? "\u2b95" : "\u2b07";

                return $"{direction} Vector({this.Items.Count})";
            }
        }

        private sealed class GridFlow(IDockGroup group) : Flow(group)
        {
            public required Vector Grid { get; init; }
        }
    }

    private sealed class CenterDock : Dock
    {
        public override bool CanMinimize => false;

        public override bool CanClose => false;

        public static CenterDock New() => (CenterDock)Factory.CreateDock(typeof(CenterDock));
    }
}
