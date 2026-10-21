// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell;

/// <summary>
///     The Shell's services in a project session's scope: the session's global history, which every window of the
///     session undoes and redoes, and the session's unsaved documents, which the cook, Play, relocations and the asset
///     feed ask about. Both end with the session.
/// </summary>
public sealed class ShellSessionModule : IScopeModule<ISessionLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IHistoryService, HistoryService>(Reuse.Singleton);
        scope.Register<IUnsavedDocuments, UnsavedDocuments>(Reuse.Singleton);
    }
}
