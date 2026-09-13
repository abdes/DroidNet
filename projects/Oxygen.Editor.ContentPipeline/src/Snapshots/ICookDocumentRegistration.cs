// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Owns one document's capture callback and live authoring-state projection.</summary>
public interface ICookDocumentRegistration : IDisposable
{
    /// <summary>Publishes current owner state without acquiring a save gate or reading source files.</summary>
    /// <param name="state">Immutable facts for this registration's document and source.</param>
    /// <remarks>Retired registrations ignore late updates. Updating state never schedules cooking.</remarks>
    public void UpdateState(CookDocumentState state);
}
