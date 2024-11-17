// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;

namespace DroidNet.Docking.Utils;

/// <summary>
/// Provides extension methods for the <see cref="DockGroupOrientation"/> enumeration.
/// </summary>
/// <remarks>
/// This class includes methods to convert <see cref="DockGroupOrientation"/> values to symbols and flow directions.
/// </remarks>
public static class DockGroupOrientationExtensions
{
    /// <summary>
    /// Converts the specified <see cref="DockGroupOrientation"/> to its corresponding symbol.
    /// </summary>
    /// <param name="orientation">The <see cref="DockGroupOrientation"/> value to convert.</param>
    /// <returns>
    /// A <see cref="string"/> representing the symbol for the given <paramref name="orientation"/>.
    /// </returns>
    /// <exception cref="InvalidEnumArgumentException">
    /// Thrown when the <paramref name="orientation"/> is not a valid <see cref="DockGroupOrientation"/> value.
    /// </exception>
    /// <example>
    /// <code><![CDATA[
    /// DockGroupOrientation orientation = DockGroupOrientation.Horizontal;
    /// string symbol = orientation.ToSymbol();
    /// // symbol is "↔"
    /// ]]></code>
    /// </example>
    /// <remarks>
    /// <para>
    /// The method returns the following symbols for each <see cref="DockGroupOrientation"/>:
    /// <list type="bullet">
    /// <item><description><see cref="DockGroupOrientation.Undetermined"/>: "?"</description></item>
    /// <item><description><see cref="DockGroupOrientation.Horizontal"/>: "↔"</description></item>
    /// <item><description><see cref="DockGroupOrientation.Vertical"/>: "↕"</description></item>
    /// </list>
    /// </para>
    /// </remarks>
    public static string ToSymbol(this DockGroupOrientation orientation)
        => orientation switch
        {
            DockGroupOrientation.Undetermined => "?",
            DockGroupOrientation.Horizontal => "\u2194",
            DockGroupOrientation.Vertical => "\u2195",
            _ => throw new InvalidEnumArgumentException(
                nameof(orientation),
                (int)orientation,
                typeof(DockGroupOrientation)),
        };

    /// <summary>
    /// Converts the specified <see cref="DockGroupOrientation"/> to its corresponding <see cref="FlowDirection"/>.
    /// </summary>
    /// <param name="orientation">The <see cref="DockGroupOrientation"/> value to convert.</param>
    /// <returns>
    /// A <see cref="FlowDirection"/> representing the flow direction for the given <paramref name="orientation"/>.
    /// </returns>
    /// <exception cref="ArgumentException">
    /// Thrown when the <paramref name="orientation"/> is <see cref="DockGroupOrientation.Undetermined"/>.
    /// </exception>
    /// <exception cref="InvalidEnumArgumentException">
    /// Thrown when the <paramref name="orientation"/> is not a valid <see cref="DockGroupOrientation"/> value.
    /// </exception>
    /// <example>
    /// <code><![CDATA[
    /// DockGroupOrientation orientation = DockGroupOrientation.Horizontal;
    /// FlowDirection flowDirection = orientation.ToFlowDirection();
    /// // flowDirection is FlowDirection.LeftToRight
    /// ]]></code>
    /// </example>
    /// <remarks>
    /// <para>
    /// The method returns the following <see cref="FlowDirection"/> for each <see cref="DockGroupOrientation"/>:
    /// <list type="bullet">
    /// <item><description><see cref="DockGroupOrientation.Horizontal"/>: <see cref="FlowDirection.LeftToRight"/></description></item>
    /// <item><description><see cref="DockGroupOrientation.Vertical"/>: <see cref="FlowDirection.TopToBottom"/></description></item>
    /// </list>
    /// </para>
    /// </remarks>
    public static FlowDirection ToFlowDirection(this DockGroupOrientation orientation)
        => orientation switch
        {
            DockGroupOrientation.Undetermined => throw new ArgumentException(
                $"When orientation is {orientation}, there is no automatic mapping to a {nameof(FlowDirection)}",
                nameof(orientation)),
            DockGroupOrientation.Horizontal => FlowDirection.LeftToRight,
            DockGroupOrientation.Vertical => FlowDirection.TopToBottom,
            _ => throw new InvalidEnumArgumentException(
                nameof(orientation),
                (int)orientation,
                typeof(DockGroupOrientation)),
        };
}
