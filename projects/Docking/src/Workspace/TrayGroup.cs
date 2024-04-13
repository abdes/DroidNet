// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using DroidNet.Docking;

public sealed class TrayGroup : DockGroup
{
    private readonly AnchorPosition position;

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "StyleCop.CSharp.ReadabilityRules",
        "SA1118:Parameter should not span multiple lines",
        Justification = "parameter uses conditional expression")]
    public TrayGroup(IDocker docker, AnchorPosition position)
        : base(
            docker,
            position is AnchorPosition.Left or AnchorPosition.Right
                ? DockGroupOrientation.Vertical
                : DockGroupOrientation.Horizontal)
    {
        if (position is AnchorPosition.With or AnchorPosition.Center)
        {
            throw new ArgumentException($"cannot use {position} for a {nameof(TrayGroup)}", nameof(position));
        }

        this.position = position;
    }

    public override DockGroupOrientation Orientation
    {
        get => base.Orientation;
        internal set => throw new InvalidOperationException(
            $"orientation of an {nameof(TrayGroup)} can only be set at creation");
    }

    /// <inheritdoc />
    public override string ToString() => $"{this.position} TrayGroup {base.ToString()}";

    internal override void AddDock(IDock dock, Anchor anchor)
        => throw new InvalidOperationException("cannot add a dock with an anchor in a try group");
}
