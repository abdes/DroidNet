// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Problems;

/// <summary>
/// What the Problems panel does outside its own view: its tab shows the problem counts, it comes forward when the
/// running scene reports an error, and when another part asks it to show a problem.
/// </summary>
/// <remarks>
/// A runtime warning waits to be looked at; saves of the files do not bring the panel forward, so the code editor keeps
/// focus.
/// </remarks>
public sealed partial class ProblemsPresenter : IDisposable
{
    private readonly ProblemsViewModel model;
    private readonly IPlacementService placement;
    private readonly IRuntimeProblemSource runtime;
    private readonly IMessenger messenger;
    private int shownRuntimeProblems;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ProblemsPresenter"/> class.</summary>
    /// <param name="model">The panel's view model.</param>
    /// <param name="placement">Where panels show.</param>
    /// <param name="runtime">The running scene's problems.</param>
    /// <param name="messenger">The window's messenger, which carries requests to show a problem.</param>
    public ProblemsPresenter(ProblemsViewModel model, IPlacementService placement, IRuntimeProblemSource runtime, IMessenger messenger)
    {
        ArgumentNullException.ThrowIfNull(model);
        ArgumentNullException.ThrowIfNull(placement);
        ArgumentNullException.ThrowIfNull(runtime);
        ArgumentNullException.ThrowIfNull(messenger);
        this.model = model;
        this.placement = placement;
        this.runtime = runtime;
        this.messenger = messenger;

        placement.SetPanelTitle(ProblemsModule.PanelId, model.Title);
        model.PropertyChanged += this.OnModelPropertyChanged;
        runtime.ProblemsChanged += this.OnRuntimeProblemsChanged;
        messenger.Register<ShowProblemMessage>(this, (_, message) => this.Show(message));
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.model.PropertyChanged -= this.OnModelPropertyChanged;
        this.runtime.ProblemsChanged -= this.OnRuntimeProblemsChanged;
        this.messenger.UnregisterAll(this);
    }

    private void Show(ShowProblemMessage message)
    {
        _ = this.model.Select(message.Problem);
        _ = this.placement.ShowPanelAsync(ProblemsModule.PanelId);
    }

    private void OnModelPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(ProblemsViewModel.Title), StringComparison.Ordinal))
        {
            this.placement.SetPanelTitle(ProblemsModule.PanelId, this.model.Title);
        }
    }

    // A runtime error brings Problems forward; warnings wait to be looked at.
    private void OnRuntimeProblemsChanged(object? sender, EventArgs args)
    {
        var problems = this.runtime.Problems;
        var hasError = problems.Count > this.shownRuntimeProblems
            && problems.Skip(this.shownRuntimeProblems).Any(static problem => problem.Severity >= DiagnosticSeverity.Error);
        this.shownRuntimeProblems = problems.Count;
        if (hasError)
        {
            _ = this.placement.ShowPanelAsync(ProblemsModule.PanelId);
        }
    }
}
