// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Testing;

/// <summary>
/// The guarantees the fakes of the module's test kit give the tests of other modules: a held read waits for its gate,
/// and a moved asset resolves through every later move.
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
}
