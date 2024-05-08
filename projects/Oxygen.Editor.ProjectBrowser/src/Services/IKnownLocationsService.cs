// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Storage;

using Oxygen.Editor.ProjectBrowser.Projects;

public interface IKnownLocationsService
{
    Task<KnownLocation?> ForKeyAsync(KnownLocations key, CancellationToken cancellationToken = default);
}
