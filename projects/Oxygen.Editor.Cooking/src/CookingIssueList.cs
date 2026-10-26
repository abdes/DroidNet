// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Cooking;

/// <summary>The issues of one run: diagnostics that say the same thing share one issue, most serious first.</summary>
public sealed class CookingIssueList : ObservableCollection<CookingIssueViewModel>
{
    private readonly HashSet<Guid> seen = [];

    /// <summary>Gets how many diagnostics the issues summarize.</summary>
    public int Total { get; private set; }

    /// <summary>Adds a diagnostic once, folding it into the issue that already says the same thing.</summary>
    /// <param name="diagnostic">The diagnostic to report.</param>
    /// <param name="scope">The name of the asset or scene it applies to.</param>
    internal void Report(DiagnosticRecord diagnostic, string scope)
    {
        if (!this.seen.Add(diagnostic.DiagnosticId))
        {
            return;
        }

        if (this.FirstOrDefault(issue => issue.Summarizes(diagnostic)) is { } existing)
        {
            existing.Absorb(diagnostic, scope);
        }
        else
        {
            this.Add(new(diagnostic, scope));
        }

        this.Total++;
    }

    /// <summary>Orders the issues by severity, then by how many places they affect.</summary>
    internal void Reorder()
    {
        var desired = this.OrderBy(static issue => issue.Rank).ThenByDescending(static issue => issue.Count).ThenBy(static issue => issue.Message, StringComparer.Ordinal).ToList();
        for (var index = 0; index < desired.Count; index++)
        {
            var at = this.IndexOf(desired[index]);
            if (at != index)
            {
                this.Move(at, index);
            }
        }
    }
}
