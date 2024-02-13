// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

public class Anchor(AnchorPosition position, DockId? dockId = null)
{
    public AnchorPosition Position { get; } = position;

    public DockId? DockId { get; } = dockId;
}

public class AnchorLeft(DockId? dockId = null) : Anchor(AnchorPosition.Left, dockId);

public class AnchorRight(DockId? dockId = null) : Anchor(AnchorPosition.Right, dockId);

public class AnchorTop(DockId? dockId = null) : Anchor(AnchorPosition.Top, dockId);

public class AnchorBottom(DockId? dockId = null) : Anchor(AnchorPosition.Bottom, dockId);
