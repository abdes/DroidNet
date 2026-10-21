// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Testing;

/// <summary>
/// A project session for a test: open for one project until the test ends it, as the editor's session is from the
/// project's activation until it closes.
/// </summary>
/// <remarks>
/// A service of the session receives it instead of the editor's session; <see cref="ChangeContext"/> replaces its
/// project as a mount change does, and <see cref="End"/> cancels its lifetime, as closing the session does before it
/// disposes anything.
/// </remarks>
public sealed class TestSession : IProjectSession, IDisposable
{
    private readonly CancellationTokenSource lifetime = new();

    /// <summary>Initializes a new instance of the <see cref="TestSession"/> class.</summary>
    /// <param name="context">The session's project, usually a <see cref="TestProject"/>'s.</param>
    public TestSession(ProjectContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        this.Context = context;
        this.Settings = context.Settings;
        this.Project = new Project(TestProject.InfoOf(context));
        this.Lifetime = this.lifetime.Token;
    }

    /// <inheritdoc/>
    public event EventHandler? ContextChanged;

    /// <inheritdoc/>
    public event EventHandler? SettingsChanged;

    /// <inheritdoc/>
    public ProjectContext Context { get; private set; }

    /// <inheritdoc/>
    public ProjectSettings Settings { get; private set; }

    /// <inheritdoc/>
    public IProject Project { get; }

    /// <inheritdoc/>
    public CancellationToken Lifetime { get; }

    /// <summary>Gives the session's project new content mounts, as a mount change does, then reports it.</summary>
    /// <param name="next">The same project, with its new mounts.</param>
    public void ChangeContext(ProjectContext next)
    {
        ArgumentNullException.ThrowIfNull(next);
        this.Context = next;
        this.ContextChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <inheritdoc/>
    public void UpdateSettings(ProjectSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        this.Settings = settings;
        this.SettingsChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Ends the session's lifetime, as closing the session does.</summary>
    public void End() => this.lifetime.Cancel();

    /// <inheritdoc/>
    public void Dispose() => this.lifetime.Dispose();
}
