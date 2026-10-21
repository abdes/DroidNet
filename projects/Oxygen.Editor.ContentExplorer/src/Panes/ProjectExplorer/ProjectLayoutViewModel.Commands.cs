// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <content>
/// Paste while the user works in the sources tree pastes the Content Browser's clipboard into its focused folder. Cut
/// and Copy go on to the browser, which acts on the asset list's selection; the tree's folders are cut and copied from
/// their context menu.
/// </content>
public partial class ProjectLayoutViewModel : ICommandHandler
{
    /// <inheritdoc/>
    public bool CanExecute(string commandId) => string.Equals(commandId, ShellCommands.Paste.Id, StringComparison.Ordinal);

    /// <inheritdoc/>
    public Task ExecuteAsync(string commandId) => this.PasteIntoFocusedFolderAsync();
}
