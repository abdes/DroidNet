// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The services of the window that shows a document's tab, which its editor uses while the tab is there.
/// </summary>
/// <param name="Window">The window.</param>
/// <param name="Area">The window's document area.</param>
/// <param name="Commands">The window's commands.</param>
/// <param name="Placement">Where the window shows documents and panels.</param>
/// <param name="Reveal">Reveals assets in the window, or <see langword="null"/> when the window has nothing to reveal them in.</param>
public sealed record DocumentWindowServices(
    WindowId Window,
    IDocumentArea Area,
    ICommandService Commands,
    IPlacementService Placement,
    IAssetReveal? Reveal)
{
    /// <summary>Gets what shows problems in the window's Problems panel, or <see langword="null"/> when the window has none.</summary>
    public IProblemReveal? Problems { get; init; }
}
