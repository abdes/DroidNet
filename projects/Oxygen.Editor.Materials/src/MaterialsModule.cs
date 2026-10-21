// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.Materials.Documents;

namespace Oxygen.Editor.Materials;

/// <summary>
///     Materials in the editor scope: one document kind, its validation and one set of edits for every workspace
///     window.
/// </summary>
public sealed class MaterialsModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<MaterialValidation>(Reuse.Singleton);
        scope.Register<MaterialDocumentKind>(Reuse.Singleton);
        scope.Register<MaterialEdits>(Reuse.Singleton);
    }
}
