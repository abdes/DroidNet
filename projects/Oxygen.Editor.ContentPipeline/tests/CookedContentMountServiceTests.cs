// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies ordered mounting and physical file ownership through validation and native use.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed partial class CookedContentMountServiceTests
{
    /// <summary>Gets or sets the active test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The default order is stable and every mounted source retains protected bytes.</summary>
    /// <returns>The asynchronous mount lifetime regression.</returns>
    [TestMethod]
    public async Task DefaultOrderAndReadersSurviveUntilTheMountSetIsReleased()
    {
        using var fixture = new Fixture();
        using var reader = new TrackingReader();
        var mounts = await fixture.Service.PrepareAsync(fixture.Project, [fixture.ProjectOutput], reader, this.TestContext.CancellationToken).ConfigureAwait(false);
        using (mounts)
        {
            _ = mounts.Roots.Should().Equal(fixture.First, fixture.Second, fixture.ProjectOutput);
            _ = reader.Disposed.Should().BeFalse();
            foreach (var root in mounts.Roots)
            {
                Action write = () => File.WriteAllBytes(Path.Combine(root, "Materials", "Shared.omat"), [9]);
                _ = write.Should().Throw<IOException>();
            }
        }

        _ = reader.Disposed.Should().BeTrue();
        File.WriteAllBytes(Path.Combine(fixture.First, "Materials", "Shared.omat"), [9]);
    }

    /// <summary>Explicit overrides and repeated aliases use the final occurrence of a physical root.</summary>
    /// <returns>The asynchronous source-order regression.</returns>
    [TestMethod]
    public async Task ExplicitOrderKeepsAnOverrideAboveProjectOutputAndDeduplicatesAliases()
    {
        using var fixture = new Fixture();
        var project = fixture.Project with
        {
            LocalFolderMounts = [.. fixture.Project.LocalFolderMounts, new("Alias", fixture.First)],
            CookedContentOrder = [new(CookedContentSourceKind.LocalFolder, "First"), new(CookedContentSourceKind.LocalFolder, "Second"), new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.LocalFolder, "Alias")],
        };
        using var reader = new TrackingReader();
        using var mounts = await fixture.Service.PrepareAsync(project, [fixture.ProjectOutput], reader, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = mounts.Roots.Should().Equal(fixture.Second, fixture.ProjectOutput, fixture.First);
    }

    /// <summary>A damaged library cannot be accepted merely because project output masks its top-level assets.</summary>
    /// <returns>The asynchronous validation regression.</returns>
    [TestMethod]
    public async Task DamagedMaskedLibraryFailsBeforeMountAndReleasesReaders()
    {
        using var fixture = new Fixture();
        using var reader = new TrackingReader();
        var path = Path.Combine(fixture.First, "Materials", "Shared.omat");
        File.WriteAllBytes(path, [8, 8, 8]);
        Func<Task> prepare = () => fixture.Service.PrepareAsync(fixture.Project, [fixture.ProjectOutput], reader, this.TestContext.CancellationToken);
        _ = await prepare.Should().ThrowAsync<InvalidDataException>().WithMessage("*integrity check*").ConfigureAwait(false);
        _ = reader.Disposed.Should().BeTrue();
        File.WriteAllBytes(path, [9]);
        fixture.Native.VerifyNoOtherCalls();
    }

    /// <summary>Cancellation waits for preparation to end and releases every acquired file reader.</summary>
    /// <returns>The asynchronous cancellation regression.</returns>
    [TestMethod]
    public async Task CancelledValidationReleasesThePreparedSourceSet()
    {
        using var fixture = new Fixture();
        using var reader = new TrackingReader();
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Native.Setup(value => value.ValidateLooseCookedRootAsync(It.IsAny<string>(), It.IsAny<CancellationToken>()))
            .Returns(async (string path, CancellationToken token) =>
            {
                entered.SetResult();
                await Task.Delay(Timeout.InfiniteTimeSpan, token).ConfigureAwait(false);
                return new CookValidationResult(path, Succeeded: true, []);
            });
        var preparation = fixture.Service.PrepareAsync(fixture.Project, [fixture.ProjectOutput], reader, cancellation.Token);
        await entered.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await cancellation.CancelAsync().ConfigureAwait(false);
        Func<Task> finish = () => preparation;
        _ = await finish.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = reader.Disposed.Should().BeTrue();
        File.WriteAllBytes(Path.Combine(fixture.First, "Materials", "Shared.omat"), [9]);
    }

    private sealed partial class TrackingReader : IDisposable
    {
        public bool Disposed { get; private set; }

        public void Dispose() => this.Disposed = true;
    }

    private sealed partial class Fixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-CookedMounts-");

        public Fixture()
        {
            this.ProjectOutput = this.WriteRoot(".cooked/Content", 1);
            this.First = this.WriteRoot("First", 2);
            this.Second = this.WriteRoot("Second", 3);
            this.Project = new ProjectContext
            {
                ProjectId = Guid.NewGuid(), Name = "Mounts", Category = Category.Games, ProjectRoot = this.directory.FullName,
                AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [new("First", this.First), new("Second", this.Second)], Scenes = [],
            };
            _ = this.Native.Setup(value => value.ValidateLooseCookedRootAsync(It.IsAny<string>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync((string path, CancellationToken _) => new CookValidationResult(path, Succeeded: true, []));
            this.Service = new(new NativeStorageProvider(new RealFileSystem()), this.Native.Object);
        }

        public string ProjectOutput { get; }

        public string First { get; }

        public string Second { get; }

        public ProjectContext Project { get; }

        public Mock<IEngineContentPipelineApi> Native { get; } = new(MockBehavior.Strict);

        public CookedContentMountService Service { get; }

        public void Dispose() => this.directory.Delete(recursive: true);

        private string WriteRoot(string relative, byte value)
        {
            var root = Path.GetFullPath(Path.Combine(this.directory.FullName, relative));
            _ = Directory.CreateDirectory(Path.Combine(root, "Materials"));
            byte[] bytes = [value, value, value];
            File.WriteAllBytes(Path.Combine(root, "Materials", "Shared.omat"), bytes);
            using var index = File.Create(Path.Combine(root, "container.index.bin"));
            LooseCookedIndex.Write(index, new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [new(new AssetKey(1, 2), "Materials/Shared.omat", "/Content/Materials/Shared.omat", 1, (ulong)bytes.Length, SHA256.HashData(bytes))], []));
            return root;
        }
    }
}
