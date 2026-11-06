// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Finds the model import record of a project's model, at the model's identity.</summary>
public static class ModelRecordLookup
{
    /// <summary>Gets the record of a model.</summary>
    /// <param name="project">The model's project.</param>
    /// <param name="model">The model's identity, such as <c>/Content/Vehicles/Car.glb</c>.</param>
    /// <returns>The record.</returns>
    /// <exception cref="InvalidDataException">The model has no record: it was never imported.</exception>
    public static ModelImportRecord Find(ProjectContext project, AssetPath model)
        => TryFind(project, model) ?? throw new InvalidDataException($"The model '{model}' has no import record. Import it first.");

    /// <summary>Gets the record of a model, if any.</summary>
    /// <param name="project">The model's project.</param>
    /// <param name="model">The model's identity, such as <c>/Content/Vehicles/Car.glb</c>.</param>
    /// <returns>The record, or <see langword="null"/> when the model is not imported.</returns>
    public static ModelImportRecord? TryFind(ProjectContext project, AssetPath model)
        => ModelImportRecords.TryRead(project, model);
}
