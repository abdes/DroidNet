// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Utils;
using DroidNet.Docking.Workspace;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(LayoutEngine))]
public class LayoutEngineTests : VerifyBase
{
    [TestMethod]
    public async Task TestLayoutBuilder()
    {
        using var docker = new Docker();

        docker.Dock(CenterDock.New(), new Anchor(AnchorPosition.Center));

        using var left1 = ToolDock.New();
        left1.AdoptDockable(Dockable.New("left1"));
        docker.Dock(left1, new AnchorLeft());

        using var left2 = ToolDock.New();
        left2.AdoptDockable(Dockable.New("left2"));
        docker.Dock(left2, new AnchorBottom(left1.Dockables[0]));

        docker.Dock(ToolDock.New(), new AnchorLeft(), minimized: true);
        docker.Dock(ToolDock.New(), new AnchorLeft());
        docker.Dock(ToolDock.New(), new AnchorTop());
        docker.Dock(ToolDock.New(), new AnchorBottom(), minimized: true);

        using var right1 = ToolDock.New();
        right1.AdoptDockable(Dockable.New("right1"));
        docker.Dock(right1, new AnchorRight());
        docker.Dock(ToolDock.New(), new Anchor(AnchorPosition.Right, right1.Dockables[0]));
        docker.Dock(ToolDock.New(), new Anchor(AnchorPosition.Bottom, right1.Dockables[0]));

        docker.DumpWorkspace();

        var gridLayout = new VectorLayoutEngine();
        docker.Layout(gridLayout);

        gridLayout.DumpLayout();

        var result = gridLayout.CurrentFlow;
        _ = await this.Verify(result).UseDirectory("Snapshots");
    }

    private sealed class VectorLayoutEngine : LayoutEngine
    {
        private Vector CurrentVector => ((GridFlow)this.CurrentFlow).Grid;

        public void DumpLayout() => DumpLayoutItemRecursive(this.CurrentVector, 0);

        public override Flow StartLayout(ILayoutSegment segment)
            => new GridFlow(segment)
            {
                Description = $"Root Grid {segment}",
                Grid = new Vector()
                {
                    Direction = OrientationToFlowDirection(segment.Orientation),
                },
            };

        public override void PlaceDock(IDock dock)
        {
            this.CurrentVector.Items.Add(dock);
            Debug.WriteLine($"Place Dock: {dock}");
        }

        public override void PlaceTray(TrayGroup tray)
        {
            this.CurrentVector.Items.Add(tray);
            Debug.WriteLine($"Place Tray: {tray}");
        }

        public override Flow StartFlow(ILayoutSegment segment)
        {
            Debug.WriteLine($"New Grid for: {segment}");
            var newFlow = new Vector { Direction = OrientationToFlowDirection(segment.Orientation) };
            this.CurrentVector.Items.Add(newFlow);
            return new GridFlow(segment)
            {
                Description = $"Grid for {segment}",
                Grid = newFlow,
            };
        }

        public override void EndFlow() => Debug.WriteLine($"Close flow {this.CurrentVector}");

        public override void EndLayout() => Debug.WriteLine("Layout ended");

        private static void DumpLayoutItemRecursive(object? item, int indentLevel)
        {
            var indent = new string(' ', indentLevel * 2); // 2 spaces per indent level

            switch (item)
            {
                case null:
                    return;

                case TrayGroup tray:
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

        private sealed class GridFlow(ILayoutSegment segment) : Flow(segment)
        {
            public required Vector Grid { get; init; }
        }
    }
}
