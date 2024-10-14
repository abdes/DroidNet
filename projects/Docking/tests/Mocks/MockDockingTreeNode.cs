// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using DroidNet.Docking;
using DroidNet.Docking.Workspace;

/// <summary>
/// A mock class for the <see cref="DockingTreeNode" /> class.
/// </summary>
/// <param name="docker">
/// The docker managing this item.
/// </param>
/// <param name="segment">
/// The <see cref="LayoutSegment" /> to which this node belongs.
/// </param>
internal sealed partial class MockDockingTreeNode(IDocker docker, LayoutSegment segment)
    : DockingTreeNode(docker, segment)
{
    public new DockingTreeNode? Left
    {
        get => base.Left;
        init => base.Left = value;
    }

    public new DockingTreeNode? Right
    {
        get => base.Right;
        init => base.Right = value;
    }
}
