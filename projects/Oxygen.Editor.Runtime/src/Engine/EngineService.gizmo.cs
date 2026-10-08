// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Relays the transform gizmo's interactions from the engine thread, in order.</summary>
public sealed partial class EngineService
{
    private readonly ConcurrentQueue<RuntimeGizmoEvent> gizmoEvents = new();
    private int publishingGizmoEvents;

    // The session whose module already reports to this service.
    private EngineSession? gizmoListenerSession;

    /// <inheritdoc />
    public event EventHandler<RuntimeGizmoEventArgs>? GizmoEvent;

    // Each session's module needs the listener once; the caller holds the lifecycle gate.
    private bool EnsureGizmoListener(EngineSession running)
    {
        if (ReferenceEquals(this.gizmoListenerSession, running))
        {
            return true;
        }

        if (!running.SetTransformGizmoListener(this.OnGizmoEvent))
        {
            return false;
        }

        this.gizmoListenerSession = running;
        return true;
    }

    // Runs on the engine thread: queue and return, so the frame never waits for a subscriber.
    private void OnGizmoEvent(RuntimeGizmoEvent gizmoEvent)
    {
        this.gizmoEvents.Enqueue(gizmoEvent);
        _ = Task.Run(this.PublishGizmoEvents, CancellationToken.None);
    }

    private void PublishGizmoEvents()
    {
        do
        {
            if (Interlocked.CompareExchange(ref this.publishingGizmoEvents, 1, 0) != 0)
            {
                return;
            }

            try
            {
                while (this.gizmoEvents.TryDequeue(out var gizmoEvent))
                {
                    if (this.GizmoEvent is { } handlers)
                    {
                        var args = new RuntimeGizmoEventArgs(gizmoEvent);
                        foreach (EventHandler<RuntimeGizmoEventArgs> handler in handlers.GetInvocationList())
                        {
                            this.NotifySubscriber(() => handler(this, args));
                        }
                    }
                }
            }
            finally
            {
                Volatile.Write(ref this.publishingGizmoEvents, 0);
            }
        }
        while (!this.gizmoEvents.IsEmpty);
    }
}
