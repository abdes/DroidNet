// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Status;

/// <summary>Observed output availability; payload integrity belongs to native validation.</summary>
public enum CookedOutputAvailability
{
    /// <summary>Current publication or index metadata could not be read.</summary>
    Unknown,

    /// <summary>The published descriptors and shared files are present.</summary>
    Present,

    /// <summary>An output or required file is absent from the published generation.</summary>
    Missing,
}
