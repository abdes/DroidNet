// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

/// <summary>Two sources beside each other that cook to one asset collide; each reports the other.</summary>
[TestClass]
public sealed class SourceCollisionsTests : IDisposable
{
    private readonly string folder = Path.Combine(Path.GetTempPath(), "oxygen-source-collision-tests", Guid.NewGuid().ToString("N"));

    public SourceCollisionsTests() => Directory.CreateDirectory(this.folder);

    public void Dispose()
    {
        if (Directory.Exists(this.folder))
        {
            Directory.Delete(this.folder, recursive: true);
        }
    }

    [TestMethod]
    public void ScriptsWithOneNameCollide()
    {
        var lua = this.Write("move.lua");
        var luau = this.Write("move.luau");
        _ = this.Write("jump.lua");

        _ = Names(SourceCollisions.Find(lua)).Should().Equal("move.luau");
        _ = Names(SourceCollisions.Find(luau)).Should().Equal("move.lua");
        _ = SourceCollisions.Find(luau, ignored: lua).Should().BeEmpty("a rename does not collide with the file it renames");
    }

    [TestMethod]
    public void ANameDifferingOnlyInCaseCollides()
    {
        _ = this.Write("Red.omat.json");

        _ = Names(SourceCollisions.Find(this.PathOf("red.omat.json"))).Should().ContainSingle().Which.Should().Be("Red.omat.json", "a case-insensitive file system holds both in one cooked file");
    }

    [TestMethod]
    public void SourcesOfDifferentAssetsDoNotCollide()
    {
        _ = this.Write("Red.omat.json");
        _ = this.Write("Red.ogeo.json");
        _ = this.Write("Red.png");

        _ = SourceCollisions.Find(this.PathOf("Red.omat.json")).Should().BeEmpty();
        _ = SourceCollisions.Find(this.PathOf("Red.png")).Should().BeEmpty("an image cooks into no descriptor of its own");
    }

    private static string[] Names(IReadOnlyList<FileSystemPath> paths) => [.. paths.Select(static path => path.Name)];

    private FileSystemPath PathOf(string name) => FileSystemPath.From(Path.Combine(this.folder, name));

    private FileSystemPath Write(string name)
    {
        File.WriteAllText(Path.Combine(this.folder, name), "{}");
        return this.PathOf(name);
    }
}
