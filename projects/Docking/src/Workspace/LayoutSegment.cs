// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using DroidNet.Docking.Utils;

public abstract class LayoutSegment(IDocker docker, DockGroupOrientation orientation) : ILayoutSegment
{
    private static int nextId = 1;

    public virtual DockGroupOrientation Orientation { get; internal set; } = orientation;

    public virtual bool StretchToFill { get; internal set; }

    public IDocker Docker { get; } = docker;

    public int DebugId { get; } = Interlocked.Increment(ref nextId);

    public override string? ToString()
        => $"{this.Orientation.ToSymbol()}{(this.StretchToFill ? " *" : string.Empty)} {this.DebugId}";
}
