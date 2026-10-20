// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell.Commands;

/// <summary>
/// The commands of one window. A command goes to the first part around the control the user works in that handles it,
/// then to the active document's editor; Undo and Redo that no part handles run the active document's history, or the
/// global history when no document is active.
/// </summary>
/// <param name="documents">The window's document service, whose active editor is asked after the parts.</param>
/// <param name="area">The window's document area, whose active document's history Undo and Redo run.</param>
/// <param name="histories">The session's histories, whose global history Undo and Redo run when no document is active.</param>
/// <param name="loggerFactory">The logger factory, if any.</param>
public sealed partial class CommandService(IDocumentService documents, IDocumentArea area, IHistoryService histories, ILoggerFactory? loggerFactory = null) : ICommandService
{
    private readonly ILogger logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger<CommandService>();

    /// <inheritdoc/>
    public IReadOnlyList<CommandDescriptor> Commands => ShellCommands.All;

    /// <inheritdoc/>
    public async Task<bool> ExecuteAsync(string commandId, DependencyObject? context)
    {
        ArgumentException.ThrowIfNullOrEmpty(commandId);
        foreach (var handler in HandlersAround(context).Append(documents.ActiveEditor as ICommandHandler).OfType<ICommandHandler>())
        {
            if (handler.CanExecute(commandId))
            {
                this.LogHandled(commandId, context, handler);
                await handler.ExecuteAsync(commandId).ConfigureAwait(true);
                return true;
            }
        }

        var history = area.ActiveDocument?.History ?? histories.GlobalHistory;
        if (string.Equals(commandId, ShellCommands.Undo.Id, StringComparison.Ordinal) && history.CanUndo)
        {
            this.LogHandled(commandId, context, history);
            await history.UndoAsync().ConfigureAwait(true);
            return true;
        }

        if (string.Equals(commandId, ShellCommands.Redo.Id, StringComparison.Ordinal) && history.CanRedo)
        {
            this.LogHandled(commandId, context, history);
            await history.RedoAsync().ConfigureAwait(true);
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
}
