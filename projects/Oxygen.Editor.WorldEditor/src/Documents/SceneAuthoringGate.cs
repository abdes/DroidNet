// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;

namespace Oxygen.Editor.World.Documents;

/// <summary>Drains mutations before replacing a scene and rejects callbacks for retired models.</summary>
internal static partial class SceneAuthoringGate
{
    private static readonly ConditionalWeakTable<Scene, State> States = [];
    private static readonly AsyncLocal<Operation?> Current = new();

    /// <summary>Enters a mutation for the captured scene model.</summary>
    /// <param name="scene">The model captured by the operation.</param>
    /// <returns>A lease, or null when independent input is suspended or the model was retired.</returns>
    public static Operation? TryEnter(Scene scene)
    {
        var state = States.GetValue(scene, _ => new());
        lock (state.Sync)
        {
            if (state.Retired || (state.Reloading && !HasActiveParent(state)))
            {
                return null;
            }

            if (state.Active++ == 0)
            {
                state.Idle = new(TaskCreationOptions.RunContinuationsAsynchronously);
            }

            var operation = new Operation(state, Current.Value);
            Current.Value = operation;
            return operation;
        }
    }

    /// <summary>Rejects subsequent commands when an authoring owner is disposed.</summary>
    /// <param name="scene">The model whose owner ended.</param>
    public static void Retire(Scene scene)
    {
        var state = States.GetValue(scene, _ => new());
        lock (state.Sync)
        {
            state.Retired = true;
        }
    }

    /// <summary>Determines whether a captured model has lost its authoring owner.</summary>
    /// <param name="scene">The captured model.</param>
    /// <returns>True when the model was retired.</returns>
    public static bool IsRetired(Scene scene)
    {
        if (!States.TryGetValue(scene, out var state))
        {
            return false;
        }

        lock (state.Sync)
        {
            return state.Retired;
        }
    }

    /// <summary>Suspends new input and waits for admitted mutations to finish.</summary>
    /// <param name="scene">The model to replace.</param>
    /// <param name="cancellationToken">Cancels waiting without retiring the model.</param>
    /// <returns>The replacement lease, or null if another replacement owns the scene.</returns>
    public static async Task<Replacement?> BeginReplacementAsync(Scene scene, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var state = States.GetValue(scene, _ => new());
        Task pending;
        lock (state.Sync)
        {
            if (state.Retired || state.Reloading || HasActiveParent(state))
            {
                return null;
            }

            state.Reloading = true;
            pending = state.Active == 0 ? Task.CompletedTask : state.Idle.Task;
        }

        try
        {
            await pending.WaitAsync(cancellationToken).ConfigureAwait(true);
            cancellationToken.ThrowIfCancellationRequested();
            return new Replacement(state);
        }
        catch
        {
            lock (state.Sync)
            {
                state.Reloading = false;
            }

            throw;
        }
    }

    private static bool HasActiveParent(State state)
    {
        for (var operation = Current.Value; operation is not null; operation = operation.Parent)
        {
            if (ReferenceEquals(operation.Owner, state) && !operation.Disposed)
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>Owns an admitted mutation, including its asynchronous continuation.</summary>
    internal sealed partial class Operation(State owner, Operation? parent) : IDisposable
    {
        /// <summary>Gets the scene's shared mutation state.</summary>
        internal State Owner { get; } = owner;

        /// <summary>Gets the enclosing operation in this execution context.</summary>
        internal Operation? Parent { get; } = parent;

        /// <summary>Gets a value indicating whether this operation has finished.</summary>
        internal bool Disposed { get; private set; }

        /// <inheritdoc/>
        public void Dispose()
        {
            lock (this.Owner.Sync)
            {
                if (this.Disposed)
                {
                    return;
                }

                this.Disposed = true;
                if (ReferenceEquals(Current.Value, this))
                {
                    Current.Value = this.Parent;
                }

                if (--this.Owner.Active == 0)
                {
                    _ = this.Owner.Idle.TrySetResult();
                }
            }
        }
    }

    /// <summary>Keeps input suspended until replacement succeeds or is abandoned.</summary>
    internal sealed partial class Replacement(State owner) : IDisposable
    {
        private bool disposed;

        /// <summary>Retires the old model after its replacement has been accepted.</summary>
        public void Retire()
        {
            lock (owner.Sync)
            {
                ObjectDisposedException.ThrowIf(this.disposed, this);
                owner.Retired = true;
            }
        }

        /// <inheritdoc/>
        public void Dispose()
        {
            lock (owner.Sync)
            {
                if (this.disposed)
                {
                    return;
                }

                this.disposed = true;
                owner.Reloading = false;
            }
        }
    }

    /// <summary>Coordinates operations admitted against one scene instance.</summary>
    internal sealed class State
    {
        /// <summary>Gets the synchronization object for admission and completion.</summary>
        public object Sync { get; } = new();

        /// <summary>Gets or sets the number of admitted operations.</summary>
        public int Active { get; set; }

        /// <summary>Gets or sets a value indicating whether a replacement owns admission.</summary>
        public bool Reloading { get; set; }

        /// <summary>Gets or sets a value indicating whether the model has been replaced.</summary>
        public bool Retired { get; set; }

        /// <summary>Gets or sets completion of the current admitted operations.</summary>
        public TaskCompletionSource Idle { get; set; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
}
