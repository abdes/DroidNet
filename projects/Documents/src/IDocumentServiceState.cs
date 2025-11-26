// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using Microsoft.UI;

namespace DroidNet.Documents;

/// <summary>
///     Optional document service contract that exposes the current document state for a given
///     window. UI presenters can use this to hydrate their initial view when they attach after
///     documents were already opened.
/// </summary>
public interface IDocumentServiceState
{
    /// <summary>
    ///     Returns the documents currently open for the specified window.
    /// </summary>
    /// <param name="windowId">The window identifier owning the documents.</param>
    /// <returns>
    ///     A stable snapshot of the document metadata. Implementations should return a copy of
    ///     their internal storage to avoid exposing mutable collections.
    /// </returns>
    public IReadOnlyList<IDocumentMetadata> GetOpenDocuments(WindowId windowId);

    /// <summary>
    ///     Returns the currently active document for the specified window, if any.
    /// </summary>
    /// <param name="windowId">The window identifier.</param>
    /// <returns>The active document identifier, or <see langword="null"/> when no document is active.</returns>
    public Guid? GetActiveDocumentId(WindowId windowId);
}
