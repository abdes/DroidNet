// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

/// <summary>
/// <see cref="AuthoredReferences"/>: the direct references of saved authored descriptors, such as a scene's instanced
/// sources, which the instance cycle rule follows (scene instances 8.3).
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class AuthoredReferencesTests
{
    private static readonly AssetPath Harbor = AssetPath.Parse("/Content/Scenes/Harbor.oscene");
    private static readonly AssetPath Watchtower = AssetPath.Parse("/Content/Scenes/Watchtower.oscene");
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task Refresh_ReadsEachSavedScenesInstancedSources()
    {
        using var workspace = CreateProject();
        var references = new AuthoredReferences(workspace.Session);

        await references.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = references.Of(Harbor).Should().Equal(Watchtower);
        _ = references.Of(Watchtower).Should().Equal(Car);
        _ = references.Of(Car).Should().BeEmpty();
    }

    [TestMethod]
    public void BeforeARead_NothingIsReferenced()
    {
        using var workspace = CreateProject();

        _ = new AuthoredReferences(workspace.Session).Of(Harbor).Should().BeEmpty();
    }

    [TestMethod]
    public async Task Refresh_FollowsASavedChange()
    {
        using var workspace = CreateProject();
        var references = new AuthoredReferences(workspace.Session);
        await references.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.Write("Content/Scenes/Watchtower.oscene.json", SceneInstancing("Watchtower", Harbor));
        await references.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = references.Of(Watchtower).Should().Equal(Harbor);
    }

    private static string SceneInstancing(string name, AssetPath source)
        => $$"""
            { "Id": "6a1f2c0e-6c1e-4a59-9a77-1f6a3e4b2d10", "Name": "{{name}}",
              "RootNodes": [ { "Id": "3d9a2b1c-0000-4000-8000-000000000001", "Components": [ { "$type": "Metadata", "Name": "Instance" },
                { "$type": "Transform", "Name": "Transform" }, { "$type": "NodeFlags", "Name": "Rendering" }, { "$type": "Hierarchy", "Name": "Hierarchy" },
                { "$type": "SceneInstance", "Name": "Scene Instance", "Source": "{{source.ToUriString()}}" } ] } ] }
            """;

    private static RelocationWorkspace CreateProject()
    {
        var workspace = new RelocationWorkspace();
        _ = workspace.Write("Content/Scenes/Harbor.oscene.json", SceneInstancing("Harbor", Watchtower));
        _ = workspace.Write("Content/Scenes/Watchtower.oscene.json", SceneInstancing("Watchtower", Car));
        return workspace;
    }
}
