// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using DroidNet.Docking.Detail;

internal sealed class CenterGroup(IDocker docker) : DockGroup(docker, DockGroupOrientation.Horizontal), IDisposable
{
    private bool disposed;

    public override bool StretchToFill
    {
        get => true;
        internal set
            => throw new InvalidOperationException(
                $"a {nameof(CenterGroup)} should always have the layout property {nameof(this.StretchToFill)} `true`");
    }

    public override string ToString() => "\u25cb " + base.ToString();

    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.ClearDocks();

        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    /// <remarks>The <see cref="CenterGroup" /> owns docks added to it, and will be responsible for their disposal.</remarks>
    internal override void AddDock(IDock dock)
    {
        base.AddDock(dock);
        dock.AsDock().Group = this;
    }

    /// <inheritdoc />
    internal override void AddDock(IDock dock, Anchor anchor) => this.AddDock(dock);

    /// <summary>Removes, and <b>disposes of</b>, all docks in this <see cref="CenterGroup" />.</summary>
    protected override void ClearDocks()
    {
        // LayoutDockGroup owns the docks and should dispose of them.
        foreach (var dock in this.Docks)
        {
            dock.AsDock().Group = null;

            if (dock is IDisposable resource)
            {
                resource.Dispose();
            }
        }

        base.ClearDocks();
    }
}
