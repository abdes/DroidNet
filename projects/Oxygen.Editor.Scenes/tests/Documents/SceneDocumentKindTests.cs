// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using AwesomeAssertions;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Scenes.Tests.Documents;

/// <summary>
/// A scene file the scene model rejects fails to decode with its reason, instead of throwing out of the open.
/// </summary>
[TestClass]
public sealed class SceneDocumentKindTests
{
    private const string NodeWithoutFlags = """
        { "Id": "00000000-0000-4000-8000-000000000101", "Name": "Main", "RootNodes": [ { "Id": "00000000-0000-4000-8000-000000000201",
          "Components": [ { "$type": "Metadata", "Name": "Box" }, { "$type": "Transform", "Name": "Transform" },
            { "$type": "Hierarchy", "Name": "Hierarchy" } ] } ] }
        """;

    private const string BlankNodeName = """
        { "Id": "00000000-0000-4000-8000-000000000101", "Name": "Main", "RootNodes": [ { "Id": "00000000-0000-4000-8000-000000000201",
          "Components": [ { "$type": "Metadata", "Name": " " }, { "$type": "Transform", "Name": "Transform" },
            { "$type": "NodeFlags", "Name": "Rendering" }, { "$type": "Hierarchy", "Name": "Hierarchy" } ] } ] }
        """;

    private const string NotJson = "{ \"Id\": ";

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    [DataRow(NodeWithoutFlags, DisplayName = "A node without its node flags")]
    [DataRow(BlankNodeName, DisplayName = "A node with a blank name")]
    [DataRow(NotJson, DisplayName = "Malformed JSON")]
    public async Task RejectedScene_FailsToDecodeWithItsReason(string json)
    {
        var project = TestProject.Create(new MockFileSystem());
        var kind = new SceneDocumentKind(interaction: null, new FakeAssetRedirects());
        var file = new DocumentFile(
            project.PathOf($"/Content/Main{AssetFileTypes.SceneDescriptor}"),
            AssetPath.Parse($"/Content/Main{AssetFileTypes.Scene}"));

        var result = await kind.DecodeAsync(
            file,
            Encoding.UTF8.GetBytes(json),
            new DocumentDecodeContext(DocumentId.New(), new HistoryKeeper(file)),
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Content.Should().BeNull();
        _ = result.Problem.Should().NotBeNullOrWhiteSpace();
    }

    [TestMethod]
    [DataRow("../Models/Car.glb", "/Content/Models/Car.glb", DisplayName = "In its mount")]
    [DataRow("/Vendor/Models/Car.glb", "/Vendor/Models/Car.glb", DisplayName = "From another mount")]
    public async Task AScene_ReadsItsReferencesFromItsIdentity(string written, string asset)
    {
        var result = await DecodeInstanceAsync(written, this.TestContext.CancellationToken).ConfigureAwait(false);

        var content = result.Content.Should().BeOfType<SceneContent>().Subject;
        using var lifetime = content;
        _ = content.Scene.RootNodes[0].Components.OfType<SceneInstanceComponent>().Single().Source.Should().Be(AssetPath.Parse(asset));
    }

    [TestMethod]
    [DataRow("asset:///Content/Models/Car.glb", DisplayName = "An asset URI")]
    [DataRow("/Content/Models/Car.glb", DisplayName = "Its own mount by virtual path")]
    public async Task AScene_WritingARefusedReferenceFailsToOpenNamingIt(string written)
    {
        var result = await DecodeInstanceAsync(written, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Content.Should().BeNull();
        _ = result.Problem.Should().Contain("Source").And.Contain(written).And.Contain("vpath.");
    }

    private static Task<DocumentDecodeResult> DecodeInstanceAsync(string written, CancellationToken cancellationToken)
    {
        var project = TestProject.Create(new MockFileSystem());
        var file = new DocumentFile(
            project.PathOf($"/Content/Scenes/Main{AssetFileTypes.SceneDescriptor}"),
            AssetPath.Parse($"/Content/Scenes/Main{AssetFileTypes.Scene}"));
        var json = $$"""
            { "Id": "00000000-0000-4000-8000-000000000101", "Name": "Main", "RootNodes": [ { "Id": "00000000-0000-4000-8000-000000000201",
              "Components": [ { "$type": "Metadata", "Name": "Car" }, { "$type": "Transform", "Name": "Transform" },
                { "$type": "NodeFlags", "Name": "Rendering" }, { "$type": "Hierarchy", "Name": "Hierarchy" },
                { "$type": "SceneInstance", "Name": "Scene Instance", "Source": "{{written}}" } ] } ] }
            """;
        return new SceneDocumentKind().DecodeAsync(
            file,
            Encoding.UTF8.GetBytes(json),
            new DocumentDecodeContext(DocumentId.New(), new HistoryKeeper(file)),
            cancellationToken);
    }
}
