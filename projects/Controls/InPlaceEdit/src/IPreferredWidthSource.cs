// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     An editor that rearranges itself when narrow and reports, without being measured, the width it needs to show its
///     content on one row.
/// </summary>
/// <remarks>
///     A container that chooses its own arrangement from the editor's needs, such as a property row going inline or
///     stacked, reads <see cref="PreferredWidth"/> instead of measuring the editor without a width limit. Measuring
///     the live editor that way would make it rearrange itself for a width it never gets, and leave its layout state
///     from that probe in place of the one for its real width.
/// </remarks>
public interface IPreferredWidthSource
{
    /// <summary>
    ///     Gets the width, in device-independent pixels and excluding the editor's margin, at which the editor shows its
    ///     content on one row without stacking or squeezing any part of it.
    /// </summary>
    /// <remarks>
    ///     The width is computed from text metrics and the template parts' own properties; reading it never measures or
    ///     arranges the editor or its parts. It applies the editor's template when that has not happened yet.
    /// </remarks>
    public double PreferredWidth { get; }
}
