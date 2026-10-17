// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Names the scene document whose preview changed.</summary>
/// <param name="document">The scene document.</param>
public sealed class ScenePreviewEventArgs(DocumentId document) : EventArgs
{
    /// <summary>Gets the scene document.</summary>
    public DocumentId Document { get; } = document;
}
