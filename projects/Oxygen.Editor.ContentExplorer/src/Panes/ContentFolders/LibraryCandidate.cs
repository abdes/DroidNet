// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>One library a candidate needs, and whether what is mounted provides it.</summary>
/// <param name="Library">The needed library's name.</param>
/// <param name="Detail">How much it needs, and what it finds.</param>
/// <param name="IsMet">Whether every need is provided.</param>
public sealed record LibraryNeed(string Library, string Detail, bool IsMet);

/// <summary>
/// A library picked in the Add library form, before it is added: the name, read from its one root and never typed, its
/// form, what it needs of the libraries mounted here and where it lands among them.
/// </summary>
/// <param name="Name">The library's name.</param>
/// <param name="Form">Its form, for the form's facts.</param>
/// <param name="Read">What was read.</param>
/// <param name="Needs">What it needs.</param>
public sealed record LibraryCandidate(string Name, string Form, LibraryRead Read, IReadOnlyList<LibraryNeed> Needs)
{
    /// <summary>Gets a value indicating whether the library lacks something it needs.</summary>
    public bool HasUnmetNeed => this.Needs.Any(static need => !need.IsMet);

    /// <summary>Gets the warning about what it lacks, or empty when it lacks nothing.</summary>
    public string UnmetText
    {
        get
        {
            var unmet = string.Join(", ", this.Needs.Where(static need => !need.IsMet).Select(static need => need.Library));
            return unmet.Length == 0 ? string.Empty
                : this.Read.Container == MountContainer.Pak
                    ? $"{unmet} does not provide what this needs. {this.Name} can be added, and stays Invalid until {unmet} is added below it."
                    : $"{unmet} does not provide what this needs. {this.Name} can be added: what it cannot find fails to load until {unmet} is added below it.";
        }
    }

    /// <summary>Gets where the library lands in the content priority.</summary>
    public string PriorityText => this.Needs.Count == 0
        ? "Below the project's content"
        : $"Above {string.Join(" and ", this.Needs.Select(static need => need.Library))}, below the project's content";

    /// <summary>Reads a library that is not mounted yet.</summary>
    /// <param name="project">The project it would be added to.</param>
    /// <param name="location">The pak file, cooked folder or <c>.cooked</c> folder.</param>
    /// <param name="mounted">The libraries mounted here, by name.</param>
    /// <param name="refusal">Why it cannot be added, with the finding's code, when it cannot.</param>
    /// <returns>The candidate; <see langword="null"/> when it cannot be added.</returns>
    public static LibraryCandidate? Inspect(ProjectContext project, FileSystemPath location, IReadOnlyDictionary<string, LibraryFacts> mounted, out string? refusal)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(location);
        ArgumentNullException.ThrowIfNull(mounted);
        string? finding = null;
        var read = LibraryRead.Inspect(project.FileSystem, location, (code, message) => finding ??= $"{code}: it {message}.");
        refusal = finding ?? (read is null ? null : RefusalOf(read.Facts));
        return refusal is not null || read is null ? null : new(read.Facts.Roots[0], FormOf(read), read, NeedsOf(read.Facts, mounted));
    }

    private static string? RefusalOf(LibraryFacts facts)
        => facts.Roots.Count == 0 ? $"{MountCodes.CookedRootMismatch}: it holds nothing, so it has no name."
            : facts.Roots.Count > 1 ? $"{MountCodes.CookedRootMultiple}: it holds the roots '{string.Join("', '", facts.Roots)}'; a library has one."
            : facts.ExternalScripts.Count != 0 ? $"{MountCodes.CookedExternalScript}: it holds the script '{facts.ExternalScripts[0]}' with an external source; only a Source mount supplies one."
            : null;

    private static string FormOf(LibraryRead read)
    {
        var assets = string.Create(CultureInfo.CurrentCulture, $"{read.Facts.Assets.Count:N0} assets");
        return read.Container switch
        {
            MountContainer.Pak => string.Create(CultureInfo.CurrentCulture, $"Pak · format {PakIndex.Version} · {assets}"),
            MountContainer.Generation => $"Library project's .cooked · follows its cooks · {assets}",
            _ => $"Loose cooked root · {assets}",
        };
    }

    private static List<LibraryNeed> NeedsOf(LibraryFacts facts, IReadOnlyDictionary<string, LibraryFacts> mounted)
    {
        var needs = new List<LibraryNeed>();
        foreach (var dependency in facts.Dependencies)
        {
            var count = string.Create(CultureInfo.CurrentCulture, $"{dependency.Requirements.Count:N0} assets");
            if (string.Equals(dependency.Library, AssetPath.Engine.Mount, StringComparison.Ordinal))
            {
                needs.Add(new(dependency.Library, count + " · provided", IsMet: true));
            }
            else if (!mounted.TryGetValue(dependency.Library, out var provider))
            {
                needs.Add(new(dependency.Library, count + " · not mounted", IsMet: false));
            }
            else
            {
                var missing = dependency.Requirements.Count(requirement => !Provides(provider, requirement));
                needs.Add(new(dependency.Library, missing == 0 ? count + " · provided" : string.Create(CultureInfo.CurrentCulture, $"{count} · {missing:N0} missing"), missing == 0));
            }
        }

        return needs;
    }

    private static bool Provides(LibraryFacts provider, LibraryRequirement requirement)
        => requirement.IsTexture
            ? provider.HasTexture(requirement.Key)
            : provider.TryFindAsset(requirement.Key, out var type) && type == requirement.AssetType;
}
