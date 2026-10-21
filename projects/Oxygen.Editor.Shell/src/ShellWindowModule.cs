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
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Layout;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Shell;

/// <summary>
///     The Shell's services in a workspace window's scope: the window's document area and its document service, its
///     commands, the placement of documents and panels, and the external code editor.
/// </summary>
/// <remarks>
///     The window's open documents come from the window's composition, with the document kinds it opens; each kind's
///     editor comes from the feature that edits it.
/// </remarks>
public sealed class ShellWindowModule : IScopeModule<IWindowLayer>
{
    // The document service follows the window's tabs, and the window's documents take part in its close.
    private static readonly Type[] Activated =
    [
        typeof(IDocumentService),
        typeof(IDocumentArea),
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
    }
}
