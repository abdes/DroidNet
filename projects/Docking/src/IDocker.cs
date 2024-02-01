// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

public interface IDocker
{
    IDockGroup Root { get; }

    void Dock(IDock dock, Anchor anchor);

    void DockToCenter(IDock dock);

    void DockToRoot(IDock dock, AnchorPosition position);
}
