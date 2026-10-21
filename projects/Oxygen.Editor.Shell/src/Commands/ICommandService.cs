// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace Oxygen.Editor.Shell.Commands;

/// <summary>
/// Runs the commands of a window: from the part the user works in outwards, then the active document's editor, then the
/// history the user works in.
/// </summary>
public interface ICommandService
{
    /// <summary>Gets the commands the window binds to keys.</summary>
    public IReadOnlyList<CommandDescriptor> Commands { get; }

    /// <summary>Registers a part's handler, which the window asks after the focused parts and the active document's editor.</summary>
    /// <remarks>A part that runs commands wherever the user works, such as the Scene Explorer's Rename, registers while it lives.</remarks>
    /// <param name="handler">The handler.</param>
    /// <returns>The registration; disposing it removes the handler.</returns>
    public IDisposable Register(ICommandHandler handler);

    /// <summary>Gets a value indicating whether something runs a command now.</summary>
    /// <param name="commandId">The command's identity.</param>
    /// <param name="context">The control the user works in, whose parts are asked first; <see langword="null"/> when there is none.</param>
    /// <returns><see langword="true"/> when <see cref="ExecuteAsync"/> would run the command.</returns>
    public bool CanExecute(string commandId, DependencyObject? context);

    /// <summary>Runs a command where the user works.</summary>
    /// <param name="commandId">The command's identity.</param>
    /// <param name="context">
    /// The control the user last pressed or focused, or a view around it, whose parts are asked first; <see langword="null"/>
    /// when there is none.
    /// </param>
    /// <returns><see langword="true"/> when something ran the command.</returns>
    public Task<bool> ExecuteAsync(string commandId, DependencyObject? context);
}
