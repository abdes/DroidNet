// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>
/// A context-bound invocation adapter: binds a menu action to a frozen execute/eligibility pair so
/// the captured context (not the later selection) drives the invoked command. MenuItemData passes
/// itself to <see cref="ICommand"/>; the adapter ignores that parameter and forwards the frozen state.
/// </summary>
public sealed class SceneExplorerCommandAdapter : ICommand
{
    private readonly Action execute;
    private readonly Func<bool> canExecute;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneExplorerCommandAdapter"/> class.
    /// </summary>
    /// <param name="execute">The frozen invocation.</param>
    /// <param name="canExecute">The frozen eligibility check.</param>
    public SceneExplorerCommandAdapter(Action execute, Func<bool> canExecute)
    {
        this.execute = execute ?? throw new ArgumentNullException(nameof(execute));
        this.canExecute = canExecute ?? throw new ArgumentNullException(nameof(canExecute));
    }

    /// <inheritdoc />
    // ICommand requires the event, but the adapter's eligibility is frozen at construction and
    // never changes, so CanExecuteChanged is intentionally never raised.
#pragma warning disable CS0067
    public event EventHandler? CanExecuteChanged;
#pragma warning restore CS0067

    /// <inheritdoc />
    public bool CanExecute(object? parameter) => this.canExecute();

    /// <inheritdoc />
    public void Execute(object? parameter) => this.execute();
}
