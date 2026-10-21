// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Windowing;
using DroidNet.Composition;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Layout;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell;

/// <summary>
///     The Shell's services in a workspace window's scope: the window's document area, which lends the session's editors
///     the window's services, its commands, the placement of documents and panels, and the external code editor.
/// </summary>
/// <remarks>
///     The documents, their editors and the document service are the project session's; the window shows them.
/// </remarks>
public sealed class ShellWindowModule : IScopeModule<IWindowLayer>
{
    // The area follows the window's tabs, the editors shown here get the window's services, and the window's documents
    // take part in its close.
    private static readonly Type[] Activated =
    [
        typeof(IDocumentArea),
        typeof(DocumentWindowAttachments),
        typeof(IPlacementService),
        typeof(DocumentAreaViewModel),
        typeof(WindowDocumentsClose),
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
        scope.Register<WindowDocumentsClose>(Reuse.Singleton);
        scope.Register<ICommandService, CommandService>(Reuse.Singleton);
        scope.Register<IPlacementService, DockingPlacementService>(Reuse.Singleton);
        scope.Register<ICodeEditorService, CodeEditorService>(Reuse.Singleton);
    }

    // The window's document area over the session's documents and editors.
    private static void RegisterDocuments(IRegistrator scope)
    {
        scope.RegisterDelegate(
            resolver => new DocumentArea(
                resolver.Resolve<IDocumentTabs>(),
                resolver.Resolve<OpenDocuments>(),
                resolver.Resolve<DocumentEditors>(),
                resolver.Resolve<IDocumentService>(),
                resolver.Resolve<IDocumentFiles>(),
                resolver.Resolve<IOperationResultPublisher>(),
                resolver.Resolve<WindowId>(),
                resolver.Resolve<IWindowManagerService>().ActivateWindow,
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);
        scope.RegisterMapping<IDocumentArea, DocumentArea>();
        scope.Register<DocumentWindowAttachments>(Reuse.Singleton);
        scope.Register<DocumentAreaViewModel>(Reuse.Singleton);
        scope.Register<DocumentAreaView>(Reuse.Singleton);
    }
}
