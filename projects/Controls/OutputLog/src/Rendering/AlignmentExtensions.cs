// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Serilog.Parsing;

namespace DroidNet.Controls.OutputLog.Rendering;

/// <summary>
/// Provides extension methods for working with <see cref="Alignment"/> structures.
/// </summary>
/// <remarks>
/// <para>
/// This class contains extension methods that simplify the manipulation of <see cref="Alignment"/> structures,
/// such as widening the alignment width.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// These methods are designed to be used in conjunction with rendering operations that require text alignment.
/// Ensure that the alignment values are appropriate for the context in which they are used to avoid formatting issues.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var alignment = new Alignment(AlignmentDirection.Left, 10);
/// var widenedAlignment = alignment.Widen(5);
/// // widenedAlignment now has a width of 15
/// ]]></code>
/// </para>
/// </remarks>
internal static class AlignmentExtensions
{
    /// <summary>
    /// Widens the alignment by the specified amount.
    /// </summary>
    /// <param name="alignment">The alignment to widen.</param>
    /// <param name="amount">The amount by which to widen the alignment.</param>
    /// <returns>A new <see cref="Alignment"/> structure with the widened width.</returns>
    /// <remarks>
    /// <para>
    /// This method creates a new <see cref="Alignment"/> structure with the same direction as the original,
    /// but with the width increased by the specified amount.
    /// </para>
    /// <para>
    /// <strong>Corner Cases:</strong>
    /// If the resulting width is negative, it will be set to zero.
    /// </para>
    /// </remarks>
    public static Alignment Widen(this Alignment alignment, int amount)
        => new(alignment.Direction, Math.Max(0, alignment.Width + amount));
}
