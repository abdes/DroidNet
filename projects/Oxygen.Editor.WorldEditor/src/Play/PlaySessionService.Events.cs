// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text.Json.Nodes;
using DroidNet.Controls.OutputConsole.Model;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.Runtime.Player;
using Oxygen.Managed.Core.Diagnostics;
using Serilog.Events;

namespace Oxygen.Editor.World.Play;

/// <summary>
/// The player's events: lifecycle drives the status, the log goes to Output, and script diagnostics and scene
/// issues go to Problems.
/// </summary>
public sealed partial class PlaySessionService
{
    private const string PlayerSource = "Player";

    private void OnPlayerEvent(object? sender, PlayerEvent received)
    {
        if (!ReferenceEquals(sender, this.player))
        {
            return;
        }

        switch (received.Topic)
        {
            case PlayerEvent.LifecycleTopic:
                this.OnLifecycle(received);
                break;
            case PlayerEvent.LogTopic:
                this.AppendLog(received);
                break;
            case PlayerEvent.DiagnosticTopic when this.reader is { } diagnostics:
                this.AddRuntimeProblem(diagnostics.Read(received.Data));
                break;
            case PlayerEvent.StatsTopic when this.Status.IsRunning:
                this.Status = this.Status with { FramesPerSecond = received.Data["fps"]?.GetValue<int>() };
                break;
            default:
                break;
        }
    }

    // While Play is on its way, it sets the status itself; otherwise a session began or ended from the player.
    private void OnLifecycle(PlayerEvent received)
    {
        var state = received.GetString("state");
        this.LogLifecycle(state, received.GetString("scene"));
        switch (state)
        {
            case "session_started":
                this.output.Clear();
                this.runtimeProblems.Clear();
                this.RuntimeProblemsChanged?.Invoke(this, EventArgs.Empty);
                if (!this.Status.IsBusy)
                {
                    this.scene = null;
                    this.Status = new PlayStatus(PlayActivity.Playing, SceneName: Path.GetFileNameWithoutExtension(received.GetString("scene")));
                }

                break;
            case "scene_loaded":
                this.AddSceneIssues(received.Data, ScriptProblemSeverity.Warning);
                break;
            case "scene_failed":
                this.AddSceneIssues(received.Data, ScriptProblemSeverity.Error);
                break;
            case "paused" when this.Status.IsRunning:
                this.Status = this.Status with { Activity = PlayActivity.Paused };
                break;
            case "resumed" when this.Status.IsRunning:
                this.Status = this.Status with { Activity = PlayActivity.Playing };
                break;
            case "session_ended" when this.Status.IsRunning:
                this.Status = new PlayStatus(PlayActivity.PlayerIdle);
                break;
            default:
                break;
        }
    }

    private void OnSettingRefused(object? sender, string message)
        => this.output.Append(new OutputLogEntry { Timestamp = DateTimeOffset.Now, Level = LogEventLevel.Warning, Source = PlayerSource, Message = message });

    // A player that ends while Play is on its way fails that Play, which reports it.
    private void OnPlayerEnded(object? sender, bool expected)
    {
        if (sender is not PlayerConnection gone || !ReferenceEquals(gone, this.player))
        {
            return;
        }

        this.player = null;
        if (this.Status.IsBusy)
        {
            return;
        }

        if (expected)
        {
            this.Status = PlayStatus.Idle;
            return;
        }

        this.AppendPlayerOutput(gone.RecentOutput);

        const string message = "The player stopped unexpectedly. Its last output is in the Output panel.";
        this.Status = this.Status with
        {
            Activity = PlayActivity.Failed,
            IsStale = false,
            FramesPerSecond = null,
            Failure = new PlayFailure("Player stopped", message, PlayPanel.Output),
        };
        this.PublishFailure(PlayOperationKinds.PlayerExited, PlayDiagnosticCodes.SessionEndedUnexpectedly, "Player stopped", message, technicalMessage: null, gone.RecentOutput);
    }

    // The last lines a player printed before it ended, which say why.
    private void AppendPlayerOutput(IReadOnlyList<string> lines)
    {
        foreach (var line in lines)
        {
            this.output.Append(new OutputLogEntry { Timestamp = DateTimeOffset.Now, Level = LogEventLevel.Error, Source = PlayerSource, Message = line });
        }
    }

    private void AppendLog(PlayerEvent received)
        => this.output.Append(new OutputLogEntry
        {
            Timestamp = DateTimeOffset.Now,
            Level = received.GetString("severity") switch
            {
                "fatal" => LogEventLevel.Fatal,
                "error" => LogEventLevel.Error,
                "warning" => LogEventLevel.Warning,
                _ => LogEventLevel.Information,
            },
            Source = PlayerSource,
            Channel = received.GetString("channel"),
            Message = received.GetString("text"),
        });

    // Scene issues name records by node index; the cooked scene lists nodes in the editor's hierarchy order.
    private void AddSceneIssues(JsonObject data, ScriptProblemSeverity severity)
    {
        if (data["issues"] is not JsonArray issues)
        {
            return;
        }

        var nodes = this.scene?.Scene.AllNodes.ToArray() ?? [];
        foreach (var issue in issues.OfType<JsonObject>())
        {
            var message = issue["message"]?.GetValue<string>() ?? string.Empty;
            var index = issue["node_index"] is JsonValue value && value.TryGetValue<int>(out var number) ? number : (int?)null;
            var node = index is { } at && at < nodes.Length ? nodes[at].Name : null;
            var code = severity == ScriptProblemSeverity.Error ? PlayDiagnosticCodes.SceneFailed : PlayDiagnosticCodes.SceneIssue;
            var location = node is null ? string.Empty : string.Create(CultureInfo.InvariantCulture, $"\n = note: on node '{node}'");
            this.AddRuntimeProblem(new ScriptProblem(
                severity,
                code,
                message,
                SourcePath: string.Empty,
                Line: null,
                Column: null,
                $"{(severity == ScriptProblemSeverity.Error ? "error" : "warning")}[{code}]: {message}{location}\n",
                "The player could not apply this part of the scene; the rest of the scene loaded.")
            {
                SlotNode = node,
                IsRuntime = true,
            });
        }
    }

    private void AddRuntimeProblem(ScriptProblem problem)
    {
        this.runtimeProblems.Add(problem);
        this.RuntimeProblemsChanged?.Invoke(this, EventArgs.Empty);

        // A runtime error brings Problems forward; warnings wait to be looked at.
        if (problem.Severity == ScriptProblemSeverity.Error)
        {
            this.workspace?.Reveal(PlayPanel.Problems);
        }
    }
}
