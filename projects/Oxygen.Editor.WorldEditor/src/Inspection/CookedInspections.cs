// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.World.Inspection;

/// <summary>Sets what the window's Cooked Inspector inspects and brings it forward.</summary>
/// <param name="inspector">The window's Cooked Inspector.</param>
/// <param name="projects">The active project, which the inspection reads.</param>
/// <param name="placement">Where panels show.</param>
public sealed class CookedInspections(CookedInspectionViewModel inspector, IProjectContextService projects, IPlacementService placement) : ICookedInspections
{
    /// <summary>The Cooked Inspector panel's identity.</summary>
    public const string PanelId = "inspect";

    /// <inheritdoc />
    public async Task<bool> InspectAsync(CookedInspectionRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (projects.ActiveProject is not { } project)
        {
            return false;
        }

        _ = inspector.InspectAsync(new CookedInspectionScope(project, request.Scope, request.Validate)
        {
            Asset = request.Asset,
            CookedSource = request.CookedSource,
        });
        _ = await placement.ShowPanelAsync(PanelId).ConfigureAwait(true);
        return true;
    }
}
