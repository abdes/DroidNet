// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

public interface IDocker
{
    IDockGroup Root { get; }

    void CloseDock(IDock dock);

    void Dock(IDock dock, Anchor anchor, bool minimized = false);

    void DockToCenter(IDock dock);

    void DockToRoot(IDock dock, AnchorPosition position, bool minimized = false);

    void FloatDock(IDock dock);

    void MinimizeDock(IDock dock);

    void PinDock(IDock dock);
}
