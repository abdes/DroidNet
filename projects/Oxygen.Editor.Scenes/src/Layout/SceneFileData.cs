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
    public SceneFileData()
    {
    }

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
