// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Text.Json;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Reads the SDK build receipt without loading the mixed-mode Interop assembly.</summary>
public static class NativeSdkMetadata
{
    /// <summary>The assembly metadata key emitted by the normal Interop build.</summary>
    public const string MetadataKey = "Oxygen.NativeSdk";

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        UnmappedMemberHandling = System.Text.Json.Serialization.JsonUnmappedMemberHandling.Disallow,
        RespectNullableAnnotations = true,
        RespectRequiredConstructorParameters = true,
    };

    /// <summary>Reads the SDK receipt from a protected Interop binary.</summary>
    /// <param name="stream">The readable PE image; the caller retains ownership.</param>
    /// <returns>The SDK files recorded when Interop was compiled.</returns>
    public static NativeBuildReceipt Read(Stream stream)
    {
        using var image = new PEReader(stream, PEStreamOptions.LeaveOpen);
        var metadata = image.GetMetadataReader();
        foreach (var handle in metadata.GetAssemblyDefinition().GetCustomAttributes())
        {
            var attribute = metadata.GetCustomAttribute(handle);
            if (attribute.Constructor.Kind != HandleKind.MemberReference)
            {
                continue;
            }

            var constructor = metadata.GetMemberReference((MemberReferenceHandle)attribute.Constructor);
            if (constructor.Parent.Kind != HandleKind.TypeReference)
            {
                continue;
            }

            var type = metadata.GetTypeReference((TypeReferenceHandle)constructor.Parent);
            if (!string.Equals(metadata.GetString(type.Namespace), "System.Reflection", StringComparison.Ordinal) || !string.Equals(metadata.GetString(type.Name), "AssemblyMetadataAttribute", StringComparison.Ordinal))
            {
                continue;
            }

            var blob = metadata.GetBlobReader(attribute.Value);
            if (blob.ReadUInt16() == 1 && string.Equals(blob.ReadSerializedString(), MetadataKey, StringComparison.Ordinal))
            {
                return JsonSerializer.Deserialize<NativeBuildReceipt>(blob.ReadSerializedString() ?? string.Empty, JsonOptions)
                    ?? throw new InvalidDataException("Interop contains an empty native SDK receipt. Rebuild Interop.");
            }
        }

        throw new InvalidDataException("Interop has no native SDK build receipt. Rebuild Interop once to record its SDK.");
    }
}
