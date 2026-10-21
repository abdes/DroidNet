// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Composition;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Layout;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Shell;

/// <summary>
///     The Shell's services in a workspace window's scope: the window's document area and its document service, the
///     prompts and confirmations that act on the window, its commands, the placement of documents and panels, the
///     project's global history and the external code editor.
/// </summary>
/// <remarks>
///     The window's open documents come from the window's composition, with the document kinds it opens; each kind's
///     editor comes from the feature that edits it.
/// </remarks>
public sealed class ShellWindowModule : IScopeModule<IWindowLayer>
{
    // The document service follows the window's tabs, and the window's close asks about its documents.
    private static readonly Type[] Activated =
    [
        typeof(IDocumentService),
        typeof(IDocumentArea),
        typeof(IPlacementService),
        typeof(DocumentAreaViewModel),
        typeof(WindowCloseCoordinator),
    ];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => Activated;

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        RegisterDocuments(scope);
        RegisterWindowDocumentServices(scope);
        scope.Register<ICommandService, CommandService>(Reuse.Singleton);
        scope.Register<IPlacementService, DockingPlacementService>(Reuse.Singleton);

        // The project's global history, which ends with the workspace.
        scope.Register<IHistoryService, HistoryService>(Reuse.Singleton);
        scope.Register<ICodeEditorService, CodeEditorService>(Reuse.Singleton);
    }

    // Each kind's editor comes from the module that edits it.
    private static void RegisterDocuments(IRegistrator scope)
    {
        scope.RegisterDelegate(
            resolver => new DocumentArea(
                resolver.Resolve<IDocumentTabs>(),
                resolver.Resolve<OpenDocuments>(),
                resolver.Resolve<IEnumerable<IDocumentEditorProvider>>().Select(static provider => provider.Editor),
                resolver.Resolve<WindowId>(),
                resolver.Resolve<IWindowManagerService>().ActivateWindow,
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);
        scope.RegisterMapping<IDocumentArea, DocumentArea>();
        scope.Register<DocumentService>(Reuse.Singleton);
        scope.RegisterMapping<IDocumentService, DocumentService>();
        scope.Register<DocumentAreaViewModel>(Reuse.Singleton);
        scope.Register<DocumentAreaView>(Reuse.Singleton);

        // The cook, Play, relocations and the asset feed ask which documents are unsaved.
        scope.Register<IUnsavedDocuments, UnsavedDocuments>(Reuse.Singleton);
    }

    // The prompts, confirmations and input of the window, which act on it without features naming it.
    private static void RegisterWindowDocumentServices(IRegistrator scope)
    {
        scope.RegisterDelegate<IDocumentClosePrompt>(
            resolver => new DocumentClosePrompt(resolver.Resolve<IDialogService>(), resolver.Resolve<IWindowManagerService>(), resolver.Resolve<WindowId>()),
            Reuse.Singleton);
        scope.RegisterDelegate<IDocumentConfirmations>(
            resolver => new DocumentConfirmations(resolver.Resolve<IDialogService>(), resolver.Resolve<IWindowManagerService>(), resolver.Resolve<WindowId>()),
            Reuse.Singleton);
        scope.RegisterDelegate<IDocumentInputCommitter>(
            resolver => new DocumentInputCommitter(resolver.Resolve<IWindowManagerService>(), resolver.Resolve<WindowId>()),
            Reuse.Singleton);
        scope.RegisterDelegate(
            resolver => new WindowCloseCoordinator(resolver.Resolve<IWindowManagerService>(), resolver.Resolve<IDocumentTabs>(), resolver.Resolve<WindowId>()),
            Reuse.Singleton);
    }
}
