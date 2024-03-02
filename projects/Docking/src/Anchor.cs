// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics;

public class Anchor : IDisposable
{
    // Keep track of when this object was disposed of.
    private bool disposed;

    private IDockable? dockable;

    public Anchor(AnchorPosition position, IDockable? relativeTo = null)
    {
        this.Position = position;
        this.RelativeTo = relativeTo;
    }

    public AnchorPosition Position { get; private set; }

    public IDockable? RelativeTo
    {
        get => this.dockable;
        private set
        {
            if (this.dockable != null)
            {
                // Unsubscribe
                this.dockable.OnDisposed -= this.OnDockableDisposed;
            }

            this.dockable = value;

            // Subscribe
            if (this.dockable != null)
            {
                this.dockable.OnDisposed += this.OnDockableDisposed;
            }
        }
    }

    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.RelativeTo = null;
        this.disposed = true;

        GC.SuppressFinalize(this);
    }

    private void OnDockableDisposed()
    {
        Debug.Assert(
            this.RelativeTo != null,
            "if we still have an event subscription, the dockable should not be null");
        var newAnchor = this.RelativeTo.Owner?.Anchor;
        if (newAnchor != null)
        {
            Debug.WriteLine(
                $"My anchor dockable was disposed of, anchoring myself {newAnchor.Position} relative to {(newAnchor.RelativeTo == null ? "root" : newAnchor.RelativeTo)}");
            this.Position = newAnchor.Position;
            this.RelativeTo = newAnchor.RelativeTo;
        }
        else
        {
            Debug.WriteLine(
                $"My anchor dockable was disposed of, new anchor is null! Anchoring myself {AnchorPosition.Left} relative to root");
            this.Position = AnchorPosition.Left;
            this.RelativeTo = null;
        }
    }
}

public class AnchorLeft(IDockable? relativeTo = null) : Anchor(AnchorPosition.Left, relativeTo);

public class AnchorRight(IDockable? relativeTo = null) : Anchor(AnchorPosition.Right, relativeTo);

public class AnchorTop(IDockable? relativeTo = null) : Anchor(AnchorPosition.Top, relativeTo);

public class AnchorBottom(IDockable? relativeTo = null) : Anchor(AnchorPosition.Bottom, relativeTo);
