// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DroidNet.Mvvm;
using DryIoc;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Shell;

/// <summary>
///     The Shell's services in a document window's scope, the window a document's tab is torn out into: its document
///     area, which lends the session's editors the window's services, its commands, the placement of its documents, and
///     its part of the window's close.
/// </summary>
/// <remarks>
///     <para>
///     A document window has tabs and no docks: its commands are the Shell's own, and it shows no panels. It locates the
///     views of its editors in its own scope, where the features' <see cref="IDocumentViewModule" />s register them.
///     </para>
///     <para>
///     The window closes without its session: once the close is certain its documents close, asking about their
///     unsaved changes first, then its scope is disposed. It closes by itself once its last tab has left it.
///     </para>
/// </remarks>
public sealed class ShellDocumentWindowModule : IScopeModule<IWindowLayer>
{
    // The editor scope locates the views of its own windows' pages.
    private static readonly Type[] Shadowed = [typeof(IViewLocator)];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => Shadowed;

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation { get; } = [.. ShellWindowModule.DocumentWindowCore, typeof(EmptyDocumentWindowClose)];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        ShellWindowModule.RegisterDocumentWindow(scope, features: []);
        scope.Register<IPlacementService, DocumentWindowPlacement>(Reuse.Singleton);
        scope.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        scope.Register<EmptyDocumentWindowClose>(Reuse.Singleton);
    }
}
