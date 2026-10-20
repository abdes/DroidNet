// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Testing;

/// <summary>
/// The guarantees the fakes of the module's test kit give the tests of other modules: a held read waits for its gate,
/// a moved asset resolves through every later move, and the cook coordinator lets one operation write at a time.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class KitFakesTests
{
    private static readonly AssetPath Cube = AssetPath.Parse("/Content/Cube.ogeo");
    private static readonly AssetPath Sphere = AssetPath.Parse("/Content/Sphere.ogeo");

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task MaterialSlotReads_WaitForTheGate()
    {
        var project = TestProject.Create(new MockFileSystem());
        var slot = Guid.NewGuid();
        using var provider = new FakeMaterialSlotProvider();
        provider.Publish(Cube, new string('c', 64), slot);
        var gate = provider.HoldReads();

        var read = provider.ReadAsync(project.Context, Cube, this.TestContext.CancellationToken);
        await gate.ReadPending.ConfigureAwait(false);
        var heldWhilePending = read.IsCompleted;
        gate.Release();

        _ = heldWhilePending.Should().BeFalse();
        _ = (await read.ConfigureAwait(false))!.Slots.Should().ContainSingle().Which.SlotId.Should().Be(slot);
    }

    [TestMethod]
    public void Redirects_FollowEveryMove()
    {
        var redirects = new FakeAssetRedirects();
        redirects.Move(Cube, Sphere);
        var moved = AssetPath.Parse("/Content/Ball.ogeo");
        redirects.Move(Sphere, moved);

        _ = redirects.Resolve(Cube).Should().Be(moved);
    }

    [TestMethod]
    public async Task CookCoordinator_LetsOneOperationWriteAtATime()
    {
        var projects = new ProjectContextService();
        projects.Activate(TestProject.Create(new MockFileSystem()).Context);
        using var coordinator = new FakeContentCookCoordinator(projects);
        var firstRunning = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseFirst = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        ContentCookOperation? first = null;

        var firstRun = coordinator.RunAsync(
            async (operation, _) =>
            {
                first = operation;
                firstRunning.SetResult();
                await releaseFirst.Task.ConfigureAwait(false);
                return true;
            },
            this.TestContext.CancellationToken);
        await firstRunning.Task.ConfigureAwait(false);
        var secondRun = coordinator.RunAsync((_, _) => Task.FromResult(releaseFirst.Task.IsCompleted), this.TestContext.CancellationToken);
        coordinator.VerifyWriter(first!);
        releaseFirst.SetResult();
        _ = await firstRun.ConfigureAwait(false);

        _ = (await secondRun.ConfigureAwait(false)).Should().BeTrue("the second operation waits for the first to finish");
        _ = coordinator.Invoking(value => value.VerifyWriter(first!)).Should().Throw<InvalidOperationException>();
    }
}
