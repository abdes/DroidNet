// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DroidNet.Documents;
using DryIoc;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell;

/// <summary>
///     The Shell's services in the editor scope: the tabs of every window, which Aura draws. Each window's document
///     service makes its close decisions.
/// </summary>
public sealed class ShellModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<DocumentTabs>(Reuse.Singleton);
        scope.RegisterMapping<IDocumentTabSource, DocumentTabs>();
        scope.RegisterMapping<IDocumentTabs, DocumentTabs>();
    }
}
