// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Runtime.Player;
using Oxygen.Editor.World.Scripting;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>
/// The player's events: lifecycle drives the status, the log is raised as <see cref="LogReceived"/>, and script
/// diagnostics and scene issues become runtime problems.
/// </summary>
public sealed partial class PlaySession
{
    private void OnPlayerEvent(object? sender, PlayerEventReceivedEventArgs args)
    {
        if (!ReferenceEquals(sender, this.player))
        {
            return;
        }

        var received = args.Event;

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
                this.LogReceived?.Invoke(this, PlayerLogEventArgs.SessionStarted());
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

    private void OnSettingRefused(object? sender, PlayerSettingRefusedEventArgs args)
        => this.LogReceived?.Invoke(this, new PlayerLogEventArgs(DateTimeOffset.Now, LogLevel.Warning, channel: null, args.Message));

    // A player that ends while Play is on its way fails that Play, which reports it.
    private void OnPlayerEnded(object? sender, PlayerEndedEventArgs args)
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

        if (args.Expected)
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
            Failure = new PlayFailure(PlayDiagnosticCodes.SessionEndedUnexpectedly, "Player stopped", message, PlayStage.Run),
        };
        this.PublishFailure(PlayOperationKinds.PlayerExited, PlayDiagnosticCodes.SessionEndedUnexpectedly, "Player stopped", message, technicalMessage: null, gone.RecentOutput);
    }

    // The last lines a player printed before it ended, which say why.
    private void AppendPlayerOutput(IReadOnlyList<string> lines)
    {
        foreach (var line in lines)
        {
            this.LogReceived?.Invoke(this, new PlayerLogEventArgs(DateTimeOffset.Now, LogLevel.Error, channel: null, line));
        }
    }

    private void AppendLog(PlayerEvent received)
    {
        var channel = received.GetString("severity") switch
        {
            "fatal" => LogLevel.Critical,
            "error" => LogLevel.Error,
            "warning" => LogLevel.Warning,
            _ => LogLevel.Information,
        };
        this.LogReceived?.Invoke(this, new PlayerLogEventArgs(
                DateTimeOffset.Now,
                channel,
                received.GetString("channel"),
                received.GetString("text")));
    }

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
            var location = node is null ? string.Empty : $"\n = note: on node '{node}'";
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
    }
}
