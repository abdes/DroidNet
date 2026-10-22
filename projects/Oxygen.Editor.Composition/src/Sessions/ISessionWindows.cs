// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using Microsoft.UI;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Sessions;

/// <summary>
///     The windows of the open project session: each one opens with its scope under the session's, records in Aura's
///     window manager that it is the session's, and shows the session's tabs; and their documents close together.
/// </summary>
public interface ISessionWindows
{
    /// <summary>
    ///     Opens a window of the session: creates its scope, with the window's id registered in it, which the window's
    ///     close disposes, and the session's close if the window is still open; records the window as the session's; and
    ///     binds the window's tab strip to the session's tabs.
    /// </summary>
    /// <param name="window">The window.</param>
    /// <param name="modules">The window's modules, in registration and activation order.</param>
    /// <returns>The window's scope.</returns>
    /// <exception cref="InvalidOperationException">No session is open, or Aura's window manager does not know the window.</exception>
    public ScopeHandle OpenWindow(WindowId window, IEnumerable<IScopeModule<IWindowLayer>> modules);

    /// <summary>
    ///     Prepares closing the documents of the session's windows, such as before the project closes, asking about the
    ///     unsaved ones, without closing them until the caller commits.
    /// </summary>
    /// <param name="except">A window whose documents are left out, because its own close prepares them.</param>
    /// <returns>
    ///     The transaction, which closes nothing when no session is open, or <see langword="null" /> when the close was
    ///     refused.
    /// </returns>
    public Task<DocumentCloseTransaction?> PrepareCloseDocumentsAsync(WindowId? except = null);
}
