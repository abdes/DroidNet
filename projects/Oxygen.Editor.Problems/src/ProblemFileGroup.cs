// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;

namespace Oxygen.Editor.Problems;

/// <summary>
/// The problems of one file in the Problems panel.
/// </summary>
/// <param name="SourcePath">The file.</param>
/// <param name="DisplayPath">The file as shown: its project-relative path.</param>
/// <param name="Entries">The problems, in source order.</param>
public sealed record ProblemFileGroup(string SourcePath, string DisplayPath, IReadOnlyList<ProblemEntry> Entries)
{
    /// <summary>Gets the group header: the file name and its problem count.</summary>
    public string Header => string.Create(CultureInfo.CurrentCulture, $"{Path.GetFileName(this.DisplayPath)} ({this.Entries.Count})");
}
