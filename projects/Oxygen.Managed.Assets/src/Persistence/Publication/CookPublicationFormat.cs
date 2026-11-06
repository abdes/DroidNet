// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;

namespace Oxygen.Managed.Assets.Persistence.Publication;

/// <summary>
/// The layout of a project's <c>.cooked</c> folder, and the reading of what selects a generation in it, so a project can
/// mount another project's output. The content pipeline writes this format; the engine never reads it.
/// </summary>
public static class CookPublicationFormat
{
    /// <summary>The name of the file that names the current publication.</summary>
    public const string HeadFileName = "head.json";

    /// <summary>The folder that holds the publication documents.</summary>
    public const string PublicationsFolder = "publications";

    /// <summary>The folder that holds the generations.</summary>
    public const string GenerationsFolder = "generations";

    /// <summary>The publication document format this editor reads and writes.</summary>
    public const int DocumentVersion = 8;

    private const int ProjectOwner = 0;

    private static readonly JsonSerializerOptions HeadOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        RespectNullableAnnotations = true,
        RespectRequiredConstructorParameters = true,
    };

    /// <summary>Gets the path of a publication's document below the <c>.cooked</c> folder.</summary>
    /// <param name="publicationId">The publication.</param>
    /// <returns>The path, with <c>/</c> separators.</returns>
    public static string DocumentPath(Guid publicationId) => $"{PublicationsFolder}/{publicationId:N}.json";

    /// <summary>Gets the path of a generation below the <c>.cooked</c> folder.</summary>
    /// <param name="sourceKey">The generation's source key.</param>
    /// <returns>The path, with <c>/</c> separators.</returns>
    public static string GenerationPath(Guid sourceKey) => $"{GenerationsFolder}/{sourceKey:N}";

    /// <summary>Reads a head.</summary>
    /// <param name="content">The file's bytes.</param>
    /// <returns>The head.</returns>
    /// <exception cref="InvalidDataException">The head is not one this editor reads.</exception>
    public static CookPublicationHead ReadHead(ReadOnlySpan<byte> content)
    {
        CookPublicationHead? head;
        try
        {
            head = JsonSerializer.Deserialize<CookPublicationHead>(content, HeadOptions);
        }
        catch (JsonException exception)
        {
            throw new InvalidDataException("The publication head is not valid.", exception);
        }

        return head is { Version: CookPublicationHead.CurrentVersion }
            ? head
            : throw new InvalidDataException($"The publication head is not in format {CookPublicationHead.CurrentVersion}.");
    }

    /// <summary>Selects the generation of a mount root from the document a head names.</summary>
    /// <param name="head">The head.</param>
    /// <param name="document">The document's bytes.</param>
    /// <param name="root">The mount root.</param>
    /// <returns>The source key of the root's generation, or <see langword="null"/> when the publication has no such root.</returns>
    /// <exception cref="InvalidDataException">The document is not the one the head names, or not one this editor reads.</exception>
    public static Guid? SelectGeneration(CookPublicationHead head, byte[] document, string root)
    {
        ArgumentNullException.ThrowIfNull(head);
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(root);
        if (!string.Equals(Convert.ToHexString(SHA256.HashData(document)), head.DocumentSha256, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("The publication document is not the one its head names.");
        }

        try
        {
            using var parsed = JsonDocument.Parse(document);
            var version = parsed.RootElement.GetProperty("version").GetInt32();
            if (version != DocumentVersion)
            {
                throw new InvalidDataException($"The publication document is in format {version}; this editor reads format {DocumentVersion}.");
            }

            foreach (var entry in parsed.RootElement.GetProperty("roots").EnumerateArray())
            {
                if (entry.GetProperty("owner").GetInt32() == ProjectOwner
                    && string.Equals(entry.GetProperty("name").GetString(), root, StringComparison.Ordinal))
                {
                    return entry.GetProperty("sourceKey").GetGuid();
                }
            }

            return null;
        }
        catch (Exception exception) when (exception is JsonException or KeyNotFoundException or FormatException or InvalidOperationException)
        {
            throw new InvalidDataException("The publication document is not valid.", exception);
        }
    }
}
