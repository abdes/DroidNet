// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;

namespace Oxygen.Editor.SceneExplorer;

/// <summary>
/// A context-bound invocation adapter: binds a menu action to a frozen execute/eligibility pair so
/// the captured context (not the later selection) drives the invoked command. MenuItemData passes
/// itself to <see cref="ICommand"/>; the adapter ignores that parameter and forwards the frozen state.
/// </summary>
/// <param name="execute">The frozen invocation.</param>
/// <param name="canExecute">The frozen eligibility check.</param>
public sealed partial class SceneExplorerCommandAdapter(Action execute, Func<bool> canExecute) : ICommand
{
    private readonly Action execute = execute ?? throw new ArgumentNullException(nameof(execute));
    private readonly Func<bool> canExecute = canExecute ?? throw new ArgumentNullException(nameof(canExecute));

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
