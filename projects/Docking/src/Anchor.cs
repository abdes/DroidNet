// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

public class Anchor(DockId dockId, AnchorPosition position)
{
    public AnchorPosition Position { get; } = position;

    public DockId DockId { get; } = dockId;
}

public class AnchorLeft(DockId dockId) : Anchor(dockId, AnchorPosition.Left);

public class AnchorRight(DockId dockId) : Anchor(dockId, AnchorPosition.Right);

public class AnchorTop(DockId dockId) : Anchor(dockId, AnchorPosition.Top);

public class AnchorBottom(DockId dockId) : Anchor(dockId, AnchorPosition.Bottom);
