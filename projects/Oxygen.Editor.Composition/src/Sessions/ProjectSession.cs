// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.Sessions;

/// <summary>The session of one project, with the scope its services and windows live in.</summary>
internal sealed class ProjectSession : IProjectSession, IDisposable
{
    private readonly CancellationTokenSource lifetime = new();

    /// <summary>Initializes a new instance of the <see cref="ProjectSession" /> class.</summary>
    /// <param name="context">The project the session is opened for.</param>
    /// <param name="project">The loaded project.</param>
    public ProjectSession(ProjectContext context, IProject project)
    {
        this.Context = context;
        this.Settings = context.Settings;
        this.Project = project;

        // The token stays readable once the session is disposed.
        this.Lifetime = this.lifetime.Token;
    }

    /// <inheritdoc />
    public event EventHandler? ContextChanged;

    /// <inheritdoc />
    public event EventHandler? SettingsChanged;

    /// <inheritdoc />
    public ProjectContext Context { get; private set; }

    /// <inheritdoc />
    public ProjectSettings Settings { get; private set; }

    /// <inheritdoc />
    public IProject Project { get; }

    /// <inheritdoc />
    public CancellationToken Lifetime { get; }

    /// <summary>Gets or sets the session's scope, once it is created.</summary>
    public ScopeHandle? Scope { get; set; }

    /// <summary>Gets or sets a value indicating whether the session is closing its windows before it ends.</summary>
    public bool IsClosing { get; set; }

    /// <inheritdoc />
    public void UpdateSettings(ProjectSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        this.Settings = settings;
        this.SettingsChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Gives the session's project its new content mounts, then reports it.</summary>
    /// <param name="next">The same project, with its new mounts.</param>
    public void ChangeContext(ProjectContext next)
    {
        this.Context = next;
        this.ContextChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Cancels the session's lifetime, which stops its work in flight.</summary>
    public void End() => this.lifetime.Cancel();

    /// <inheritdoc />
    public void Dispose() => this.lifetime.Dispose();
}
