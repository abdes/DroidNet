// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models;

/// <summary>One model of an import session: a <c>gltf</c> or <c>fbx</c> job of its import manifest.</summary>
/// <param name="Source">The model's primary source file, a full path.</param>
/// <param name="VirtualPath">The model's identity, which gives its folder and name.</param>
/// <param name="Settings">The model import settings the job sets, its <c>name</c> included, in the job's own keys.</param>
public sealed record ModelImportSessionJob(string Source, AssetPath VirtualPath, JsonElement Settings)
{
    /// <summary>Gets the import's name: the job's <c>name</c>, or the source file's name without its extension.</summary>
    public string Name
        => this.Settings.ValueKind == JsonValueKind.Object
            && this.Settings.TryGetProperty(ModelImportSettingCatalog.NameKey, out var name)
            && name.ValueKind == JsonValueKind.String && name.GetString() is { Length: > 0 } text
                ? text
                : Path.GetFileNameWithoutExtension(this.Source);
}
