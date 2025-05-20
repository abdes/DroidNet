

```cpp
// Example 1: Basic texture registration and view creation
void SetupBindlessTextures(ResourceRegistry& registry, std::vector<std::shared_ptr<Texture>> textures) {
    // Register all textures
    std::vector<uint32_t> texture_ids;
    for (const auto& texture : textures) {
        texture_ids.push_back(registry.RegisterTexture(texture));
    }

    // Create SRVs for all textures
    std::vector<DescriptorHandle> srv_handles;
    for (uint32_t texture_id : texture_ids) {
        TextureViewKey view_key{
            .binding_type = ResourceViewType::kTexture_SRV,
            .format = Format::RGBA8_UNORM,
            .dimension = TextureDimension::TEX2D
        };

        srv_handles.push_back(registry.CreateTextureView(texture_id, view_key));
    }

    // Pass indices to material system
    for (size_t i = 0; i < textures.size(); i++) {
        uint32_t srv_index = srv_handles[i].GetIndex();
        MaterialSystem::RegisterTextureIndex(textures[i]->GetName(), srv_index);
    }
}

// Example 2: Bindless rendering setup
void RenderWithBindlessTextures(CommandList& cmd_list, ResourceRegistry& registry) {
    // Prepare descriptor allocator for rendering
    registry.GetDescriptorAllocator()->PrepareForRendering(cmd_list.GetNativeObject());

    // Bind root signature with descriptor table
    cmd_list.SetRootSignature(bindless_root_signature_);

    // Set our descriptor heap as the base for the descriptor table
    // All texture indices are offsets from this base
    cmd_list.SetGraphicsRootDescriptorTable(0, registry.GetDescriptorAllocator()->GetGPUDescriptorTableBase());

    // Draw objects - each material references textures by index in constant buffer
    for (auto& material : materials_) {
        MaterialConstants constants;
        constants.albedo_texture_index = material.GetAlbedoTextureIndex();
        constants.normal_texture_index = material.GetNormalTextureIndex();

        // Upload constants
        cmd_list.SetGraphicsRoot32BitConstants(1, sizeof(constants)/4, &constants, 0);

        // Draw mesh
        cmd_list.DrawIndexed(material.GetMesh());
    }
}

// Example 3: Complete resource setup with multiple view types
void SetupCompleteBindlessPipeline(ResourceRegistry& registry) {
    // Register a texture
    auto albedo = CreateTexture("albedo.png");
    uint32_t albedo_id = registry.RegisterTexture(albedo);

    // Create multiple views of the same texture
    TextureViewKey srv_key{
        .binding_type = ResourceViewType::kTexture_SRV,
        .format = Format::RGBA8_UNORM,
        .dimension = TextureDimension::TEX2D
    };

    TextureViewKey uav_key{
        .binding_type = ResourceViewType::kTexture_UAV,
        .format = Format::RGBA8_UNORM,
        .dimension = TextureDimension::TEX2D,
        .mip_slice = 0
    };

    // Get handles for both view types
    DescriptorHandle srv_handle = registry.CreateTextureView(albedo_id, srv_key);
    DescriptorHandle uav_handle = registry.CreateTextureView(albedo_id, uav_key,
        DescriptorVisibility::kShaderVisible);

    // Register a buffer
    auto vertex_buffer = CreateBuffer(1024 * sizeof(Vertex), BufferUsage::kVertex);
    uint32_t buffer_id = registry.RegisterBuffer(vertex_buffer);

    // Create SRV for the buffer (for use in compute shader)
    BufferViewKey buffer_srv_key{
        .binding_type = ResourceViewType::kRawBuffer_SRV,
        .format = Format::UNKNOWN,
        .offset = 0,
        .size = vertex_buffer->GetSize()
    };

    DescriptorHandle buffer_srv_handle = registry.CreateBufferView(buffer_id, buffer_srv_key);

    // Use indices in shader constants or resource tables
    ShaderResourceIndices indices;
    indices.albedo_texture = srv_handle.GetIndex();
    indices.albedo_uav = uav_handle.GetIndex();
    indices.vertex_buffer = buffer_srv_handle.GetIndex();

    // Upload indices to constant buffer
    constant_buffer_->Update(&indices, sizeof(indices));
}
```

```cpp
// Root signature for bindless texture access
// "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "
// "DescriptorTable(SRV(t0, numDescriptors = unbounded), visibility = SHADER_VISIBILITY_PIXEL), "
// "DescriptorTable(UAV(u0, numDescriptors = unbounded), visibility = SHADER_VISIBILITY_ALL), "
// "CBV(b0, visibility = SHADER_VISIBILITY_ALL)"

// Texture array declaration for bindless access
Texture2D<float4> g_Textures[] : register(t0, space0);
RWTexture2D<float4> g_RWTextures[] : register(u0, space0);
SamplerState g_Sampler : register(s0);

// Material constant buffer with texture indices
cbuffer MaterialConstants : register(b0)
{
    uint albedoTextureIndex;
    uint normalTextureIndex;
    uint roughnessTextureIndex;
    uint vertexBufferIndex;
}

// Pixel shader with bindless texture access
float4 PSMain(float2 texCoord : TEXCOORD) : SV_TARGET
{
    // Access textures by index from constant buffer
    float4 albedo = g_Textures[albedoTextureIndex].Sample(g_Sampler, texCoord);
    float3 normal = g_Textures[normalTextureIndex].Sample(g_Sampler, texCoord).xyz * 2.0 - 1.0;
    float roughness = g_Textures[roughnessTextureIndex].Sample(g_Sampler, texCoord).r;

    // Use the textures for lighting calculation
    return CalculateLighting(albedo, normal, roughness);
}

// Compute shader with bindless UAV access
[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    // Read from one texture and write to another
    float4 color = g_Textures[albedoTextureIndex].Load(int3(DTid.xy, 0));

    // Process the color
    color = ProcessColor(color);

    // Write to output
    g_RWTextures[vertexBufferIndex][DTid.xy] = color;
}
```

class BindlessRenderingSystem {
public:
    BindlessRenderingSystem(std::shared_ptr<DescriptorAllocator> allocator)
        : registry_(std::make_unique<ResourceRegistry>(std::move(allocator)))
        , constant_buffer_(CreateConstantBuffer(sizeof(MaterialConstants), BufferUsage::kConstant))
    {
        InitializeBindlessSystem();
    }

    // Register a texture and create SRV
    uint32_t RegisterTexture(std::shared_ptr<Texture> texture) {
        // Register the texture
        NativeObject texture_obj = registry_->RegisterTexture(texture);

        // Create shader resource view
        TextureViewKey srv_key{
            .binding_type = ResourceViewType::kTexture_SRV,
            .format = texture->GetFormat(),
            .dimension = TextureDimension::TEX2D
        };

        // Get descriptor handle for bindless access
        DescriptorHandle srv_handle = registry_->CreateTextureView(
            texture_obj, srv_key, DescriptorVisibility::kShaderVisible);

        // Store texture and handle mapping for later use
        uint32_t index = srv_handle.GetIndex();
        texture_handles_[texture->GetName()] = srv_handle;

        return index; // Return shader-visible index
    }

private:
    void InitializeBindlessSystem() {
        // Create root signature with unbounded descriptor tables
        RootSignatureDesc desc;

        // Table 0: SRVs (textures)
        desc.AddDescriptorTable(DescriptorRangeType::kSRV, 0,
                               UINT_MAX, /* unbounded */
                               ShaderVisibility::kPixel);

        // Table 1: Samplers
        desc.AddDescriptorTable(DescriptorRangeType::kSampler, 0,
                               8, /* fixed count */
                               ShaderVisibility::kPixel);

        // Parameter 2: CBV for material constants
        desc.AddConstantBufferView(0, ShaderVisibility::kAll);

        root_signature_ = CreateRootSignature(desc);
    }

    std::unique_ptr<ResourceRegistry> registry_;
    std::shared_ptr<Buffer> constant_buffer_;
    RootSignature root_signature_;
    std::unordered_map<std::string, DescriptorHandle> texture_handles_;
};

struct MaterialConstants {
    uint32_t albedo_texture_index;
    uint32_t normal_texture_index;
    uint32_t roughness_texture_index;
    uint32_t metallic_texture_index;
    float roughness_factor;
    float metallic_factor;
    float2 padding;
};

class Material {
public:
    Material(BindlessRenderingSystem& bindless_system)
        : bindless_system_(bindless_system)
    {
        // Initialize with default indices
        constants_.albedo_texture_index = 0;    // Default white
        constants_.normal_texture_index = 0;    // Default flat normal
        constants_.roughness_texture_index = 0; // Default roughness
        constants_.metallic_texture_index = 0;  // Default metallic
    }

    void SetAlbedoTexture(std::shared_ptr<Texture> texture) {
        uint32_t index = bindless_system_.RegisterTexture(texture);
        constants_.albedo_texture_index = index;
        textures_.albedo = texture;
    }

    void SetNormalTexture(std::shared_ptr<Texture> texture) {
        uint32_t index = bindless_system_.RegisterTexture(texture);
        constants_.normal_texture_index = index;
        textures_.normal = texture;
    }

    const MaterialConstants& GetConstants() const {
        return constants_;
    }

private:
    struct {
        std::shared_ptr<Texture> albedo;
        std::shared_ptr<Texture> normal;
        std::shared_ptr<Texture> roughness;
        std::shared_ptr<Texture> metallic;
    } textures_;

    MaterialConstants constants_;
    BindlessRenderingSystem& bindless_system_;
};
class Renderer {
public:
    void DrawMeshWithMaterial(CommandList& cmd_list,
                             const Mesh& mesh,
                             const Material& material,
                             BindlessRenderingSystem& bindless_system) {
        // 1. Get descriptor allocator ready
        auto descriptor_allocator = bindless_system.GetDescriptorAllocator();
        descriptor_allocator->PrepareForRendering(cmd_list.GetNativeObject());

        // 2. Set root signature
        cmd_list.SetGraphicsRootSignature(bindless_system.GetRootSignature());

        // 3. Set descriptor tables
        // Table 0: Set SRV heap base
        D3D12_GPU_DESCRIPTOR_HANDLE srv_heap_base =
            descriptor_allocator->GetGPUDescriptorTableBase(ResourceViewType::kTexture_SRV);
        cmd_list.SetGraphicsRootDescriptorTable(0, srv_heap_base);

        // Table 1: Set samplers (assume already set up)
        D3D12_GPU_DESCRIPTOR_HANDLE sampler_heap_base =
            descriptor_allocator->GetGPUDescriptorTableBase(ResourceViewType::kSampler);
        cmd_list.SetGraphicsRootDescriptorTable(1, sampler_heap_base);

        // 4. Update and bind constant buffer with material data
        const MaterialConstants& constants = material.GetConstants();

        // Update constant buffer
        ConstantBufferUpdate cb_update;
        cb_update.buffer = bindless_system.GetMaterialConstantBuffer();
        cb_update.data = &constants;
        cb_update.size = sizeof(MaterialConstants);
        cmd_list.UpdateConstantBuffer(cb_update);

        // Bind constants (CBV)
        cmd_list.SetGraphicsRootConstantBufferView(2, bindless_system.GetMaterialConstantBufferGpuAddress());

        // 5. Draw the mesh
        cmd_list.DrawIndexedMesh(mesh);
    }
};

void RenderScene(Scene& scene, CommandList& cmd_list, BindlessRenderingSystem& bindless_system) {
    // Prepare for rendering
    Renderer renderer;

    // For each object in the scene
    for (const auto& object : scene.GetObjects()) {
        const Mesh& mesh = object.GetMesh();
        const Material& material = object.GetMaterial();

        // Draw using the bindless system
        renderer.DrawMeshWithMaterial(cmd_list, mesh, material, bindless_system);
    }
}

// Application initialization
void InitApplication() {
    // Create descriptor allocator with appropriate configuration
    auto allocator_config = DescriptorAllocatorConfig{};
    allocator_config.heap_strategy = std::make_shared<DefaultDescriptorAllocationStrategy>();
    auto descriptor_allocator = std::make_shared<D3D12DescriptorAllocator>(device, allocator_config);

    // Create bindless rendering system
    BindlessRenderingSystem bindless_system(descriptor_allocator);

    // Load textures
    auto albedo = LoadTexture("albedo.png");
    auto normal = LoadTexture("normal.png");
    auto roughness = LoadTexture("roughness.png");

    // Create material and assign textures
    Material material(bindless_system);
    material.SetAlbedoTexture(albedo);
    material.SetNormalTexture(normal);
    material.SetRoughnessTexture(roughness);

    // Create mesh and scene object
    auto mesh = LoadMesh("model.obj");
    SceneObject object(mesh, material);

    // Add to scene
    scene.AddObject(object);
}

// Root Signature:
// "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "
// "DescriptorTable(SRV(t0, numDescriptors = unbounded), visibility = SHADER_VISIBILITY_PIXEL), "
// "DescriptorTable(Sampler(s0, numDescriptors = 8), visibility = SHADER_VISIBILITY_PIXEL), "
// "CBV(b0, visibility = SHADER_VISIBILITY_ALL)"

// Unbounded texture array for bindless access
Texture2D<float4> g_Textures[] : register(t0);

// Samplers
SamplerState g_Samplers[8] : register(s0);

// Material constants with texture indices
cbuffer MaterialConstants : register(b0)
{
    uint albedoTextureIndex;
    uint normalTextureIndex;
    uint roughnessTextureIndex;
    uint metallicTextureIndex;
    float roughnessFactor;
    float metallicFactor;
    float2 padding;
}

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    // Sample textures using their indices from constants
    float4 albedo = g_Textures[albedoTextureIndex].Sample(g_Samplers[0], input.uv);
    float3 normalMap = g_Textures[normalTextureIndex].Sample(g_Samplers[0], input.uv).xyz * 2.0 - 1.0;
    float roughness = g_Textures[roughnessTextureIndex].Sample(g_Samplers[0], input.uv).r * roughnessFactor;
    float metallic = g_Textures[metallicTextureIndex].Sample(g_Samplers[0], input.uv).r * metallicFactor;

    // Convert normal from tangent to world space
    float3x3 TBN = float3x3(input.tangent, input.bitangent, input.normal);
    float3 worldNormal = mul(normalMap, TBN);

    // Use these values for PBR lighting calculations
    return CalculatePBR(albedo, worldNormal, roughness, metallic);
}
