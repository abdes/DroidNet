// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics;

/// <summary>
/// Represents an anchor point for a dockable object in a workspace.
/// </summary>
/// <remarks>
/// Dockables can be positioned relative to either the workspace root or other anchors. The workspace root offers five anchor
/// locations: left, right, top, bottom, and center. Relative docking to other dockables can be done: left, right, top, and bottom
/// of or with another dockable.
/// <para>
/// Anchors can be moved due to dockable re-positioning or due to the disposal of the relative dockable. In case of disposal, the
/// anchor repositions itself to the disposed dockable’s relative dockable or defaults to the workspace’s left edge.
/// </para>
/// </remarks>
public partial class Anchor : IDisposable
{
    /// <summary>Keep track of when this object was disposed of.</summary>
    private bool isDisposed;

    /// <summary>The dockable relative to which the anchor is positioned.</summary>
    private IDockable? dockable;

    /// <summary>Initializes a new instance of the <see cref="Anchor" /> class.</summary>
    /// <param name="position">The position of the anchor.</param>
    /// <param name="relativeTo">The dockable object to which this anchor is relative.</param>
    public Anchor(AnchorPosition position, IDockable? relativeTo = null)
    {
        this.Position = position;
        this.RelativeTo = relativeTo;
    }

    /// <summary>Gets the <see cref="AnchorPosition">position</see> of the anchor.</summary>
    public AnchorPosition Position { get; private set; }

    /// <summary>Gets the dockable object to which this anchor is relative.</summary>
    public IDockable? RelativeTo
    {
        get => this.dockable;
        private set
        {
            /*
             * The anchor keeps track of its relative dockable, in case it gets disposed of. When that happens, the anchor must
             * automatically choose a new anchor point, either as the anchor point of the disposed dockable, or as the workspace
             * root.
             */

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

    /// <inheritdoc />
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    public override string ToString()
        => $"{this.Position} {(this.dockable == null ? "Edge" : $"of `{this.dockable.Id}` in dock `{this.dockable.Owner}`")}";

    /// <summary>
    /// Releases the unmanaged resources used by the <see cref="Anchor" /> and optionally releases
    /// the managed resources.
    /// </summary>
    /// <param name="disposing">
    /// <see langword="true" /> to release both managed and unmanaged resources; <see langword="false" />
    /// to release only unmanaged resources.
    /// </param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            /* Dispose of managed resources */
            this.RelativeTo = null;
        }

        /* Dispose of unmanaged resources */

        this.isDisposed = true;
    }

    /// <summary>
    /// Handles the event when the dockable object to which this anchor is attached is disposed of. Automatically chooses a new
    /// anchor point, either as the anchor point of the disposed dockable, if it had one, or at the left edge of the workspace
    /// root.
    /// </summary>
    /// <param name="sender">The origin of the event.</param>
    /// <param name="args">The event arguments (unused).</param>
    private void OnDockableDisposed(object? sender, EventArgs? args)
    {
        Debug.Assert(
            this.RelativeTo != null,
            "if we still have an event subscription, the dockable should not be null");
        var newAnchor = this.RelativeTo.Owner?.Anchor;
        if (newAnchor != null)
        {
            // $"My anchor dockable was disposed of, anchoring myself {newAnchor.Position} relative to {newAnchor.RelativeTo?.ToString() ?? "root"}"
            this.Position = newAnchor.Position;
            this.RelativeTo = newAnchor.RelativeTo;
        }
        else
        {
            // $"My anchor dockable was disposed of, new anchor is null! Anchoring myself {nameof(AnchorPosition.Left)} relative to root"
            this.Position = AnchorPosition.Left;
            this.RelativeTo = null;
        }
    }
}

/// <summary>Represents an anchor point on the left side of a dockable object or workspace.</summary>
/// <param name="relativeTo">The dockable object to which this anchor is relative.</param>
public partial class AnchorLeft(IDockable? relativeTo = null) : Anchor(AnchorPosition.Left, relativeTo);

/// <summary>Represents an anchor point on the right side of a dockable object or workspace.</summary>
/// <param name="relativeTo">The dockable object to which this anchor is relative.</param>
public partial class AnchorRight(IDockable? relativeTo = null) : Anchor(AnchorPosition.Right, relativeTo);

/// <summary>Represents an anchor point on the top side of a dockable object or workspace.</summary>
/// <param name="relativeTo">The dockable object to which this anchor is relative.</param>
public partial class AnchorTop(IDockable? relativeTo = null) : Anchor(AnchorPosition.Top, relativeTo);

/// <summary>Represents an anchor point on the bottom side of a dockable object or workspace.</summary>
/// <param name="relativeTo">The dockable object to which this anchor is relative.</param>
public partial class AnchorBottom(IDockable? relativeTo = null) : Anchor(AnchorPosition.Bottom, relativeTo);
