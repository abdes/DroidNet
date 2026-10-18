// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.Scenes.Layout;

/// <summary>JSON serialization context for scene files and their explorer layout, with the scene format's options.</summary>
[JsonSerializable(typeof(SceneFileData))]
[JsonSerializable(typeof(ExplorerEntryData))]
[JsonSourceGenerationOptions(
    WriteIndented = true,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    Converters = [typeof(Vector3JsonConverter), typeof(Vector4JsonConverter), typeof(QuaternionJsonConverter)])]
public partial class SceneFileJsonContext : JsonSerializerContext;
