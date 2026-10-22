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
}
