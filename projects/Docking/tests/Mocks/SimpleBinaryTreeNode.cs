// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using DroidNet.Docking.Workspace;

internal sealed class SimpleBinaryTreeNode(int storedValue) : BinaryTreeNode<int>(storedValue)
{
    public new BinaryTreeNode<int>? Left { get => base.Left; set => base.Left = value; }

    public new BinaryTreeNode<int>? Right { get => base.Right; set => base.Right = value; }
}
