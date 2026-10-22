// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;

namespace DroidNet.Mvvm;

/// <summary>
/// The views that one host shows for its view models: one view per view-model instance, kept for as long as that
/// view model stays in the host.
/// </summary>
/// <remarks>
/// <para>
/// A view model often outlives the place that shows it: a dock switches between its dockables, a router outlet gets
/// its template applied again. A host that converted its view model every time would build a new view each time, and
/// every discarded view would stay alive, and keep reacting, for as long as anything it subscribed to on the view model
/// does. A host keeps its views here instead, and converts a view model only the first time it shows it.
/// </para>
/// <para>
/// Each host owns its own instance. A view is an element with a single parent, so a view model shown by two hosts has
/// one view in each; one store shared between hosts would put one view in two places.
/// </para>
/// <para>
/// A host calls <see cref="RetainOnly"/> when view models leave it (a closed dockable, a new outlet view model), never
/// from <c>Unloaded</c>: releasing a view clears its <see cref="IViewFor.ViewModel"/>, a XAML property, and a closing
/// window raises <c>Unloaded</c> while its XAML is being torn down.
/// </para>
/// </remarks>
public sealed class HostedViews
{
    private readonly Dictionary<object, object> views = new(ReferenceEqualityComparer.Instance);

    /// <summary>
    /// Gets the view this host already shows for <paramref name="viewModel"/>, or converts the view model to a new
    /// view and keeps it.
    /// </summary>
    /// <param name="viewModel">The view model to show.</param>
    /// <param name="converter">
    /// The converter that makes a view when the host has none for <paramref name="viewModel"/>; a view already made is
    /// kept when the converter changes.
    /// </param>
    /// <returns>
    /// The view for <paramref name="viewModel"/>, or <see langword="null"/> when there is no converter or it made no
    /// view; nothing is kept then, so a later call can convert again.
    /// </returns>
    public object? GetOrCreate(object viewModel, IValueConverter? converter)
    {
        if (this.views.TryGetValue(viewModel, out var view))
        {
            return view;
        }

        view = converter?.Convert(viewModel, typeof(object), parameter: null, language: null);
        if (view is not null)
        {
            this.views.Add(viewModel, view);
        }

        return view;
    }

    /// <summary>
    /// Releases the view of every view model that is not in <paramref name="viewModels"/>: the host forgets it and, when
    /// it is an <see cref="IViewFor"/> still showing that view model, clears its view model so the view lets go of it.
    /// </summary>
    /// <param name="viewModels">The view models that stay in the host.</param>
    public void RetainOnly(IEnumerable<object?> viewModels)
    {
        var kept = new HashSet<object>(ReferenceEqualityComparer.Instance);
        foreach (var viewModel in viewModels)
        {
            if (viewModel is not null)
            {
                _ = kept.Add(viewModel);
            }
        }

        foreach (var (viewModel, view) in this.views.Where(entry => !kept.Contains(entry.Key)).ToList())
        {
            _ = this.views.Remove(viewModel);
            if (view is IViewFor viewFor && ReferenceEquals(viewFor.ViewModel, viewModel))
            {
                viewFor.ViewModel = null;
            }
        }
    }
}
