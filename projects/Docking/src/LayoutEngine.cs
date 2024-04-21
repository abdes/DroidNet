// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using DroidNet.Docking.Utils;
using DroidNet.Docking.Workspace;

public abstract class LayoutEngine
{
    private readonly Stack<Flow> flows = new();

    public Flow CurrentFlow => this.flows.Peek();

    public abstract Flow StartLayout(ILayoutSegment segment);

    public abstract void PlaceDock(IDock dock);

    public abstract void PlaceTray(TrayGroup tray);

    public abstract Flow StartFlow(ILayoutSegment segment);

    public abstract void EndFlow();

    public abstract void EndLayout();

    internal void PushFlow(Flow state) => this.flows.Push(state); /* $"==> {state}"*/

    internal void PopFlow()
        => _ = this.flows.Pop(); /* $"<== {(this.flows.Count != 0 ? this.flows.Peek() : string.Empty)}" */

    public abstract class Flow(ILayoutSegment segment)
    {
        public string Description { get; init; } = string.Empty;

        public FlowDirection Direction { get; } = segment.Orientation.ToFlowDirection();

        public bool IsHorizontal => this.Direction == FlowDirection.LeftToRight;

        public bool IsVertical => this.Direction == FlowDirection.TopToBottom;

        public override string ToString() => this.Description;
    }
}
