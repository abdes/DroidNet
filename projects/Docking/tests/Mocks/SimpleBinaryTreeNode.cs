// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using DroidNet.Docking.Workspace;

/// <summary>
/// A simple <see cref="BinaryTreeNode{T}" /> for testing.
/// </summary>
/// <param name="storedValue">
/// The initial value stored in the node.
/// </param>
internal sealed partial class SimpleBinaryTreeNode(int storedValue) : BinaryTreeNode<int>(storedValue)
{
    public new BinaryTreeNode<int>? Left
    {
        get => base.Left;
        set => base.Left = value;
    }

    public new BinaryTreeNode<int>? Right
    {
        get => base.Right;
        set => base.Right = value;
    }
}
