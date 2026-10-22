// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;
using DroidNet.Documents;
using Microsoft.UI;

namespace DroidNet.Aura.Documents;

/// <summary>
///     Creates the window that receives a document whose tab was torn out of a window and dropped outside every tab
///     strip. The application implements it: which window a document gets, and what else that window shows, is its
///     own.
/// </summary>
/// <remarks>
///     Aura attaches the document to the window once it is created, through <see cref="IDocumentTabSource" />. A window
///     that is not created leaves the document where it came from.
/// </remarks>
public interface IDocumentWindowFactory
{
    /// <summary>Creates a window whose tabs can receive the document, at the point where its tab was dropped.</summary>
    /// <param name="document">The document whose tab was torn out.</param>
    /// <param name="position">The point of the screen, in physical pixels, where the tab was dropped.</param>
    /// <returns>The new window, or <see langword="null" /> when none was created.</returns>
    public Task<WindowId?> CreateWindowAsync(IDocumentMetadata document, SpatialPoint<PhysicalScreenSpace> position);
}
