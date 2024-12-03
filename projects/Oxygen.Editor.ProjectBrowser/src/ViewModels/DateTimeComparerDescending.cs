// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// Compares <see cref="DateTime"/> values in descending order.
/// </summary>
internal sealed class DateTimeComparerDescending : Comparer<DateTime>
{
    /// <inheritdoc/>
    public override int Compare(DateTime x, DateTime y) => y.CompareTo(x);
}
