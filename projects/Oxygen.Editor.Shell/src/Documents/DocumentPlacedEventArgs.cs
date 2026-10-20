// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Documents;

/// <summary>Names the outcome of a request to open a document.</summary>
/// <param name="placement">The outcome.</param>
public sealed class DocumentPlacedEventArgs(DocumentPlacement placement) : EventArgs
{
    /// <summary>Gets the outcome.</summary>
    public DocumentPlacement Placement { get; } = placement;
}
