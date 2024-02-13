// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Collections.ObjectModel;
using DroidNet.Docking;

internal sealed class TrayGroup : DockGroupBase, IDockTray
{
    private readonly ObservableCollection<IDock> minimizedDocks = [];
    private readonly DockGroupOrientation orientation;

    public TrayGroup(AnchorPosition position)
    {
        if (position == AnchorPosition.With)
        {
            throw new ArgumentException($"cannot use {position} for a {nameof(TrayGroup)}", nameof(position));
        }

        this.orientation
            = position is AnchorPosition.Left or AnchorPosition.Right
                ? DockGroupOrientation.Vertical
                : DockGroupOrientation.Horizontal;
        this.MinimizedDocks = new ReadOnlyObservableCollection<IDock>(this.minimizedDocks);
    }

    public ReadOnlyObservableCollection<IDock> MinimizedDocks { get; }

    public override ReadOnlyObservableCollection<IDock> Docks { get; } = new([]);

    public override bool IsEmpty => this.MinimizedDocks.Count == 0;

    public override IDockGroup? First
    {
        get => null;
        protected set => throw new InvalidOperationException();
    }

    public override IDockGroup? Second
    {
        get => null;
        protected set => throw new InvalidOperationException();
    }

    public override DockGroupOrientation Orientation
    {
        get => this.orientation;
        protected set => throw new InvalidOperationException();
    }

    internal void AddMinimizedDock(IDock dock) => this.minimizedDocks.Add(dock);

    internal bool RemoveMinimizedDock(IDock dock) => this.minimizedDocks.Remove(dock);
}
