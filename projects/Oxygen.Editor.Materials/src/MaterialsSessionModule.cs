// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.Materials.Documents;

namespace Oxygen.Editor.Materials;

/// <summary>
///     Materials in a project session's scope: one set of edits for every window of the session, which follows the
///     session's asset relocations.
/// </summary>
public sealed class MaterialsSessionModule : IScopeModule<ISessionLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<MaterialEdits>(Reuse.Singleton);
    }
}
