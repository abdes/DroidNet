// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>
/// How a model's preview scene is lit and exposed (model editor 5.2): the preview's sky and sun, which the viewport's
/// <em>Preview environment</em> shows or hides, and the stops the viewport adds to the preview's fixed exposure.
/// </summary>
/// <param name="IsShown">Whether the preview's sky and sun light the model.</param>
/// <param name="ExposureCompensationEv">The stops added to the preview's fixed exposure; positive brightens.</param>
public sealed record PreviewEnvironment(bool IsShown, float ExposureCompensationEv)
{
    /// <summary>Gets the environment a preview starts with: its sky and sun shown, at the fixed exposure.</summary>
    public static PreviewEnvironment Default { get; } = new(IsShown: true, ExposureCompensationEv: 0f);
}
