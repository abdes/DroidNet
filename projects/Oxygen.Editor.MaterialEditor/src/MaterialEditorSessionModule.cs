// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
///     The material editor in a project session's scope: the editor of material documents, which the session creates for
///     every material that gets a tab in any of its windows.
/// </summary>
public sealed class MaterialEditorSessionModule : IScopeModule<ISessionLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IDocumentEditorProvider, MaterialEditorProvider>(Reuse.Singleton);
    }
}
