// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell.Commands;

/// <summary>
/// The commands of one window. A command goes to the first part around the control the user works in that handles it,
/// then to the active document's editor, then to the parts that registered a handler, in the order they registered; Undo
/// and Redo that no part handles run the active document's history, or the global history when no document is active.
/// </summary>
/// <remarks>
/// A command takes no arguments: it acts on the window's active document and its selection, so it runs the same from a
/// key, a menu or a script that names it, with or without a control to start from.
/// </remarks>
/// <param name="area">
/// The window's document area: its active editor is asked after the parts, and its active document's history runs Undo and
/// Redo.
/// </param>
/// <param name="histories">The session's histories, whose global history Undo and Redo run when no document is active.</param>
/// <param name="contributions">The window's contributions, which name its commands.</param>
/// <param name="loggerFactory">The logger factory, if any.</param>
public sealed partial class CommandService(
    IDocumentArea area,
    IHistoryService histories,
    ContributionRegistry contributions,
    ILoggerFactory? loggerFactory = null) : ICommandService
{
    private readonly ILogger logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger<CommandService>();
    private readonly List<ICommandHandler> registered = [];

    /// <inheritdoc/>
    public IReadOnlyList<CommandDescriptor> Commands => [.. contributions.Commands];

    /// <inheritdoc/>
    public IDisposable Register(ICommandHandler handler)
    {
        ArgumentNullException.ThrowIfNull(handler);
        this.registered.Add(handler);
        return new Registration(this.registered, handler);
    }

    /// <inheritdoc/>
    public bool CanExecute(string commandId, DependencyObject? context)
    {
        ArgumentException.ThrowIfNullOrEmpty(commandId);
        return this.HandlerFor(commandId, context) is not null || this.HistoryFor(commandId) is not null;
    }

    /// <inheritdoc/>
    public async Task<bool> ExecuteAsync(string commandId, DependencyObject? context)
    {
        ArgumentException.ThrowIfNullOrEmpty(commandId);
        if (this.HandlerFor(commandId, context) is { } handler)
        {
            this.LogHandled(commandId, context, handler);
            await handler.ExecuteAsync(commandId).ConfigureAwait(true);
            return true;
        }

        if (this.HistoryFor(commandId) is { } history)
        {
            this.LogHandled(commandId, context, history);
            if (string.Equals(commandId, ShellCommands.Undo.Id, StringComparison.Ordinal))
            {
                await history.UndoAsync().ConfigureAwait(true);
            }
            else
            {
                await history.RedoAsync().ConfigureAwait(true);
            }

            return true;
        }

        this.LogUnhandled(commandId, context);
        return false;
    }

    // The view models of the views around the control, innermost first.
    private static IEnumerable<ICommandHandler> HandlersAround(DependencyObject? context)
    {
        for (var current = context; current is not null; current = VisualTreeHelper.GetParent(current))
        {
            if (current is IViewFor { ViewModel: ICommandHandler handler })
            {
                yield return handler;
            }
        }
    }

    private ICommandHandler? HandlerFor(string commandId, DependencyObject? context)
        => HandlersAround(context)
            .Append(area.ActiveEditor as ICommandHandler)
            .Concat([.. this.registered])
            .OfType<ICommandHandler>()
            .FirstOrDefault(handler => handler.CanExecute(commandId));

    // Undo and Redo that no part runs go to the history the user works in, when it has a change to undo or redo.
    private HistoryKeeper? HistoryFor(string commandId)
    {
        var history = area.ActiveDocument?.History ?? histories.GlobalHistory;
        return (string.Equals(commandId, ShellCommands.Undo.Id, StringComparison.Ordinal) && history.CanUndo)
            || (string.Equals(commandId, ShellCommands.Redo.Id, StringComparison.Ordinal) && history.CanRedo)
                ? history
                : null;
    }

    private sealed partial class Registration(List<ICommandHandler> handlers, ICommandHandler handler) : IDisposable
    {
        public void Dispose() => _ = handlers.Remove(handler);
    }
}
