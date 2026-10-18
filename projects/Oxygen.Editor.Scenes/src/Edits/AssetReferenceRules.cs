// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits;

/// <summary>The rules every asset reference an edit writes must follow.</summary>
internal static class AssetReferenceRules
{
    /// <summary>Checks that each reference names a file of the expected type.</summary>
    /// <param name="references">The references.</param>
    /// <param name="expectedSuffix">The file suffix of the expected asset type.</param>
    /// <returns>Why a reference is wrong, or <see langword="null"/> when all are right.</returns>
    public static string? Check(IEnumerable<AssetPath?> references, string expectedSuffix)
    {
        foreach (var reference in references)
        {
            if (reference is null)
            {
                return "A scene reference is empty.";
            }

            // Engine-generated assets, such as the built-in shapes and the default material, have no file.
            if (reference.IsGenerated)
            {
                continue;
            }

            try
            {
                _ = ContentPipelinePaths.ToNativeDescriptorPath(reference, expectedSuffix);
            }
            catch (ArgumentException error)
            {
                return error.Message;
            }
        }

        return null;
    }
}
