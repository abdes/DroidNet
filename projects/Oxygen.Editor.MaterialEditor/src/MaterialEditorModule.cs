// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>The material editor: the editor of material documents, with the view that shows it.</summary>
public sealed class MaterialEditorModule : IFeatureModule
{
    /// <inheritdoc />
    public IReadOnlyList<PanelDescriptor> Panels { get; } = [];

    /// <inheritdoc />
    public IReadOnlyList<CommandDescriptor> Commands { get; } = [];

    /// <inheritdoc />
    public void RegisterWindowServices(IContainer window)
    {
        ArgumentNullException.ThrowIfNull(window);
        window.Register<IDocumentEditorProvider, MaterialEditorProvider>(Reuse.Singleton);
        window.Register<MaterialEditorView>(Reuse.Transient);
    }

    /// <inheritdoc />
    public void ActivateWindowServices(IResolver window) => ArgumentNullException.ThrowIfNull(window);
}
