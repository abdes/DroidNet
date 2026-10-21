// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.CookedInspector;

/// <summary>The Cooked Inspector panel: a read-only report of the cooked output of one scope.</summary>
public sealed class CookedInspectorModule : IFeatureModule
{
    /// <summary>The Cooked Inspector panel's identity.</summary>
    public const string PanelId = CookedInspections.PanelId;

    /// <inheritdoc />
    public IReadOnlyList<PanelDescriptor> Panels { get; } = [new(PanelId, "Cooked Inspector", typeof(CookedInspectionViewModel))];

    /// <inheritdoc />
    public IReadOnlyList<CommandDescriptor> Commands { get; } = [];

    /// <inheritdoc />
    public void RegisterWindowServices(IContainer window)
    {
        ArgumentNullException.ThrowIfNull(window);

        // The inspector shows its asset links in the window's Content Browser, through the port that browser implements.
        window.RegisterDelegate(
            resolver => new CookedInspectionViewModel(
                resolver.Resolve<IContentPipelineService>(),
                resolver.Resolve<IAssetFeed>(),
                resolver.Resolve<IProjectContextService>(),
                asset => resolver.Resolve<IAssetReveal>().RevealAsync([asset])),
            Reuse.Singleton);
        window.Register<CookedInspectionView>(Reuse.Transient);
        window.Register<ICookedInspections, CookedInspections>(Reuse.Singleton);
    }

    /// <inheritdoc />
    public void ActivateWindowServices(IResolver window)
    {
        ArgumentNullException.ThrowIfNull(window);

        // The panel takes inspection requests before it is ever shown.
        _ = window.Resolve<CookedInspectionViewModel>();
    }
}
