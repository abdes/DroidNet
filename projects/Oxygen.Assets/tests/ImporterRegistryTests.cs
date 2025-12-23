// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Assets.Import;

namespace Oxygen.Assets.Tests;

[TestClass]
public sealed class ImporterRegistryTests
{
    [TestMethod]
    public void Select_ShouldPickHighestPriorityMatchingImporter()
    {
        var registry = new ImporterRegistry();
        var low = new FakeImporter("Low", priority: 1, extensions: [".png"]);
        var high = new FakeImporter("High", priority: 10, extensions: [".png"]);

        registry.Register(low);
        registry.Register(high);

        var probe = new ImportProbe(
            SourcePath: "Content/Textures/Wood.png",
            Extension: ".png",
            HeaderBytes: ReadOnlyMemory<byte>.Empty);

        var selected = registry.Select(probe);

        _ = selected.Should().BeSameAs(high);
    }

    [TestMethod]
    public void Select_ShouldReturnNullWhenNoImporterMatches()
    {
        var registry = new ImporterRegistry();
        registry.Register(new FakeImporter("OnlyPng", priority: 0, extensions: [".png"]));

        var probe = new ImportProbe(
            SourcePath: "Content/Geometry/Cube.glb",
            Extension: ".glb",
            HeaderBytes: ReadOnlyMemory<byte>.Empty);

        var selected = registry.Select(probe);

        _ = selected.Should().BeNull();
    }

    [TestMethod]
    public void Select_ShouldBreakPriorityTiesByNameDeterministically()
    {
        var registry = new ImporterRegistry();
        var a = new FakeImporter("A", priority: 5, extensions: [".gltf"]);
        var b = new FakeImporter("B", priority: 5, extensions: [".gltf"]);

        registry.Register(b);
        registry.Register(a);

        var probe = new ImportProbe(
            SourcePath: "Content/Geometry/Robot.gltf",
            Extension: ".gltf",
            HeaderBytes: ReadOnlyMemory<byte>.Empty);

        var selected = registry.Select(probe);

        _ = selected.Should().BeSameAs(a);
    }

    private sealed class FakeImporter : IAssetImporter
    {
        private readonly HashSet<string> extensions;

        public FakeImporter(string name, int priority, IEnumerable<string> extensions)
        {
            ArgumentNullException.ThrowIfNull(name);
            ArgumentNullException.ThrowIfNull(extensions);

            this.Name = name;
            this.Priority = priority;
            this.extensions = new HashSet<string>(extensions, StringComparer.OrdinalIgnoreCase);
        }

        public string Name { get; }

        public int Priority { get; }

        public bool CanImport(ImportProbe probe)
        {
            ArgumentNullException.ThrowIfNull(probe);
            return this.extensions.Contains(probe.Extension);
        }

        public Task<IReadOnlyList<ImportedAsset>> ImportAsync(ImportContext context, CancellationToken cancellationToken)
        {
            throw new NotSupportedException("Not needed for selection tests.");
        }
    }
}
