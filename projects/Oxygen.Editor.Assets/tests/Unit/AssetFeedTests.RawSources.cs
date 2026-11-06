// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Assets.TestSupport;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Models;
using Oxygen.Managed.Assets.Authoring.Textures;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>A model and a texture are one row each; the files they own are not rows, and a file to import is.</summary>
public sealed partial class AssetFeedTests
{
    /// <summary>A model is one row, however its files lie: its record and its companions are not rows of their own.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AModelsRecordAndCompanionsAreNotRows()
    {
        using var workspace = new AssetWorkspace();
        var lamp = AssetPath.Parse("/Content/Props/Lamp.glb");
        var record = ModelImportSettings.Create(lamp, "Lamp/Lamp.glb", ModelImportSettings.DefaultSettings("Lamp")) with
        {
            SourceFingerprint = new(2, new string('a', 64), [new("Lamp.bin", 3, new string('b', 64))]),
        };
        await WriteAsync(workspace, "Content/Props/Lamp/Lamp.glb", "glb", this.TestContext.CancellationToken).ConfigureAwait(false);
        await WriteAsync(workspace, "Content/Props/Lamp/Lamp.bin", "bin", this.TestContext.CancellationToken).ConfigureAwait(false);
        await WriteAsync(workspace, "Content/Props/Lamp.glb.import.json", record.ToBytes(), this.TestContext.CancellationToken).ConfigureAwait(false);

        var rows = await this.RowsAsync(
            workspace,
            ["/Content/Props/Lamp.glb.import.json", "/Content/Props/Lamp/Lamp.glb", "/Content/Props/Lamp/Lamp.bin"]).ConfigureAwait(false);

        var row = rows.Should().ContainSingle().Subject;
        _ = row.Identity.Should().Be(lamp);
        _ = row.Kind.Should().Be(AssetKind.Model);
        _ = row.CanReimport.Should().BeTrue();
        _ = row.IsRawSource.Should().BeFalse();
    }

    /// <summary>V1: a source a record references stays a row with Import, since more settings can be made of it (V4).</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ASourceARecordOnlyReferencesStaysARawRow()
    {
        using var workspace = new AssetWorkspace();
        var truck = AssetPath.Parse("/Content/Cars/Truck.fbx");
        var record = ModelImportSettings.Create(truck, "../Raw/Truck.fbx", ModelImportSettings.DefaultSettings("Truck"));
        await WriteAsync(workspace, "Content/Raw/Truck.fbx", "fbx", this.TestContext.CancellationToken).ConfigureAwait(false);
        await WriteAsync(workspace, "Content/Cars/Truck.fbx.import.json", record.ToBytes(), this.TestContext.CancellationToken).ConfigureAwait(false);

        var rows = await this.RowsAsync(workspace, ["/Content/Cars/Truck.fbx.import.json", "/Content/Raw/Truck.fbx"]).ConfigureAwait(false);

        _ = rows.Single(row => row.Identity == truck).IsRawSource.Should().BeFalse();
        var raw = rows.Single(row => row.Identity == AssetPath.Parse("/Content/Raw/Truck.fbx"));
        _ = raw.IsRawSource.Should().BeTrue();
        _ = raw.PrimaryBadge.Should().Be("Not imported");
    }

    /// <summary>D2: a texture is one row and the image beside it its companion; any other image is a file to import.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AnImageBesideItsTextureIsNoRowAndAnyOtherIsARawRow()
    {
        using var workspace = new AssetWorkspace();
        var brick = AssetPath.Parse("/Content/Textures/Brick.otex");
        var descriptor = JsonSerializer.SerializeToUtf8Bytes(TextureSourceWriter.ToJson(new TextureSource("Brick.png") { Name = "Brick", Intent = "albedo", ColorSpace = "srgb", Format = "rgba8_srgb" }, brick));
        await WriteAsync(workspace, "Content/Textures/Brick.otex.json", descriptor, this.TestContext.CancellationToken).ConfigureAwait(false);
        await WriteAsync(workspace, "Content/Textures/Brick.png", "png", this.TestContext.CancellationToken).ConfigureAwait(false);
        await WriteAsync(workspace, "Content/Textures/Wood.png", "png", this.TestContext.CancellationToken).ConfigureAwait(false);

        var rows = await this.RowsAsync(workspace, ["/Content/Textures/Brick.otex.json", "/Content/Textures/Brick.png", "/Content/Textures/Wood.png"]).ConfigureAwait(false);

        _ = rows.Select(static row => row.Identity).Should().BeEquivalentTo([brick, AssetPath.Parse("/Content/Textures/Wood.png")]);
        var raw = rows.Single(row => row.Kind == AssetKind.Image);
        _ = raw.IsRawSource.Should().BeTrue();
        _ = raw.PrimaryBadge.Should().Be("Not imported");
        _ = raw.PrimaryBadgeTooltip.Should().Contain("create a texture");
        _ = rows.Single(row => row.Identity == brick).IsRawSource.Should().BeFalse();
    }

    private static Task WriteAsync(AssetWorkspace workspace, string relative, string content, CancellationToken token)
        => WriteAsync(workspace, relative, System.Text.Encoding.UTF8.GetBytes(content), token);

    private static async Task WriteAsync(AssetWorkspace workspace, string relative, byte[] content, CancellationToken token)
    {
        var path = workspace.SourcePath(relative);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        await File.WriteAllBytesAsync(path, content, token).ConfigureAwait(false);
    }

    private async Task<IReadOnlyList<AssetEntry>> RowsAsync(AssetWorkspace workspace, string[] files)
    {
        using var provider = AssetFeeds.Create(new TestProjectAssetCatalog([.. files.Select(file => new AssetRecord(AssetPath.Parse(file)))]), workspace.OpenSession(), new ProjectCookScopeProvider());
        IReadOnlyList<AssetEntry> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        return rows;
    }
}
