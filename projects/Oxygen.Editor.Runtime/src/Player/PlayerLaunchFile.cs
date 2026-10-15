// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.Runtime.Player;

/// <summary>
/// The whole input of an <c>Oxygen.Player</c> run, as <c>oxygen.player-launch.schema.json</c> version 2 defines it.
/// </summary>
/// <remarks>
/// The player reads the file at launch and again at every Play, taking the mounts and the scene it names then; the
/// window, control and script source roots apply at launch only. A launcher changes a running player's window and
/// frame pacing through its control commands.
/// </remarks>
/// <param name="Mounts">Cooked roots or PAK files, highest precedence last; absolute paths.</param>
/// <param name="Scene">The virtual path of the scene to play.</param>
/// <param name="ScriptSourceRoots">Roots of external script sources; non-empty enables source reload.</param>
/// <param name="Window">The player window.</param>
public sealed record PlayerLaunchFile(
    IReadOnlyList<string> Mounts,
    string Scene,
    IReadOnlyList<string> ScriptSourceRoots,
    PlayerLaunchWindow Window)
{
    /// <summary>The launch file format version.</summary>
    public const int Version = 2;

    private static readonly JsonSerializerOptions Options = new() { WriteIndented = true };

    /// <summary>Writes the launch file, replacing it atomically so a player never reads a partial file.</summary>
    /// <param name="path">The file path.</param>
    /// <param name="cancellationToken">Cancels the write.</param>
    /// <returns>A task that completes when the file is in place.</returns>
    public async Task WriteAsync(string path, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrEmpty(path);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        var document = new Document(
            Version,
            this.Mounts,
            this.Scene,
            this.ScriptSourceRoots,
            new WindowDocument(
                this.Window.Title,
                ModeName(this.Window.Mode),
                this.Window.Width,
                this.Window.Height,
                this.Window.VSync,
                this.Window.FrameCap),
            new ControlDocument(Port: 0, ExitWhenOwnerLeaves: true));
        var temporary = path + ".tmp";
        var stream = File.Create(temporary);
        await using (stream.ConfigureAwait(false))
        {
            await JsonSerializer.SerializeAsync(stream, document, Options, cancellationToken).ConfigureAwait(false);
        }

        File.Move(temporary, path, overwrite: true);
    }

    /// <summary>Gets the name the player's launch file and window commands use for a window mode.</summary>
    /// <param name="mode">The window mode.</param>
    /// <returns><c>windowed</c> or <c>borderless</c>.</returns>
    public static string ModeName(PlayerWindowMode mode)
        => mode == PlayerWindowMode.Borderless ? "borderless" : "windowed";

    private sealed record Document(
        [property: JsonPropertyName("version")] int Version,
        [property: JsonPropertyName("mounts")] IReadOnlyList<string> Mounts,
        [property: JsonPropertyName("scene")] string Scene,
        [property: JsonPropertyName("script_source_roots")] IReadOnlyList<string> ScriptSourceRoots,
        [property: JsonPropertyName("window")] WindowDocument Window,
        [property: JsonPropertyName("control")] ControlDocument Control);

    private sealed record WindowDocument(
        [property: JsonPropertyName("title")] string Title,
        [property: JsonPropertyName("mode")] string Mode,
        [property: JsonPropertyName("width")] int Width,
        [property: JsonPropertyName("height")] int Height,
        [property: JsonPropertyName("vsync")] bool VSync,
        [property: JsonPropertyName("frame_cap")] int FrameCap);

    private sealed record ControlDocument(
        [property: JsonPropertyName("port")] int Port,
        [property: JsonPropertyName("exit_when_owner_leaves")] bool ExitWhenOwnerLeaves);
}
