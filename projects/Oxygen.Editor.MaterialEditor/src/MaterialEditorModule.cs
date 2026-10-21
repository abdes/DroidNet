// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>The material editor in a window: the view that shows a material's editor, which the session creates.</summary>
public sealed class MaterialEditorModule : IFeatureModule
{
    /// <inheritdoc />
    public IReadOnlyList<PanelDescriptor> Panels { get; } = [];

    /// <inheritdoc />
    public IReadOnlyList<CommandDescriptor> Commands { get; } = [];

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
