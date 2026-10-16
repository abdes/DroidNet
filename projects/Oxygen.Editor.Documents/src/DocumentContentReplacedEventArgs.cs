// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>Tells the editors of a document that a reload replaced its content.</summary>
/// <param name="previous">The content that was replaced; it is disposed once the event returns.</param>
/// <param name="current">The content read from the file.</param>
public sealed class DocumentContentReplacedEventArgs(IDocumentContent previous, IDocumentContent current) : EventArgs
{
    /// <summary>Gets the content that was replaced; it is disposed once the event returns.</summary>
    public IDocumentContent Previous { get; } = previous;

    /// <summary>Gets the content read from the file.</summary>
    public IDocumentContent Current { get; } = current;
}
