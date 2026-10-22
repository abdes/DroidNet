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
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell;

/// <summary>
///     The Shell's services in a workspace window's scope: the window's document area, which lends the session's editors
///     the window's services, its commands, the panels and commands its features contribute, the placement of documents
///     and panels, and the external code editor.
/// </summary>
/// <remarks>
///     The documents, their editors and the document service are the project session's; the window shows them. The
///     window's scope ends when the window closes.
/// </remarks>
/// <param name="features">The features whose panels and commands the window shows.</param>
public sealed class ShellWindowModule(IReadOnlyList<IFeatureModule> features) : IScopeModule<IWindowLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    /// <remarks>
    ///     The area follows the window's tabs, the editors shown here get the window's services, and the window's
    ///     documents and its scope take part in its close.
    /// </remarks>
    public IReadOnlyList<Type> Activation { get; } = [.. DocumentWindowCore, typeof(IPlacementService)];

    /// <summary>
    ///     Gets the services of every window that shows documents that are created with the window: its document area and
    ///     its view model, which lend the editors the window's services, and its parts of the window's close.
    /// </summary>
    internal static IReadOnlyList<Type> DocumentWindowCore { get; } =
    [
        typeof(IDocumentArea),
        typeof(DocumentWindowAttachments),
        typeof(DocumentAreaViewModel),
        typeof(WindowDocumentsClose),
        typeof(WindowScopeClose),
    ];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        RegisterDocumentWindow(scope, features);
        scope.Register<DocumentAreaView>(Reuse.Singleton);
        scope.Register<IPlacementService, DockingPlacementService>(Reuse.Singleton);
        scope.Register<ICodeEditorService, CodeEditorService>(Reuse.Singleton);
    }

    /// <summary>
    ///     Registers the services of every window that shows documents: its document area over the session's documents
    ///     and editors, its view model and the window services its editors use while their tabs are there, its commands
    ///     with the panels and commands of its features, which the registry checks once, and its parts of the window's
    ///     close, which ends the window's scope.
    /// </summary>
    /// <param name="scope">The window's scope.</param>
    /// <param name="features">The features whose panels and commands the window shows; none for a document window.</param>
    internal static void RegisterDocumentWindow(IRegistrator scope, IReadOnlyList<IFeatureModule> features)
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
                resolver.Resolve<ILoggerFactory>()),
            Reuse.Singleton);
        scope.RegisterMapping<IDocumentArea, DocumentArea>();
        scope.Register<DocumentWindowAttachments>(Reuse.Singleton);
        scope.Register<DocumentAreaViewModel>(Reuse.Singleton);
        scope.RegisterDelegate(
            _ => new ContributionRegistry(
                [.. features.SelectMany(static feature => feature.Panels)],
                [.. features.SelectMany(static feature => feature.Commands)]),
            Reuse.Singleton);
        scope.Register<ICommandService, CommandService>(Reuse.Singleton);
        scope.Register<WindowDocumentsClose>(Reuse.Singleton);
        scope.Register<WindowScopeClose>(Reuse.Singleton);
    }
}
