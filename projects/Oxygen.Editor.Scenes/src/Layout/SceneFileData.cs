// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json.Serialization;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Layout;

/// <summary>What a scene file records: the scene, and the explorer layout the editor keeps beside it.</summary>
/// <remarks>
/// The layout is editor-only: the cook and the engine read the file as <see cref="SceneData"/> and never see it.
/// </remarks>
public sealed record SceneFileData : SceneData
{
    /// <summary>Initializes a new instance of the <see cref="SceneFileData"/> record.</summary>
    public SceneFileData()
    {
    }

    /// <summary>Initializes a new instance of the <see cref="SceneFileData"/> record from a scene and its layout.</summary>
    /// <param name="scene">The scene's data.</param>
    /// <param name="explorerLayout">The scene's explorer layout, or <see langword="null"/> for none.</param>
    [SetsRequiredMembers]
    public SceneFileData(SceneData scene, IList<ExplorerEntryData>? explorerLayout)
        : base(scene)
    {
        this.ExplorerLayout = explorerLayout;
    }

    /// <summary>Gets the scene's explorer layout: its folders and node entries.</summary>
    [JsonPropertyOrder(1)]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    public IList<ExplorerEntryData>? ExplorerLayout { get; init; }
}
