// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

/// <summary>
/// What a cook decides from a root's inventory: an unchanged output is reused at the content-named path the index records,
/// a writer's leftovers are no damage, and damage rebuilds its owner or the whole root.
/// </summary>
[TestClass]
internal sealed class CookedRootDecisionsTests
{
    private const string Mount = "Content";
    private const string Descriptor = "Materials/Red.omat.0123456789abcdef";
    private const string Shared = "Resources/buffers.data";
    private static readonly AssetPath Source = AssetPath.Parse("/Content/Materials/Red.omat");
    private static readonly Guid SourceKey = Guid.Parse("7c9e6679-7425-40de-944b-e07fc1f90ae7");
    private static readonly string IndexSha = new('a', 64);

    private readonly ProjectContext project = CreateProject();

    [TestMethod]
    public void AnUnchangedOutputIsReusedAtItsContentNamedDescriptor()
    {
        var plan = Plan(Inventory());

        _ = plan.Reusable.Keys.Should().Equal(Source);
        _ = plan.VerifiedOutputs.Should().Contain((Mount, Source.VirtualPath));
    }

    [TestMethod]
    public void AnOutputWhoseDescriptorIsNoLongerListedIsNotReused()
    {
        var plan = Plan(Inventory(), descriptor: "Materials/Red.omat.fedcba9876543210");

        _ = plan.Reusable.Should().BeEmpty();
    }

    [TestMethod]
    public void AChangedSourceIsNotReused()
    {
        var plan = Plan(Inventory(), fingerprint: "another");

        _ = plan.Reusable.Should().BeEmpty();
    }

    [TestMethod]
    [DataRow("uncommitted")]
    [DataRow("uncommitted_tail")]
    public void AWritersLeftoversLeaveReuseAndRepairAlone(string reason)
    {
        var inventory = Inventory((reason == "uncommitted" ? "Materials/Stray.omat.0123456789abcdef" : Shared, reason));

        _ = Plan(inventory).Reusable.Keys.Should().Equal(Source);
        var repair = this.Repair(inventory);
        _ = repair.EmptyRoots.Should().BeEmpty();
        _ = repair.Sources.Should().BeEmpty();
    }

    [TestMethod]
    public void ADamagedDescriptorCooksOnlyItsOwner()
    {
        var repair = this.Repair(Inventory((Descriptor, "digest_mismatch")));

        _ = repair.EmptyRoots.Should().BeEmpty();
        _ = repair.Sources.Should().Equal(Source);
    }

    [TestMethod]
    public void DamagedSharedDataRebuildsTheRoot()
    {
        var repair = this.Repair(Inventory((Shared, "digest_mismatch"), ("Materials/Stray.omat.0123456789abcdef", "uncommitted")));

        _ = repair.EmptyRoots.Should().Equal(Mount);
        _ = repair.Sources.Should().Equal(Source);
    }

    private static CookIncrementalPlan Plan(CookedInventoryReport inventory, string descriptor = Descriptor, string fingerprint = "")
    {
        var input = Input();
        var graph = Graph(input);
        var current = CookIncrementalPlanner.Fingerprint(input, [], graph, ImporterRevisionsFixture.Current);
        var previous = Previous(descriptor, fingerprint.Length == 0 ? current : fingerprint);
        var inventories = ImmutableDictionary<string, CookedInventoryReport>.Empty.Add(Mount, inventory);
        return CookIncrementalPlanner.CreatePlan([], graph, previous, inventories, ImporterRevisionsFixture.Current);
    }

    private static CookProvenance Previous(string descriptor, string fingerprint)
    {
        var output = new ContentCookedAsset(Source, Source, ContentCookAssetKind.Material, Mount, Source.VirtualPath) { DescriptorRelativePath = descriptor };
        var product = new CookProvenance.Product(Source, fingerprint, [], [new(output, Mount)]) { ReuseFingerprint = fingerprint };
        return new(Guid.NewGuid(), [new(Mount, SourceKey, IndexSha)], [product]);
    }

    private static ContentCookInput Input()
        => new(Source, ContentCookAssetKind.Material, Mount, "/Content/Materials/Red.omat.json", FileSystemPath.From(@"C:\Games\Demo\Content\Materials\Red.omat.json"), ContentCookInputRole.Primary);

    private static CookDependencyGraph Graph(ContentCookInput input)
        => new(
            [input],
            [],
            ImmutableDictionary<AssetPath, ImmutableArray<AssetPath>>.Empty.Add(Source, []),
            ImmutableDictionary<AssetPath, ImmutableArray<string>>.Empty.Add(Source, []),
            [],
            []);

    // The Inspector's report of a root holding the material's descriptor and the shared buffer data.
    private static CookedInventoryReport Inventory(params (string Path, string Reason)[] issues)
        => CookedInventoryReport.Parse(JsonSerializer.Serialize(new
        {
            schema_version = EngineSchemaVersions.Inventory,
            source_key = SourceKey,
            index_size = 256,
            index_sha256 = IndexSha,
            files = new object[]
            {
                new { relative_path = Descriptor, size = 1, sha256 = new string('b', 64), file_kind = (int?)null },
                new { relative_path = Shared, size = 1, sha256 = new string('c', 64), file_kind = (int?)2 },
                new { relative_path = "Resources/buffers.table", size = 1, sha256 = new string('d', 64), file_kind = (int?)1 },
            },
            assets = new[]
            {
                new { asset_key = "01020304-0506-0708-090a-0b0c0d0e0f10", asset_type = 1, virtual_path = Source.VirtualPath, descriptor_relative_path = Descriptor },
            },
            resources = Array.Empty<object>(),
            issues = issues.Select(static issue => new { relative_path = issue.Path, reason = issue.Reason }),
        }));

    private static ProjectContext CreateProject()
    {
        var fileSystem = new MockFileSystem();
        var root = @"C:\Games\Demo";
        _ = fileSystem.Directory.CreateDirectory(Path.Combine(root, "Content", "Materials"));
        var info = new ProjectInfo("Demo", Category.Games, root) { Mounts = [new ProjectMount(Mount, MountKind.Source, Mount)] };
        return ProjectContext.FromProjectInfo(info, fileSystem);
    }

    private CookRootRepair Repair(CookedInventoryReport inventory)
        => CookRootRepair.Create(
            this.project,
            static _ => FileSystemPath.From(@"C:\Games\Demo\.cooked\generations\one"),
            Previous(Descriptor, "unused"),
            [Mount],
            ImmutableDictionary<string, CookedInventoryReport>.Empty.Add(Mount, inventory));
}
