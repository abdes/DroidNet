// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.Cooking;

/// <summary>Brings the Cooking panel forward when explicit work asks for it.</summary>
public sealed partial class CookingPresenter : IDisposable
{
    private readonly CookingPanelViewModel model;
    private readonly IPlacementService placement;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="CookingPresenter"/> class.</summary>
    /// <param name="model">The panel's view model.</param>
    /// <param name="placement">Where panels show.</param>
    public CookingPresenter(CookingPanelViewModel model, IPlacementService placement)
    {
        ArgumentNullException.ThrowIfNull(model);
        ArgumentNullException.ThrowIfNull(placement);
        this.model = model;
        this.placement = placement;
        model.RevealRequested += this.OnRevealRequested;
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.model.RevealRequested -= this.OnRevealRequested;
    }

    private void OnRevealRequested(object? sender, EventArgs args) => _ = this.placement.ShowPanelAsync(CookingModule.PanelId);
}
