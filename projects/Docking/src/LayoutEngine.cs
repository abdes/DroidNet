// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using DroidNet.Docking.Workspace;

public abstract class LayoutEngine
{
    private readonly Stack<LayoutFlow> flows = new();

    public LayoutFlow CurrentFlow => this.flows.Peek();

    public abstract LayoutFlow StartLayout(ILayoutSegment segment);

    public abstract void PlaceDock(IDock dock);

    public abstract void PlaceTray(TrayGroup tray);

    public abstract LayoutFlow StartFlow(ILayoutSegment segment);

    public abstract void EndFlow();

    public abstract void EndLayout();

    internal void PushFlow(LayoutFlow state) => this.flows.Push(state); /* $"==> {state}"*/

    internal void PopFlow()
        => _ = this.flows.Pop(); /* $"<== {(this.flows.Count != 0 ? this.flows.Peek() : string.Empty)}" */
}
