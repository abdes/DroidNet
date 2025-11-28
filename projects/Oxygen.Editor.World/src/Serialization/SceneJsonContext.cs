// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// JSON serialization context for scene data using source generators.
/// </summary>
[JsonSerializable(typeof(SceneData))]
[JsonSerializable(typeof(SceneNodeData))]
[JsonSerializable(typeof(ComponentData))]
[JsonSerializable(typeof(TransformData))]
[JsonSourceGenerationOptions(
    WriteIndented = true,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    Converters = new[] { typeof(Vector3JsonConverter), typeof(QuaternionJsonConverter) })]
public partial class SceneJsonContext : JsonSerializerContext
{
}
