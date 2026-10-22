// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.SceneEditor;

/// <summary>
/// The scene editor, contributed for scene documents. A window shows one scene: opening another one replaces it, and the
/// scene's tab does not close on its own.
/// </summary>
/// <param name="kind">The scene document kind.</param>
/// <param name="services">The project session's services, which every scene editor of the session uses.</param>
public sealed class SceneEditorProvider(SceneDocumentKind kind, SceneEditorServices services) : IDocumentEditorProvider
{
    /// <inheritdoc/>
    public DocumentEditorDescriptor Editor { get; } = new(kind, tab => new SceneEditorViewModel(tab, services)) { IsClosable = false, IsOnePerArea = true, IsPinned = true, IsPinLocked = true };
}
