// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

/// <summary>Two sources beside each other that cook to one asset collide; each reports the other.</summary>
[TestClass]
internal sealed class SourceCollisionsTests
{
    private readonly TestProject project = TestProject.Create(new MockFileSystem());

    [TestMethod]
    public void ScriptsWithOneNameCollide()
    {
        var lua = this.Write("move.lua");
        var luau = this.Write("move.luau");
        _ = this.Write("jump.lua");

        _ = Names(SourceCollisions.Find(this.project.Context, lua)).Should().Equal("move.luau");
        _ = Names(SourceCollisions.Find(this.project.Context, luau)).Should().Equal("move.lua");
        _ = SourceCollisions.Find(this.project.Context, luau, ignored: lua).Should().BeEmpty("a rename does not collide with the file it renames");
    }

    [TestMethod]
    public void ANameDifferingOnlyInCaseCollides()
    {
        _ = this.Write("Red.omat.json");

        _ = Names(SourceCollisions.Find(this.project.Context, this.PathOf("red.omat.json"))).Should().ContainSingle().Which.Should().Be("Red.omat.json", "a case-insensitive file system holds both in one cooked file");
    }

    [TestMethod]
    public void SourcesOfDifferentAssetsDoNotCollide()
    {
        _ = this.Write("Red.omat.json");
        _ = this.Write("Red.ogeo.json");
        _ = this.Write("Red.png");

        _ = SourceCollisions.Find(this.project.Context, this.PathOf("Red.omat.json")).Should().BeEmpty();
        _ = SourceCollisions.Find(this.project.Context, this.PathOf("Red.png")).Should().BeEmpty("an image cooks into no descriptor of its own");
    }

    private static string[] Names(IReadOnlyList<FileSystemPath> paths) => [.. paths.Select(static path => path.Name)];

    private FileSystemPath PathOf(string name) => this.project.PathOf("/Content/" + name);

    private FileSystemPath Write(string name) => this.project.WriteFile("/Content/" + name, "{}");
}
