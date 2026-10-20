// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Documents;

/// <summary>Names the document tab an event is about.</summary>
/// <param name="tab">The tab.</param>
internal sealed class DocumentTabEventArgs(DocumentTab tab) : EventArgs
{
    /// <summary>Gets the tab.</summary>
    public DocumentTab Tab { get; } = tab;
}
