// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Problems;

/// <summary>Retarget: repairs an unresolved reference by picking the asset it should name.</summary>
public sealed partial class ProblemsViewModel
{
    private const string RetargetLabel = "Retarget";

    private IDisposable? retargetAssets;

    /// <summary>Gets the assets Retarget offers: those of the kind the broken reference names.</summary>
    public AssetPickerModel RetargetChoices { get; } = new(static asset => AssetChoice.For(asset, asset.TypeDisplayName, ""));

    /// <summary>Gets or sets a value indicating whether Retarget also rewrites the other references with the same text.</summary>
    [ObservableProperty]
    public partial bool RetargetsOthers { get; set; } = true;

    /// <summary>Gets why the last Retarget changed nothing, or empty.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasRetargetFailure))]
    public partial string RetargetFailure { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the last Retarget changed nothing.</summary>
    public bool HasRetargetFailure => this.RetargetFailure.Length != 0;

    /// <summary>Gets a value indicating whether the selected problem is a reference Retarget can rewrite.</summary>
    public bool CanRetarget => this.SelectedReference is not null;

    /// <summary>Gets the other rewritable references with the selected reference's text, which name the same path.</summary>
    public IReadOnlyList<UnresolvedReference> OtherReferences => this.SelectedReference is { } selected
        ? [.. this.sources.SelectMany(static source => source.Problems).OfType<UnresolvedReferenceProblem>()
            .Select(static problem => problem.Reference)
            .Where(other => other != selected
                && string.Equals(other.Written, selected.Written, StringComparison.Ordinal)
                && string.Equals(other.Resolved, selected.Resolved, StringComparison.Ordinal)
                && this.retarget.Relocation.CanRetarget(other))]
        : [];

    /// <summary>Gets a value indicating whether other references have the selected reference's text.</summary>
    public bool HasOtherReferences => this.OtherReferences.Count != 0;

    /// <summary>Gets what the option for the other references says.</summary>
    public string OtherReferencesText => this.OtherReferences.Count == 1
        ? "Also the 1 other reference that names this path"
        : string.Create(CultureInfo.CurrentCulture, $"Also the {this.OtherReferences.Count} other references that name this path");

    /// <summary>Gets where the other references are.</summary>
    public string OtherReferencesDetail
        => string.Join(", ", this.OtherReferences.Select(static other => $"{other.Property} in {other.File.Name}")) + ". One Undo step.";

    private UnresolvedReference? SelectedReference
        => this.Selected?.Problem is UnresolvedReferenceProblem { Reference: var reference }
            && AssetKinds.FromFileName(reference.Written) != AssetKind.Unknown
            && this.retarget.Relocation.CanRetarget(reference)
                ? reference
                : null;

    /// <summary>
    /// Fills the picker for the selected reference, before its flyout opens: a picker refuses a change of its choice lists
    /// while a flyout attaches them.
    /// </summary>
    public void BeginRetarget()
    {
        if (this.SelectedReference is not { } reference)
        {
            return;
        }

        this.EndRetarget();
        this.RetargetFailure = string.Empty;
        this.RetargetsOthers = true;
        var rule = AssetRule.For(AssetKinds.FromFileName(reference.Written));
        var context = SynchronizationContext.Current;
        this.retargetAssets = this.retarget.Assets.Items.Subscribe(assets =>
        {
            if (context is null)
            {
                this.RetargetChoices.Update(rule.Apply(assets));
            }
            else
            {
                context.Post(_ => this.RetargetChoices.Update(rule.Apply(assets)), state: null);
            }
        });
        this.RetargetChoices.SearchText = RelocationPaths.GetDisplayName(reference.Written[(reference.Written.LastIndexOf('/') + 1)..]);
        this.OnPropertyChanged(nameof(this.HasOtherReferences));
        this.OnPropertyChanged(nameof(this.OtherReferencesText));
        this.OnPropertyChanged(nameof(this.OtherReferencesDetail));
    }

    /// <summary>Stops following the assets, once the picker closed.</summary>
    public void EndRetarget()
    {
        this.retargetAssets?.Dispose();
        this.retargetAssets = null;
    }

    /// <summary>Rewrites the selected reference, and the others when asked, to name an asset, as one Undo step.</summary>
    /// <param name="target">The picked asset.</param>
    /// <returns>A task that completes when the files are rewritten or the change was refused.</returns>
    public async Task RetargetAsync(AssetPath target)
    {
        ArgumentNullException.ThrowIfNull(target);
        if (this.SelectedReference is not { } reference)
        {
            return;
        }

        IReadOnlyList<UnresolvedReference> references = this.RetargetsOthers ? [reference, .. this.OtherReferences] : [reference];
        this.RetargetFailure = string.Empty;
        var services = this.retarget;
        if (!await services.Documents.SaveBeforeAsync(SaveBeforeOperation.Retarget, _ => Task.FromResult(services.Relocation.FindUnsavedDocuments()), this.session.Lifetime).ConfigureAwait(true))
        {
            this.RetargetFailure = "Not retargeted: the documents were not saved.";
            return;
        }

        try
        {
            _ = await services.Relocation.RetargetAsync(references, target, RetargetLabel, services.Histories.GlobalHistory, this.session.Lifetime).ConfigureAwait(true);
        }
        catch (AssetRelocationException failure)
        {
            this.RetargetFailure = failure.Message;
        }
    }
}
