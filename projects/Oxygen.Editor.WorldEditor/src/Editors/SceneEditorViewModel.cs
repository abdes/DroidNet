// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
/// ViewModel for the Scene Editor.
/// </summary>
public partial class SceneEditorViewModel(SceneDocumentMetadata metadata) : ObservableObject
{
    [ObservableProperty]
    public partial SceneDocumentMetadata Metadata { get; set; } = metadata;
}
