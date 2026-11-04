// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Projects.Tests.Mounts;

/// <summary>
/// Bind and Override write the project's own <c>.mounts.json</c>: they bind and remove mounts, keep what the file holds
/// besides, return the changes that undo them, and refuse a file they cannot read.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Unit")]
public sealed class MountBindingsTests
{
    private const string Root = @"C:\Games\Demo";

    private readonly MockFileSystem fileSystem = new();

    public TestContext TestContext { get; set; } = null!;

    private string MountsFile => Path.Combine(Root, ProjectMounts.FileName);

    [TestMethod]
    public async Task Bind_WithoutAMountsFile_CreatesTheProjectsOwnFile()
    {
        _ = this.fileSystem.Directory.CreateDirectory(Root);

        _ = await this.WriteAsync(MountBindings.None.Bind("Studio", FileSystemPath.From(@"D:\Studio\Shared"))).ConfigureAwait(false);

        var document = this.ReadMountsFile();
        _ = document["schema_version"]!.GetValue<int>().Should().Be(2);
        _ = document["mounts"]!["Studio"]!.GetValue<string>().Should().Be("D:/Studio/Shared");
    }

    [TestMethod]
    public async Task Bind_KeepsTheOtherBindingsAndWhatTheFileHoldsBesides()
    {
        _ = this.fileSystem.Directory.CreateDirectory(Root);
        this.fileSystem.File.WriteAllText(this.MountsFile, """{"schema_version":2,"root":true,"mounts":{"Vendor":"E:/Vendor","Media":"H:/Media"}}""");

        _ = await this.WriteAsync(MountBindings.None.Bind("Studio", FileSystemPath.From(@"D:\Studio")).Unbind("Media")).ConfigureAwait(false);

        var document = this.ReadMountsFile();
        _ = document["root"]!.GetValue<bool>().Should().BeTrue();
        _ = document["mounts"]!.AsObject().Select(static entry => (entry.Key, entry.Value!.GetValue<string>()))
            .Should().BeEquivalentTo([("Vendor", "E:/Vendor"), ("Studio", "D:/Studio")]);
    }

    [TestMethod]
    public async Task TheUndoOfAWrite_RestoresTheBindingsItReplaced()
    {
        _ = this.fileSystem.Directory.CreateDirectory(Root);
        const string original = """{"schema_version":2,"mounts":{"Media":"H:/Media"}}""";
        this.fileSystem.File.WriteAllText(this.MountsFile, original);

        var undo = await this.WriteAsync(MountBindings.None.Bind("Media", FileSystemPath.From(@"G:\Media")).Bind("Studio", FileSystemPath.From(@"D:\Studio"))).ConfigureAwait(false);
        _ = await this.WriteAsync(undo).ConfigureAwait(false);

        _ = undo.Changes.Should().BeEquivalentTo(new Dictionary<string, string?>(StringComparer.Ordinal) { ["Media"] = "H:/Media", ["Studio"] = null });
        _ = this.ReadMountsFile()["mounts"]!.AsObject().Select(static entry => (entry.Key, entry.Value!.GetValue<string>()))
            .Should().Equal(("Media", "H:/Media"));
    }

    [TestMethod]
    public async Task NoChange_WritesNothing()
    {
        _ = this.fileSystem.Directory.CreateDirectory(Root);

        var undo = await this.WriteAsync(MountBindings.None).ConfigureAwait(false);

        _ = undo.IsEmpty.Should().BeTrue();
        _ = this.fileSystem.File.Exists(this.MountsFile).Should().BeFalse();
    }

    [TestMethod]
    [DataRow("""{"version":2,"mounts":{}}""")]
    [DataRow("""{"mounts":{}}""")]
    [DataRow("{")]
    public async Task AFileThatIsNotAMountsFileOfVersion1_IsRefusedAndKept(string content)
    {
        _ = this.fileSystem.Directory.CreateDirectory(Root);
        this.fileSystem.File.WriteAllText(this.MountsFile, content);

        var write = () => this.WriteAsync(MountBindings.None.Bind("Studio", FileSystemPath.From(@"D:\Studio")));

        _ = await write.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        _ = this.fileSystem.File.ReadAllText(this.MountsFile).Should().Be(content);
    }

    [TestMethod]
    public async Task AWrittenBinding_BindsThePlaceholderWhenTheTableIsResolvedAgain()
    {
        _ = this.fileSystem.Directory.CreateDirectory(Path.Combine(Root, "Content"));
        _ = this.fileSystem.Directory.CreateDirectory(@"D:\Studio");
        ProjectMount[] declared = [new("Content", MountKind.Source, "Content"), new("Studio")];
        _ = ProjectMounts.Resolve(this.fileSystem, FileSystemPath.From(Root), declared).Find("Studio")!.State.Should().Be(MountState.Unbound);

        _ = await this.WriteAsync(MountBindings.None.Bind("Studio", FileSystemPath.From(@"D:\Studio"))).ConfigureAwait(false);

        var studio = ProjectMounts.Resolve(this.fileSystem, FileSystemPath.From(Root), declared).Find("Studio")!;
        _ = studio.State.Should().Be(MountState.Bound);
        _ = studio.Origin.Should().Be(MountOrigin.MountsFile);
        _ = studio.Location.Should().Be(FileSystemPath.From(@"D:\Studio"));
    }

    private Task<MountBindings> WriteAsync(MountBindings bindings)
        => bindings.WriteAsync(new NativeAtomicFileStore(this.fileSystem), FileSystemPath.From(Root), this.TestContext.CancellationToken);

    private JsonObject ReadMountsFile() => JsonNode.Parse(this.fileSystem.File.ReadAllText(this.MountsFile))!.AsObject();
}
