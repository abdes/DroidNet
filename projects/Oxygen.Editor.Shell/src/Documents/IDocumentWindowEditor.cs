// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     An editor that uses the services of the window that shows its document's tab.
/// </summary>
/// <remarks>
///     The editor belongs to the project session and outlives any one window. It receives a window's services when its
///     tab is shown there and loses them when the tab leaves; it reads them when it acts and never keeps them past
///     <see cref="DetachWindow"/>.
/// </remarks>
public interface IDocumentWindowEditor
{
    /// <summary>Gives the editor the services of the window that now shows its tab.</summary>
    /// <param name="window">The window's services.</param>
    public void AttachWindow(DocumentWindowServices window);

    /// <summary>Takes back the services of the window its tab has left.</summary>
    public void DetachWindow();
}
