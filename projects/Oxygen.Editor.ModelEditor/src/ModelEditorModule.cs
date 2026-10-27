// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.ModelEditor;

/// <summary>The Model Editor's view in a window's scope.</summary>
public sealed class ModelEditorModule : IDocumentViewModule
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<ModelEditorView>(Reuse.Transient);
    }
}
