// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
///     The material editor in a window: the view that shows a material's editor, which the session creates. Every window
///     that shows materials registers it, the workspace window and the document windows a material's tab moves to.
/// </summary>
public sealed class MaterialEditorModule : IDocumentViewModule
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<MaterialEditorView>(Reuse.Transient);
    }
}
