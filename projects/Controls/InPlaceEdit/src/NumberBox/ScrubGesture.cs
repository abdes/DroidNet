// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>Turns horizontal pointer travel on a scrub handle into whole value steps.</summary>
/// <remarks>
///     A label scrubs as soon as it is pressed. The value region also opens the text editor on a tap, so it scrubs
///     only after the pointer has travelled <see cref="DragThreshold"/> horizontally, and the tap that ends such a
///     scrub must not open the editor. A scrub makes one step per DIP of horizontal travel; vertical travel and the
///     rate of pointer events never add steps.
/// </remarks>
internal sealed class ScrubGesture
{
    /// <summary>The horizontal travel, in DIPs, that turns a press on the value region into a scrub.</summary>
    public const double DragThreshold = 4;

    private double? pressX;
    private double lastX;
    private bool suppressTap;

    /// <summary>Gets a value indicating whether a scrub is in progress.</summary>
    public bool IsScrubbing { get; private set; }

    /// <summary>Starts a gesture at a press.</summary>
    /// <param name="x">The horizontal pointer position, relative to the handle.</param>
    /// <param name="immediate">Whether the scrub starts now, as on a label, rather than after the drag threshold.</param>
    public void Press(double x, bool immediate)
    {
        this.pressX = x;
        this.lastX = x;
        this.IsScrubbing = immediate;
        this.suppressTap = false;
    }

    /// <summary>Follows the pointer.</summary>
    /// <param name="x">The horizontal pointer position, relative to the handle.</param>
    /// <param name="isInContact">Whether the pointer is still pressed.</param>
    /// <returns>Whether this move started the scrub, and the whole steps travelled since the last applied step.</returns>
    public ScrubMove Move(double x, bool isInContact)
    {
        if (this.pressX is not { } press)
        {
            return default;
        }

        if (!this.IsScrubbing)
        {
            if (!isInContact)
            {
                this.pressX = null;
                return default;
            }

            if (Math.Abs(x - press) < DragThreshold)
            {
                return default;
            }

            this.IsScrubbing = true;
            this.lastX = x;
            return new ScrubMove(Started: true, Steps: 0);
        }

        var steps = Math.Truncate(x - this.lastX);
        this.lastX += steps;
        return new ScrubMove(Started: false, Steps: (int)steps);
    }

    /// <summary>Ends the gesture at a release.</summary>
    /// <returns>Whether a scrub was in progress, which also suppresses the tap that follows it.</returns>
    public bool Release()
    {
        var wasScrubbing = this.IsScrubbing;
        this.Reset();
        this.suppressTap = wasScrubbing;
        return wasScrubbing;
    }

    /// <summary>Abandons the gesture, as when capture is lost or the edit is cancelled.</summary>
    public void Cancel()
    {
        this.suppressTap = this.IsScrubbing;
        this.Reset();
    }

    /// <summary>Decides whether a tap on the value region may open the editor.</summary>
    /// <returns><see langword="false"/> once, for the tap that ends a scrub; otherwise <see langword="true"/>.</returns>
    public bool AcceptTap()
    {
        var accepted = !this.suppressTap;
        this.suppressTap = false;
        return accepted;
    }

    private void Reset()
    {
        this.pressX = null;
        this.IsScrubbing = false;
    }
}
