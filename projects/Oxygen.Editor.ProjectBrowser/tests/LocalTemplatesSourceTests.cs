// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Assets.Import.Materials;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.World.Serialization;
using Testably.Abstractions;

namespace Oxygen.Editor.ProjectBrowser.Tests;

[TestClass]
[TestCategory(nameof(LocalTemplatesSource))]
public class LocalTemplatesSourceTests
{
    [TestMethod]
    public async Task LoadTemplateAsync_ShouldLoadV01TemplateDescriptor()
    {
        var root = CreateTempTemplate();
        try
        {
            var source = new LocalTemplatesSource(new RealFileSystem());

            var template = await source.LoadTemplateAsync(new Uri(Path.Combine(root, "Template.json"))).ConfigureAwait(false);

            Assert.AreEqual(1, template.SchemaVersion);
            Assert.AreEqual("Games/Blank", template.TemplateId);
            Assert.AreEqual("Blank", template.Name);
            Assert.AreEqual(".", template.SourceFolder);
            Assert.IsTrue(Path.IsPathRooted(template.Icon));
            Assert.AreEqual(1, template.PreviewImages.Count);
            Assert.IsTrue(Path.IsPathRooted(template.PreviewImages[0]));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [TestMethod]
    public async Task LoadTemplateAsync_ShouldRejectPayloadProjectManifest()
    {
        var root = CreateTempTemplate();
        try
        {
            await File.WriteAllTextAsync(Path.Combine(root, "Project.oxy"), "{}").ConfigureAwait(false);
            var source = new LocalTemplatesSource(new RealFileSystem());

            try
            {
                _ = await source.LoadTemplateAsync(new Uri(Path.Combine(root, "Template.json"))).ConfigureAwait(false);
                Assert.Fail("Template loading should reject payload Project.oxy files.");
            }
            catch (TemplateLoadingException)
            {
            }
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [TestMethod]
    public async Task BuiltInStarterScenes_ShouldDeserializeAsSceneData()
    {
        var templatesRoot = FindBuiltInTemplatesRoot();
        var starterScenes = Directory.GetFiles(templatesRoot, "*.oscene.json", SearchOption.AllDirectories);

        Assert.IsTrue(starterScenes.Length > 0);
        foreach (var scenePath in starterScenes)
        {
            await using var stream = File.OpenRead(scenePath);
            var scene = await JsonSerializer.DeserializeAsync(stream, SceneJsonContext.Default.SceneData)
                .ConfigureAwait(false);

            Assert.IsNotNull(scene, scenePath);
        }
    }

    [TestMethod]
    public void BuiltInStarterMaterials_ShouldDeserializeAsMaterialSources()
    {
        var templatesRoot = FindBuiltInTemplatesRoot();
        var starterMaterials = Directory.GetFiles(templatesRoot, "*.omat.json", SearchOption.AllDirectories);

        Assert.IsTrue(starterMaterials.Length > 0);
        foreach (var materialPath in starterMaterials)
        {
            var material = MaterialSourceReader.Read(File.ReadAllBytes(materialPath));

            Assert.AreEqual("oxygen.material.v1", material.Schema, materialPath);
        }
    }

    private static string FindBuiltInTemplatesRoot()
    {
        foreach (var start in new[] { Environment.CurrentDirectory, AppContext.BaseDirectory })
        {
            var directory = new DirectoryInfo(start);
            while (directory is not null)
            {
                var candidate = Path.Combine(
                    directory.FullName,
                    "projects",
                    "Oxygen.Editor.ProjectBrowser",
                    "src",
                    "Assets",
                    "Templates");
                if (Directory.Exists(candidate))
                {
                    return candidate;
                }

                directory = directory.Parent;
            }
        }

        Assert.Fail("Could not locate built-in Project Browser templates.");
        return string.Empty;
    }

    private static string CreateTempTemplate()
    {
        var root = Path.Combine(Path.GetTempPath(), "OxygenTemplateTests", Guid.NewGuid().ToString("N"));
        const string TemplateJson =
            """
            {
              "SchemaVersion": 1,
              "TemplateId": "Games/Blank",
              "Name": "Blank",
              "DisplayName": "Blank",
              "Description": "Blank template",
              "Icon": "Media/Icon.png",
              "Preview": "Media/Preview.png",
              "SourceFolder": ".",
              "Category": "C44E7604-B265-40D8-9442-11A01ECE334C",
              "AuthoringMounts": [
                {
                  "Name": "Content",
                  "RelativePath": "Content"
                }
              ],
              "LocalFolderMounts": [],
              "StarterScene": {
                "AssetUri": "asset:///Content/Scenes/Main.oscene.json",
                "RelativePath": "Content/Scenes/Main.oscene.json",
                "OpenOnCreate": true
              },
              "StarterContent": [
                {
                  "AssetUri": "asset:///Content/Materials/Default.omat.json",
                  "RelativePath": "Content/Materials/Default.omat.json",
                  "Kind": "Material"
                }
              ],
              "PreviewImages": [
                "Media/Preview.png"
              ]
            }
            """;

        foreach (var folder in new[]
                 {
                     "Content",
                     "Content/Scenes",
                     "Content/Materials",
                     "Content/Geometry",
                     "Content/Textures",
                     "Content/Audio",
                     "Content/Video",
                     "Content/Scripts",
                     "Content/Prefabs",
                     "Content/Animations",
                     "Content/SourceMedia",
                     "Content/SourceMedia/Images",
                     "Content/SourceMedia/Audio",
                     "Content/SourceMedia/Video",
                     "Content/SourceMedia/DCC",
                     "Config",
                     "Media",
                 })
        {
            Directory.CreateDirectory(Path.Combine(root, folder));
        }

        File.WriteAllText(Path.Combine(root, "Media", "Icon.png"), "icon");
        File.WriteAllText(Path.Combine(root, "Media", "Preview.png"), "preview");
        File.WriteAllText(
            Path.Combine(root, "Content", "Scenes", "Main.oscene.json"),
            """
            {
              "Id": "bc749567-e334-418d-962f-1d84defe30a9",
              "Name": "Main",
              "RootNodes": []
            }
            """);
        File.WriteAllText(
            Path.Combine(root, "Content", "Materials", "Default.omat.json"),
            """
            {
              "Schema": "oxygen.material.v1",
              "Type": "PBR",
              "Name": "Default",
              "PbrMetallicRoughness": {
                "BaseColorFactor": [1, 1, 1, 1],
                "MetallicFactor": 0,
                "RoughnessFactor": 0.5
              },
              "AlphaMode": "OPAQUE",
              "DoubleSided": false
            }
            """);
        File.WriteAllText(Path.Combine(root, "Template.json"), TemplateJson);
        return root;
    }
}
