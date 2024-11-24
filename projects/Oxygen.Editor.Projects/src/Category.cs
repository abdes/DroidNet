// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Resources;

namespace Oxygen.Editor.Projects;

/// <summary>
/// Represents a category for a project within the Oxygen Editor.
/// </summary>
/// <remarks>
/// The <see cref="Category"/> class defines a category for projects within the Oxygen Editor. It
/// includes properties for the category's ID, name, and description. The class also provides
/// methods for retrieving categories by ID and for JSON serialization and deserialization.
/// </remarks>
[SuppressMessage("ReSharper", "MemberCanBePrivate.Global", Justification = "we provide public shortcuts for the known categories")]
public class Category(string id, string name, string description)
{
    /// <summary>
    /// Represents the category for games.
    /// </summary>
    public static readonly Category Games = new("C44E7604-B265-40D8-9442-11A01ECE334C", "PROJ_Category_Games", "PROJ_Category_Games_Description");

    /// <summary>
    /// Represents the category for visualization projects.
    /// </summary>
    public static readonly Category Visualization = new("D88D97B6-9F2A-4EF5-8137-CD6709CA1233", "PROJ_Category_Visualization", "PROJ_Category_Visualization_Description");

    /// <summary>
    /// Represents the miscellaneous category.
    /// </summary>
    public static readonly Category Misc = new("892D3C51-72C0-47DD-AF32-65CA63EEDDFE", "PROJ_Category_Misc", "PROJ_Category_Misc_Description");

    private static readonly Category[] Categories = [Games, Visualization, Misc,];
    private static readonly Lazy<Dictionary<string, Category>> LazyCategoriesDictionary =
        new(() => Categories.ToDictionary(category => category.Id, category => category, StringComparer.OrdinalIgnoreCase));

    /// <summary>
    /// Gets the ID of the project category.
    /// </summary>
    public string Id { get; } = id;

    /// <summary>
    /// Gets the localized name of the project category.
    /// </summary>
    public string Name { get; } = name.GetLocalizedMine();

    /// <summary>
    /// Gets the localized description of the project category.
    /// </summary>
    public string Description { get; } = description.GetLocalizedMine();

    private static Dictionary<string, Category> CategoriesDictionary => LazyCategoriesDictionary.Value;

    /// <summary>Get the project category with the given ID.</summary>
    /// <param name="id">The category ID.</param>
    /// <returns>
    /// A valid <see cref="Category"/> object if there is one with the given ID; otherwise, <see langword="null"/>.
    /// </returns>
    public static Category? ById(string id) => CategoriesDictionary.GetValueOrDefault(id);
}
