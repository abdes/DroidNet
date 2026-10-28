// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Incremental;

namespace Oxygen.Editor.ContentPipeline.Testing;

/// <summary>The importer revisions of an ImportTool, for tests that run no native tool.</summary>
public static class ImporterRevisionsFixture
{
    /// <summary>Gets every importer the ImportTool reports, each at revision 1.</summary>
    public static ImporterRevisions Current { get; } = new(new[]
    {
        "gltf", "fbx", "texture", "buffer_container", "material_descriptor", "physics_material_descriptor",
        "collision_shape_descriptor", "geometry_descriptor", "scene_descriptor", "script", "script_sidecar",
        "physics_sidecar", "input",
    }.ToImmutableDictionary(static name => name, static _ => 1U, StringComparer.Ordinal));

    /// <summary>Gets <see cref="Current"/> as the ImportTool's <c>versions --json</c> prints it.</summary>
    public static string VersionsJson { get; } = JsonSerializer.Serialize(new
    {
        program = "Oxygen.Cooker.ImportTool",
        version = "test",
        importers = Current.Revisions,
    });
}
