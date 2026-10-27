// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Models.Documents;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.ModelEditor;

/// <summary>
/// The editor of model documents: the Model Editor, whose tab stays in its window, since the engine shows one scene in
/// one window (model editor ME-1).
/// </summary>
/// <param name="kind">The model document kind.</param>
/// <param name="services">The session's services that the model editors use.</param>
public sealed class ModelEditorProvider(ModelDocumentKind kind, ModelEditorServices services) : IDocumentEditorProvider
{
    /// <inheritdoc/>
    public DocumentEditorDescriptor Editor { get; } = new(kind, tab => new ModelEditorViewModel(tab, services)) { CanTearOut = false };
}
