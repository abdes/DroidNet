// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Moq;

namespace DroidNet.Aura.Drag.Tests;

/// <summary>
/// Extension methods for verifying interactions with <see cref="Mock{ITabStrip}"/>.
/// </summary>
[ExcludeFromCodeCoverage]
internal static class TabStripMockExtensions
{
    /// <summary>
    /// Verifies that TakeSnapshot was called on the mock.
    /// </summary>
    /// <param name="mock">The TabStrip mock.</param>
    /// <param name="times">The expected number of times.</param>
    public static void VerifyTakeSnapshot(this Mock<ITabStrip> mock, Times times)
        => mock.Verify(m => m.TakeSnapshot(), times);

    /// <summary>
    /// Verifies that RemoveItemAt was called with the specified index.
    /// </summary>
    /// <param name="mock">The TabStrip mock.</param>
    /// <param name="index">The expected index.</param>
    /// <param name="times">The expected number of times.</param>
    public static void VerifyRemoveItemAt(this Mock<ITabStrip> mock, int index, Times times)
        => mock.Verify(m => m.RemoveItemAt(index), times);

    /// <summary>
    /// Verifies that MoveItem was called with the specified arguments.
    /// </summary>
    /// <param name="mock">The TabStrip mock.</param>
    /// <param name="fromIndex">The expected source index.</param>
    /// <param name="toIndex">The expected destination index.</param>
    /// <param name="times">The expected invocation count.</param>
    public static void VerifyMoveItem(this Mock<ITabStrip> mock, int fromIndex, int toIndex, Times times)
        => mock.Verify(m => m.MoveItem(fromIndex, toIndex), times);

    /// <summary>
    /// Verifies that ApplyTransformToItem was called.
    /// </summary>
    /// <param name="mock">The TabStrip mock.</param>
    /// <param name="times">The expected number of times.</param>
    public static void VerifyApplyTransform(this Mock<ITabStrip> mock, Times times)
        => mock.Verify(m => m.ApplyTransformToItem(It.IsAny<int>(), It.IsAny<double>()), times);
}
