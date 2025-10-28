// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
///     Provides data for the event that occurs when a document is detached from the editor.
/// </summary>
public class DocumentDetachedEventArgs : EventArgs
{
    /// <summary>
    /// Gets the document that was detached.
    /// </summary>
    public required TabbedDocumentItem Document { get; init; }
}
