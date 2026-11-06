// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

/// <summary>
/// A cooked mount reaches the cook and the reference check through the mount table: a valid library is declared and bound
/// for the ImportTool at what is read, resolves what it holds and nothing else, and an invalid or unbound one resolves
/// nothing. A generated descriptor spells every reference as the engine requires, and a run reports how it ended.
/// </summary>
[TestClass]
internal sealed class CookedMountReferencesTests
{
    private const string Root = @"C:\Games\Demo";

    private readonly ProjectContext project = CreateProject();

    [TestMethod]
    public void AValidLibraryIsDeclaredAndBoundForTheTool()
    {
        _ = ContentImportManifestBuilder.CreateMounts(this.project).Should().Contain(new ContentImportMount("Vendor", Path: null) { Kind = "Cooked" });
        _ = ContentImportMountsFile.Of(this.project).Mounts["Vendor"].Should().Be("E:/Vendor");
    }

    [TestMethod]
    public void AnInvalidOrUnboundLibraryIsNotDeclared()
    {
        var names = ContentImportManifestBuilder.CreateMounts(this.project).Where(static mount => mount.Kind is not null).Select(static mount => mount.Name);

        _ = names.Should().Equal("Vendor");
        _ = ContentImportMountsFile.Of(this.project).Mounts.Keys.Should().NotContain("Stale").And.NotContain("Gone");
    }

    [TestMethod]
    public void ALibraryResolvesWhatItHolds()
    {
        _ = ReferenceCheck.UnresolvedCode(this.project, AssetPath.Parse("/Vendor/Wood.omat")).Should().BeNull();
        _ = ReferenceCheck.UnresolvedCode(this.project, AssetPath.Parse("/Vendor/Removed.omat")).Should().Be(ReferenceCheck.ReferenceUnresolved);
    }

    [TestMethod]
    public void AnUnusableLibraryResolvesNothing()
    {
        _ = ReferenceCheck.UnresolvedCode(this.project, AssetPath.Parse("/Stale/Wood.omat")).Should().Be(MountCodes.CookedRootMismatch);
        _ = ReferenceCheck.UnresolvedCode(this.project, AssetPath.Parse("/Gone/Wood.omat")).Should().Be(MountCodes.Unbound);
    }

    [TestMethod]
    [DataRow("/Content/Scenes/Main.oscene", "/Content/Models/Car.ogeo", ".ogeo", "../Models/Car.ogeo")]
    [DataRow("/Content/Scenes/Main.oscene", "/Content/Scenes/Sky.otex", ".otex", "Sky.otex")]
    [DataRow("/Content/Scenes/Main.oscene", "/Vendor/Wood.omat", ".omat", "/Vendor/Wood.omat")]
    [DataRow("/Content/Scenes/Main.oscene", "/Engine/Materials/Default.omat", ".omat", "/Engine/Materials/Default.omat")]
    public void AGeneratedDescriptorSpellsAReferenceFromItsJob(string anchor, string asset, string extension, string expected)
        => _ = ContentPipelinePaths.ToNativeReference(AssetPath.Parse(asset), extension, AssetPath.Parse(anchor)).Should().Be(expected);

    [TestMethod]
    public void AGeneratedDescriptorRefusesAnAssetOfAnotherType()
    {
        var spell = () => ContentPipelinePaths.ToNativeReference(AssetPath.Parse("/Content/Car.ogeo"), ".omat", AssetPath.Parse("/Content/Main.oscene"));

        _ = spell.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void AModelJobNamesItsIdentityAndNoSource()
    {
        var input = new ContentCookInput(AssetPath.Parse("/Content/Models/Truck.glb"), ContentCookAssetKind.ForeignSource, "Content", "/Content/Media/Truck.fbx", FileSystemPath.From(@"C:\Games\Demo\Content\Media\Truck.fbx"), ContentCookInputRole.Primary);

        var job = new ContentImportManifestBuilder().BuildJob(input, []);

        _ = job.IsModel.Should().BeTrue();
        _ = job.VirtualPath.Should().Be("/Content/Models/Truck.glb");
        _ = job.ManifestSource.Should().BeNull();
        _ = job.ReportedType.Should().Be("fbx", "the record's source decides the importer");
        _ = new ContentImportManifestValidator().Validate(Guid.NewGuid(), new(ContentImportManifest.CurrentSchemaVersion, @"C:\out", [job])).Should().BeEmpty();
        _ = new ContentImportManifestValidator().Validate(Guid.NewGuid(), new(ContentImportManifest.CurrentSchemaVersion, @"C:\out", [job with { VirtualPath = null }])).Should().ContainSingle();
    }

    [TestMethod]
    [DataRow(0, NativeRunOutcome.Succeeded)]
    [DataRow(1, NativeRunOutcome.Refused)]
    [DataRow(2, NativeRunOutcome.Failed)]
    [DataRow(3, NativeRunOutcome.Stopped)]
    [DataRow(4, NativeRunOutcome.ToolDefect)]
    [DataRow(99, NativeRunOutcome.Failed)]
    [DataRow(-1073741819, NativeRunOutcome.Failed)]
    public void ARunsOutcomeIsItsExitCode(int exitCode, NativeRunOutcome expected)
        => _ = NativeImportReportReader.OutcomeOf(exitCode).Should().Be(expected);

    [TestMethod]
    public void AStoppedRunAndAToolDefectSaySoWhateverTheirJobsReported()
    {
        var id = Guid.NewGuid();

        _ = NativeImportReportReader.DiagnosticOf(id, NativeRunOutcome.Stopped)!.Code.Should().Be(AssetImportDiagnosticCodes.RunStopped);
        var defect = NativeImportReportReader.DiagnosticOf(id, NativeRunOutcome.ToolDefect)!;
        _ = defect.Code.Should().Be(AssetImportDiagnosticCodes.ToolDefect);
        _ = defect.Domain.Should().Be(FailureDomain.ContentPipeline, "the content is not at fault");
        _ = NativeImportReportReader.DiagnosticOf(id, NativeRunOutcome.Failed).Should().BeNull("the jobs explain a failure");
    }

    private static ProjectContext CreateProject()
    {
        var fileSystem = new MockFileSystem();
        foreach (var folder in new[] { Path.Combine(Root, "Content"), @"E:\Vendor", @"E:\Stale" })
        {
            _ = fileSystem.Directory.CreateDirectory(folder);
        }

        fileSystem.File.WriteAllBytes(@"E:\Vendor\" + LooseCookedIndex.FileName, CookedLibraryFixture.LooseIndex("/Vendor/Wood.omat"));
        fileSystem.File.WriteAllBytes(@"E:\Stale\" + LooseCookedIndex.FileName, CookedLibraryFixture.LooseIndex("/Other/Wood.omat"));
        _ = TestProject.BindMounts(fileSystem, FileSystemPath.From(Root), ("Vendor", @"E:\Vendor"), ("Stale", @"E:\Stale"));
        var info = new ProjectInfo("Demo", Category.Games, Root)
        {
            Mounts =
            [
                new ProjectMount("Content", MountKind.Source, "Content"),
                new ProjectMount("Vendor", MountKind.Cooked),
                new ProjectMount("Stale", MountKind.Cooked),
                new ProjectMount("Gone", MountKind.Cooked),
            ],
        };
        return ProjectContext.FromProjectInfo(info, fileSystem);
    }
}
