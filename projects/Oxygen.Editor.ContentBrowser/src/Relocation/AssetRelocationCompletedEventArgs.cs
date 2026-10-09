// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Relocation;

/// <summary>Carries the outcome of a completed or failed relocation change.</summary>
/// <param name="outcome">The result to show and whether Undo is available.</param>
public sealed class AssetRelocationCompletedEventArgs(AssetRelocationOutcome outcome) : EventArgs
{
    /// <summary>Gets the result to show and whether Undo is available.</summary>
    public AssetRelocationOutcome Outcome { get; } = outcome;
}
