// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.LivePreview.Tests.Session;

/// <summary>
/// A <c>.mounts.json</c> change during a session is a mount change: the watcher resolves the table again when the
/// project's own file or one above it changes, and applies a table that differs, with the project's declarations as
/// they are.
/// </summary>
[TestClass]
[TestCategory("Mounts")]
public sealed class MountsFileWatcherTests : IDisposable
{
    private const string Parent = @"C:\Studio";
    private const string Root = @"C:\Studio\Games\Demo";
    private const string Shared = @"D:\Shared";

    private readonly MockFileSystem fileSystem = new();
    private readonly Mock<IContentMountChanges> changes = new();
    private readonly TaskCompletionSource<(ProjectContext Expected, ProjectInfo Candidate, MountBindings Bindings)> applied = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly TestSession session;

    public MountsFileWatcherTests()
    {
        _ = this.fileSystem.Directory.CreateDirectory(Shared);
        var info = new ProjectInfo(Guid.NewGuid(), "Demo", Category.Games, Root)
        {
            Mounts = [new ProjectMount("Content", MountKind.Source, "Content"), new ProjectMount("Shared")],
        };
        _ = this.fileSystem.Directory.CreateDirectory(Path.Combine(Root, "Content"));
        this.session = new(TestProject.Open(info, this.fileSystem).Context);
        _ = this.changes.Setup(value => value.ChangeMountsAsync(It.IsAny<ProjectContext>(), It.IsAny<ProjectInfo>(), It.IsAny<MountBindings>(), It.IsAny<CancellationToken>()))
            .Callback<ProjectContext, ProjectInfo, MountBindings, CancellationToken>((expected, candidate, bindings, _) => this.applied.TrySetResult((expected, candidate, bindings)))
            .ReturnsAsync(true);
    }

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task AnUnchangedTable_IsNotApplied()
    {
        using var watcher = this.CreateWatcher();

        var changed = await watcher.ApplyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = changed.Should().BeFalse();
        this.changes.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task ABindingInTheProjectsOwnFile_IsAppliedWithTheDeclarationsAsTheyAre()
    {
        using var watcher = this.CreateWatcher();
        this.fileSystem.File.WriteAllText(Path.Combine(Root, ProjectMounts.FileName), """{"schema_version":2,"mounts":{"Shared":"D:/Shared"}}""");

        var changed = await watcher.ApplyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = changed.Should().BeTrue();
        var (expected, candidate, bindings) = await this.applied.Task.ConfigureAwait(false);
        _ = expected.Should().BeSameAs(this.session.Context);
        _ = candidate.Mounts.Should().Equal(this.session.Context.DeclaredMounts);
        _ = bindings.IsEmpty.Should().BeTrue("the file is already written");
    }

    [TestMethod]
    [DataRow(Root, DisplayName = "The project's own file")]
    [DataRow(Parent, DisplayName = "A file above the project")]
    public async Task WritingAMountsFile_AppliesTheNewTable(string folder)
    {
        using var watcher = this.CreateWatcher();

        this.fileSystem.File.WriteAllText(Path.Combine(folder, ProjectMounts.FileName), """{"schema_version":2,"mounts":{"Shared":"D:/Shared"}}""");

        var (_, candidate, _) = await this.applied.Task.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = MountChange.CandidateContext(this.session.Context, candidate).Mounts.Single(static mount => mount.Name == "Shared").Location
            .Should().Be(FileSystemPath.From(Shared));
    }

    public void Dispose() => this.session.Dispose();

    private MountsFileWatcher CreateWatcher() => new(this.session, this.changes.Object, NullLogger<MountsFileWatcher>.Instance);
}
