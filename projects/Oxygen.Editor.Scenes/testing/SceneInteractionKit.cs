// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes.Testing;

/// <summary>
/// The one way a test builds the real <see cref="SceneNodeInteraction"/>: over an in-memory settings store, the structures of
/// the sources a test publishes, and a publisher that keeps what it is given.
/// </summary>
/// <remarks>
/// Tests that only need lock and hide state as a dependency use <see cref="FakeNodeInteraction"/>. A service that takes the
/// concrete <see cref="SceneNodeInteraction"/>, or a test of its persistence, activation or lock rule, uses this kit. Services
/// the kit creates share its settings, so a second <see cref="Create"/> is a restart of the editor over the same stored state.
/// </remarks>
/// <remarks>Initializes a new instance of the <see cref="SceneInteractionKit"/> class.</remarks>
/// <param name="structures">The structures the services read; by default, new ones with nothing published.</param>
public sealed partial class SceneInteractionKit(FakeSceneStructures? structures = null)
{
    /// <summary>Gets the settings the services load from and save to.</summary>
    public InMemoryEditorSettings Settings { get; } = new();

    /// <summary>Gets the structures the services read, unless a service is created over others.</summary>
    public FakeSceneStructures Structures { get; } = structures ?? new FakeSceneStructures();

    /// <summary>Gets the operation results the services published to the kit's own publisher, in order.</summary>
    public IReadOnlyList<OperationResult> Published => this.Results.Published;

    private CapturingResults Results { get; } = new();

    /// <summary>Creates the service of a project session.</summary>
    /// <param name="session">The project session whose scenes the state belongs to.</param>
    /// <param name="results">Receives the service's operation results; by default, the kit's own publisher (<see cref="Published"/>).</param>
    /// <param name="status">The diagnostic status reducer; by default, a new one.</param>
    /// <param name="structures">What the instances contain; by default, the kit's <see cref="Structures"/>.</param>
    /// <returns>The service; its state is empty until it restores a scene.</returns>
    public SceneNodeInteraction Create(
        IProjectSession session,
        IOperationResultPublisher? results = null,
        IStatusReducer? status = null,
        ISceneStructures? structures = null)
    {
        ArgumentNullException.ThrowIfNull(session);
        return new SceneNodeInteraction(
            session,
            this.Settings,
            results ?? this.Results,
            status ?? new OperationStatusReducer(),
            structures ?? this.Structures);
    }

    private sealed partial class CapturingResults : IOperationResultPublisher
    {
        public List<OperationResult> Published { get; } = [];

        public void Publish(OperationResult result) => this.Published.Add(result);

        public IDisposable Subscribe(IObserver<OperationResult> observer) => new NoSubscription();

        private sealed partial class NoSubscription : IDisposable
        {
            public void Dispose()
            {
            }
        }
    }
}
