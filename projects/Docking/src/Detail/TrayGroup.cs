// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Collections.ObjectModel;
using DroidNet.Docking;
using DroidNet.Docking.Utils;

internal sealed class TrayGroup : DockGroupBase, IDockTray
{
    private readonly DockGroupOrientation orientation;
    private readonly AnchorPosition position;
    private readonly ObservableCollection<IDock> docks = [];

    public TrayGroup(AnchorPosition position, IDocker docker)
        : base(docker)
    {
        if (position == AnchorPosition.With)
        {
            throw new ArgumentException($"cannot use {position} for a {nameof(TrayGroup)}", nameof(position));
        }

        this.position = position;

        this.orientation
            = position is AnchorPosition.Left or AnchorPosition.Right
                ? DockGroupOrientation.Vertical
                : DockGroupOrientation.Horizontal;

        this.Docks = new ReadOnlyObservableCollection<IDock>(this.docks);
    }

    public override bool IsEmpty => this.docks.Count == 0;

    /// <inheritdoc />
    /// <remarks>This property does not make sense for a TrayGroup and should not be used.</remarks>
    public override IDockGroup? First
    {
        get => null;
        protected set => throw new InvalidOperationException();
    }

    /// <inheritdoc />
    /// <remarks>This property does not make sense for a TrayGroup and should not be used.</remarks>
    public override IDockGroup? Second
    {
        get => null;
        protected set => throw new InvalidOperationException();
    }

    public override ReadOnlyObservableCollection<IDock> Docks { get; }

    /// <inheritdoc cref="DockGroup" />
    /// <remarks>The orientation of a TrayGroup is determined from its position and should not be set afterward. Attempting to set
    /// it will result in a <see cref="InvalidOperationException" /> to be thrown.</remarks>
    public override DockGroupOrientation Orientation
    {
        get => this.orientation;
        protected set => throw new InvalidOperationException();
    }

    /// <inheritdoc />
    public override string ToString()
        => $"{this.Orientation.ToSymbol()} {this.DebugId} {this.position} TrayGroup ({this.Docks.Count})";

    internal void AddDock(IDock dock) => this.docks.Add(dock);

    internal bool RemoveDock(IDock dock) => this.docks.Remove(dock);
}
