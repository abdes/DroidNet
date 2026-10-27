// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.SceneEditor.Settings;

/// <summary>
///     The scene editor in a project session's scope: the editor of scene documents, which every window of the session
///     reaches. It ends with the session.
/// </summary>
public sealed class SceneEditorSessionModule : IScopeModule<ISessionLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // The scene editors belong to the session; each window that shows one lends it the window's services.
        scope.Register<IDocumentEditorProvider, SceneEditorProvider>(Reuse.Singleton);
        scope.Register<SceneEditorServices>(Reuse.Singleton);
    }
}
