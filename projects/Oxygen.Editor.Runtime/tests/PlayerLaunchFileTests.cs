// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Player;

namespace Oxygen.Editor.Runtime.Tests;

/// <summary>Checks the launch file the editor writes for the player and the discovery file it reads back.</summary>
[TestClass]
public sealed class PlayerLaunchFileTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task WriteAsync_WritesEveryFieldInTheSchemaShapeAndOwnsThePlayer()
    {
        var folder = Path.Combine(Path.GetTempPath(), "oxygen-launch-file-tests", Guid.NewGuid().ToString("N"));
        var path = Path.Combine(folder, "play", "launch.json");
        try
        {
            var launch = new PlayerLaunchFile(
                [@"C:\Project\.cooked\generations\a", @"C:\Project\.cooked\generations\b"],
                "/Content/Scenes/Level.oscene",
                [@"C:\Project\Content"],
                new PlayerLaunchWindow("Level - Oxygen Player", PlayerWindowMode.Borderless, 1600, 900, VSync: false, FrameCap: 60));

            await launch.WriteAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);

            using var document = JsonDocument.Parse(await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false));
            var root = document.RootElement;
            _ = root.GetProperty("version").GetInt32().Should().Be(PlayerLaunchFile.Version);
            _ = root.GetProperty("mounts").EnumerateArray().Select(static mount => mount.GetString()).Should()
                .Equal(@"C:\Project\.cooked\generations\a", @"C:\Project\.cooked\generations\b");
            _ = root.GetProperty("scene").GetString().Should().Be("/Content/Scenes/Level.oscene");
            _ = root.GetProperty("script_source_roots")[0].GetString().Should().Be(@"C:\Project\Content");
            var window = root.GetProperty("window");
            _ = window.GetProperty("title").GetString().Should().Be("Level - Oxygen Player");
            _ = window.GetProperty("mode").GetString().Should().Be("borderless");
            _ = window.GetProperty("width").GetInt32().Should().Be(1600);
            _ = window.GetProperty("height").GetInt32().Should().Be(900);
            _ = window.GetProperty("vsync").GetBoolean().Should().BeFalse();
            _ = window.GetProperty("frame_cap").GetInt32().Should().Be(60);
            _ = root.GetProperty("control").GetProperty("exit_when_owner_leaves").GetBoolean().Should().BeTrue();
            _ = File.Exists(path + ".tmp").Should().BeFalse();
        }
        finally
        {
            if (Directory.Exists(folder))
            {
                Directory.Delete(folder, recursive: true);
            }
        }
    }

    [TestMethod]
    public void TryRead_ReadsACompleteDiscoveryFileOnly()
    {
        var folder = Path.Combine(Path.GetTempPath(), "oxygen-discovery-tests", Guid.NewGuid().ToString("N"));
        _ = Directory.CreateDirectory(folder);
        try
        {
            var path = Path.Combine(folder, "4242.json");
            _ = PlayerControlEndpoint.TryRead(path).Should().BeNull("the server has not written it yet");

            File.WriteAllText(path, """{ "protocol": 1, "pid": 4242, "po""");
            _ = PlayerControlEndpoint.TryRead(path).Should().BeNull("the server is still writing it");

            File.WriteAllText(path, """{ "protocol": 1, "pid": 4242, "port": 50513, "token": "abc" }""");
            _ = PlayerControlEndpoint.TryRead(path).Should().Be(new PlayerControlEndpoint(50513, "abc"));

            File.WriteAllText(path, """{ "protocol": 2, "pid": 4242, "port": 50513, "token": "abc" }""");
            var read = () => PlayerControlEndpoint.TryRead(path);
            _ = read.Should().Throw<PlayerLaunchException>().WithMessage("*protocol 2*");
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }
}
