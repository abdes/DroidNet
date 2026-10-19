// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>A relocation, copy or delete was rejected before changing any file; the message tells the user why.</summary>
public sealed class AssetRelocationException : Exception
{
    /// <summary>The code of a refused rename, move or delete of one product of an imported model.</summary>
    /// <remarks>A model product is regenerated from its model on every cook and has no authored place of its own.</remarks>
    public const string ModelProductCode = "relocation.model_product";

    /// <summary>Initializes a new instance of the <see cref="AssetRelocationException"/> class.</summary>
    public AssetRelocationException()
    {
    }

    /// <summary>Initializes a new instance of the <see cref="AssetRelocationException"/> class.</summary>
    /// <param name="message">Why the operation was rejected.</param>
    public AssetRelocationException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="AssetRelocationException"/> class.</summary>
    /// <param name="message">Why the operation was rejected.</param>
    /// <param name="innerException">The underlying failure.</param>
    public AssetRelocationException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="AssetRelocationException"/> class with a code.</summary>
    /// <param name="code">The refusal's code, such as <see cref="ModelProductCode"/>.</param>
    /// <param name="message">Why the operation was rejected.</param>
    public AssetRelocationException(string code, string message)
        : base(message)
    {
        this.Code = code;
    }

    /// <summary>Gets the refusal's code, or null for a refusal the message alone describes.</summary>
    public string? Code { get; }

    /// <summary>Refuses an operation on one product of an imported model.</summary>
    /// <param name="product">The product's identity.</param>
    /// <param name="model">The model it is named under.</param>
    /// <returns>The refusal.</returns>
    internal static AssetRelocationException ModelProduct(Oxygen.Managed.Core.AssetPath product, Oxygen.Managed.Core.AssetPath model)
        => new(ModelProductCode, $"'{product.Name}' is part of the imported model '{model.Name}' and is regenerated from it. Rename, move or delete the model instead.");
}
