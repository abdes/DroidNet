// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Documents;
using DroidNet.Composition;
using DryIoc;

namespace Oxygen.Editor.DocumentWindows;

/// <summary>
///     The document windows in the editor scope: the page of a document window, and the factory through which Aura's
///     document transfers open one for a torn-out tab. Each window registers its own services in its own scope.
/// </summary>
internal sealed class DocumentWindowsModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<DocumentWindowViewModel>(Reuse.Transient);
        scope.Register<DocumentWindowView>(Reuse.Transient);
        scope.Register<IDocumentWindowFactory, DocumentWindowFactory>(Reuse.Singleton);
    }
}
