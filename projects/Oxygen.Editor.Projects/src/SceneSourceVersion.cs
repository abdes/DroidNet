// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;

namespace Oxygen.Editor.Projects;

/// <summary>The persisted scene destination and content version acknowledged by project storage.</summary>
/// <param name="SourcePath">The source path returned by the storage provider.</param>
/// <param name="Version">The version accepted after loading or successfully saving this scene.</param>
public sealed record SceneSourceVersion(string SourcePath, FileVersion Version);
