// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>
/// The model import records of the project's models, which an edit reads while it validates and asks the content pipeline
/// to replace when it applies, on the scene thread: <i>Reassign</i> repairs a kept change in its model's import settings
/// and reimports the model (scene instances 6.4).
/// </summary>
public interface IModelImportRecordFiles
{
    /// <summary>Reads a model's import record as it is, or as a replacement already requested makes it.</summary>
    /// <param name="model">The model's source, such as <c>/Content/Vehicles/Car.glb</c>.</param>
    /// <returns>The record's bytes, or <see langword="null"/> when no readable record of the project names the model.</returns>
    public byte[]? Read(AssetPath model);

    /// <summary>
    /// Requests that a model's import record, when it still holds <paramref name="expected"/>, take
    /// <paramref name="replacement"/>, and that the model be reimported; the content pipeline does both later, as one
    /// operation, and leaves a record that changed since as it is.
    /// </summary>
    /// <param name="model">The model's source.</param>
    /// <param name="expected">The bytes the record holds now.</param>
    /// <param name="replacement">The record's new bytes.</param>
    public void Replace(AssetPath model, ReadOnlyMemory<byte> expected, ReadOnlyMemory<byte> replacement);
}
