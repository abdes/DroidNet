// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell;

/// <summary>
///     The Shell's services in a project session's scope: the session's global history, which every window of the
///     session undoes and redoes; the tabs of the session's windows, which Aura draws, with their close decisions and
///     the input commits of the windows that show them; the document service, which owns the editors of the session's
///     documents in every window; the editor of each document kind, which the features contribute; and the session's
///     unsaved documents, which the cook, Play, relocations and the asset feed ask about. All end with the session.
/// </summary>
public sealed class ShellSessionModule : IScopeModule<ISessionLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    /// <remarks>The document service follows the tabs from the start, so every document's tab gets its editor.</remarks>
    public IReadOnlyList<Type> Activation { get; } = [typeof(IDocumentService)];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IHistoryService, HistoryService>(Reuse.Singleton);
        scope.Register<IUnsavedDocuments, UnsavedDocuments>(Reuse.Singleton);

        // The session's own editor providers, which the features register in their session modules.
        scope.RegisterDelegate(
            resolver => new DocumentEditors(
                resolver.Resolve<OpenDocuments>(),
                resolver.ResolveMany<IDocumentEditorProvider>().Select(static provider => provider.Editor)),
            Reuse.Singleton);
        // The tabs of the session's windows, which every window of the session is bound to, and their close decisions.
        scope.Register<DocumentCloseCoordinator>(Reuse.Singleton);
        scope.Register<DocumentTabs>(Reuse.Singleton);
        scope.RegisterMapping<IDocumentTabs, DocumentTabs>();
        scope.Register<IDocumentInputCommitter, DocumentInputCommitter>(Reuse.Singleton);
        scope.Register<DocumentService>(Reuse.Singleton);
        scope.RegisterMapping<IDocumentService, DocumentService>();
    }
}
