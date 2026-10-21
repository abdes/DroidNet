// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects.Testing;

/// <summary>
/// The session host for a test: it opens <see cref="TestSession"/>s, one at a time, and changes the open one's content
/// mounts as the editor's host does.
/// </summary>
/// <remarks>
/// <see cref="ChangeContext"/> refuses another project, as the editor's host does.
/// </remarks>
public sealed class TestSessionHost : IProjectSessionHost, IDisposable
{
    private TestSession? current;

    /// <inheritdoc/>
    public event EventHandler? SessionChanged;

    /// <inheritdoc/>
    public IProjectSession? Current => this.current;

    /// <summary>Gets the open session as a test drives it.</summary>
    public TestSession? Session => this.current;

    /// <summary>Opens the session of a project.</summary>
    /// <param name="context">The project.</param>
    /// <returns>The open session.</returns>
    public TestSession Open(ProjectContext context)
    {
        if (this.current is not null)
        {
            throw new InvalidOperationException("A project session is already open; close it before opening another.");
        }

        this.current = new TestSession(context);
        this.SessionChanged?.Invoke(this, EventArgs.Empty);
        return this.current;
    }

    /// <inheritdoc/>
    public Task<IProjectSession> OpenAsync(ProjectContext context, IProject project)
        => Task.FromResult<IProjectSession>(this.Open(context));

    /// <inheritdoc/>
    public void ChangeContext(ProjectContext next)
    {
        ArgumentNullException.ThrowIfNull(next);
        var session = this.current ?? throw new InvalidOperationException("Only an open project session has content mounts to change.");
        if (next.ProjectId != session.Context.ProjectId || next.ProjectRoot != session.Context.ProjectRoot)
        {
            throw new InvalidOperationException("A content mount change stays within the session's project; another project opens a new session.");
        }

        session.ChangeContext(next);
    }

    /// <inheritdoc/>
    public Task CloseAsync()
    {
        if (this.current is { } session)
        {
            this.current = null;
            session.End();
            session.Dispose();
            this.SessionChanged?.Invoke(this, EventArgs.Empty);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public void Dispose() => this.current?.Dispose();
}
