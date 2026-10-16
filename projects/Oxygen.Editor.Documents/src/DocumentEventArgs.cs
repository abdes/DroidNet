// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>Names the document an event is about.</summary>
/// <param name="document">The document.</param>
public sealed class DocumentEventArgs(Document document) : EventArgs
{
    /// <summary>Gets the document.</summary>
    public Document Document { get; } = document;
}
