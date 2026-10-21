// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.CookedInspector;

/// <summary>Sets what the window's Cooked Inspector inspects and brings it forward.</summary>
/// <param name="inspector">The window's Cooked Inspector.</param>
/// <param name="session">The project session the window lives in, whose project the inspection reads.</param>
/// <param name="placement">Where panels show.</param>
public sealed class CookedInspections(CookedInspectionViewModel inspector, IProjectSession session, IPlacementService placement) : ICookedInspections
{
    /// <summary>The Cooked Inspector panel's identity.</summary>
    public const string PanelId = "inspect";

    /// <inheritdoc />
    public async Task<bool> InspectAsync(CookedInspectionRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        _ = inspector.InspectAsync(new CookedInspectionScope(session.Context,request.Scope, request.Validate)
        {
            Asset = request.Asset,
            CookedSource = request.CookedSource,
        });
        _ = await placement.ShowPanelAsync(PanelId).ConfigureAwait(true);
        return true;
    }
}
