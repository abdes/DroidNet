// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core;

/// <summary>
/// Compares names and virtual paths under Unicode 15.1 simple case folding (<see cref="VirtualPath.FoldCase"/>). Identity
/// is case-sensitive, but a case-insensitive file system holds two paths equal under this comparer in one file, so a
/// name equal to an existing sibling under it is refused where it is authored.
/// </summary>
public sealed class VirtualPathComparer : IEqualityComparer<string>
{
    private VirtualPathComparer()
    {
    }

    /// <summary>Gets the comparer that treats case-only siblings as equal.</summary>
    public static VirtualPathComparer CaseFolded { get; } = new();

    /// <inheritdoc/>
    public bool Equals(string? x, string? y)
        => x is null || y is null
            ? ReferenceEquals(x, y)
            : string.Equals(Fold(x), Fold(y), StringComparison.Ordinal);

    /// <inheritdoc/>
    public int GetHashCode(string obj)
    {
        ArgumentNullException.ThrowIfNull(obj);
        return StringComparer.Ordinal.GetHashCode(Fold(obj));
    }

    // Text that is not Unicode has no folding; it is compared as it is, and the rules refuse it anyway.
    private static string Fold(string text) => VirtualPath.FoldCase(text) ?? text;
}
