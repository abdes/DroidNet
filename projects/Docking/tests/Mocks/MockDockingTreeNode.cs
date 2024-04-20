// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using DroidNet.Docking;
using DroidNet.Docking.Workspace;

internal sealed class MockDockingTreeNode(IDocker docker, LayoutSegment segment) : DockingTreeNode(docker, segment)
{
    public new DockingTreeNode? Left { get => base.Left; set => base.Left = value; }

    public new DockingTreeNode? Right { get => base.Right; set => base.Right = value; }
}
