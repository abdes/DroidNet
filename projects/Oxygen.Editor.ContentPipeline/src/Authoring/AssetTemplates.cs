// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using Oxygen.Managed.Assets.Authoring.Materials;

namespace Oxygen.Editor.ContentPipeline.Authoring;

/// <summary>
/// The text New Script, New Scene and New Material write; New Input File writes <c>InputTemplates.New</c>.
/// </summary>
public static class AssetTemplates
{
    /// <summary>The template of a new script: one example parameter and the two most common hooks.</summary>
    public const string Script = """
        local Script = oxygen.script {
          params = {
            speed = oxygen.param.float { default = 1, min = 0, unit = "m/s",
              description = "How fast the node moves" },
          },
        }

        function Script:on_start()
        end

        function Script:on_gameplay(dt)
        end

        return Script

        """;

    /// <summary>Makes the text of a new, empty scene, with an identity of its own.</summary>
    /// <param name="sceneName">The scene's name, which is its file name.</param>
    /// <returns>The scene file's text.</returns>
    public static string Scene(string sceneName)
        => $$"""
            {
              "Id": "{{Guid.NewGuid():D}}",
              "Name": "{{sceneName}}",
              "RootNodes": []
            }

            """;

    /// <summary>Makes a new material's source: white, opaque and single-sided, named after its file.</summary>
    /// <param name="materialName">The material's name, which is its file's name.</param>
    /// <returns>The material's descriptor JSON, as the material writer writes it.</returns>
    public static string Material(string materialName)
    {
        var source = new MaterialSource(
            name: materialName,
            pbrMetallicRoughness: new MaterialPbrMetallicRoughness(
                baseColorR: 1.0f,
                baseColorG: 1.0f,
                baseColorB: 1.0f,
                baseColorA: 1.0f,
                metallicFactor: 0.0f,
                roughnessFactor: 0.5f),
            alphaMode: MaterialAlphaMode.Opaque,
            alphaCutoff: 0.5f,
            doubleSided: false);
        using var memory = new MemoryStream();
        MaterialSourceWriter.Write(memory, source);
        return Encoding.UTF8.GetString(memory.ToArray());
    }
}
