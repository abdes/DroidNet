// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>The result of a save.</summary>
/// <param name="Outcome">How the save ended.</param>
/// <param name="Message">Why the save was refused or conflicted, as people read it; null when it saved.</param>
public sealed record DocumentSaveResult(DocumentSaveOutcome Outcome, string? Message = null)
{
    /// <summary>Gets a value indicating whether the save wrote what the document captured, or had nothing to write.</summary>
    public bool Succeeded => this.Outcome == DocumentSaveOutcome.Saved;
}
