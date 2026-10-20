// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Commands;

/// <summary>
/// A part that handles commands itself. The view model of a part's view implements it; the command service asks the
/// parts around the focused control first, then the active document's editor.
/// </summary>
public interface ICommandHandler
{
    /// <summary>Gets a value indicating whether the part runs a command now.</summary>
    /// <param name="commandId">The command's identity.</param>
    /// <returns><see langword="true"/> when the part runs the command; otherwise the command goes on to the next handler.</returns>
    public bool CanExecute(string commandId);

    /// <summary>Runs a command that <see cref="CanExecute"/> accepted.</summary>
    /// <param name="commandId">The command's identity.</param>
    /// <returns>A task that completes when the command has run.</returns>
    public Task ExecuteAsync(string commandId);
}
