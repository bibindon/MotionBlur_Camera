float4x4 g_matInvCurrentViewProj;
float4x4 g_matPrevViewProj;
float g_fBlurScale = 2.0f;
float g_fMaxBlurPixels = 24.0f;
int g_iDebugViewMode = 0;
int g_iMotionBlurEnabled = 1;
float4 g_vTexelSize = { 1.0f / 1600.0f, 1.0f / 900.0f, 1600.0f, 900.0f };

texture texture1;
sampler colorSampler = sampler_state
{
    Texture = (texture1);
    MinFilter = NONE;
    MagFilter = NONE;
    MipFilter = NONE;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

sampler colorLinearSampler = sampler_state
{
    Texture = (texture1);
    MinFilter = LINEAR;
    MagFilter = LINEAR;
    MipFilter = NONE;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

texture depthTexture;
sampler depthSampler = sampler_state
{
    Texture = (depthTexture);
    MinFilter = POINT;
    MagFilter = POINT;
    MipFilter = NONE;
    AddressU = CLAMP;
    AddressV = CLAMP;
};

void VertexShader1(in  float4 inPosition  : POSITION,
                   in  float2 inTexCoord  : TEXCOORD0,
                   out float4 outPosition : POSITION,
                   out float2 outTexCoord : TEXCOORD0)
{
    outPosition = inPosition;
    outTexCoord = inTexCoord;
}

float2 GetPrevUv(float2 uv, float depth)
{
    // Reconstruct an approximate world position from the current UV and depth.
    float4 currentClip;
    currentClip.x = uv.x * 2.0f - 1.0f;
    currentClip.y = (1.0f - uv.y) * 2.0f - 1.0f;
    currentClip.z = depth;
    currentClip.w = 1.0f;

    float4 worldPos = mul(currentClip, g_matInvCurrentViewProj);
    float invWorldW = 1.0f / max(abs(worldPos.w), 0.0001f);
    worldPos *= invWorldW;

    // Reproject into the previous frame to recover the previous UV.
    float4 prevClip = mul(worldPos, g_matPrevViewProj);
    float invPrevW = 1.0f / max(abs(prevClip.w), 0.0001f);
    prevClip *= invPrevW;

    float2 prevUv;
    prevUv.x = prevClip.x * 0.5f + 0.5f;
    prevUv.y = (1.0f - prevClip.y) * 0.5f;
    return prevUv;
}

float2 GetVelocity(float2 uv, float depth)
{
    float2 prevUv = GetPrevUv(uv, depth);
    float2 velocity = (uv - prevUv) * g_fBlurScale;

    // Clamp in pixels so the limit means the same thing at any screen size.
    float2 velocityPixels;
    velocityPixels.x = velocity.x / g_vTexelSize.x;
    velocityPixels.y = velocity.y / g_vTexelSize.y;

    float velocityPixelLength = length(velocityPixels);
    if (velocityPixelLength > g_fMaxBlurPixels)
    {
        velocityPixels *= g_fMaxBlurPixels / velocityPixelLength;
        velocity.x = velocityPixels.x * g_vTexelSize.x;
        velocity.y = velocityPixels.y * g_vTexelSize.y;
    }

    return velocity;
}

float4 SampleMotionBlur(float2 uv, float2 velocity)
{
    // Gather 21 samples along the motion direction and average them evenly.
    float4 accumColor = 0.0f;
    const int sampleCount = 21;

    [unroll]
    for (int i = 0; i < sampleCount; ++i)
    {
        float t = ((float)i / (float)(sampleCount - 1)) * 2.0f - 1.0f;
        float2 sampleUv = saturate(uv - velocity * t);
        accumColor += tex2D(colorLinearSampler, sampleUv);
    }

    return accumColor / (float)sampleCount;
}

void PixelShader1(in float2 inTexCoord : TEXCOORD0,
                  out float4 outColor  : COLOR0)
{
    float depth = tex2D(depthSampler, inTexCoord).r;
    float2 velocity = GetVelocity(inTexCoord, depth);

    if (g_iDebugViewMode == 1)
    {
        outColor = float4(depth, depth, depth, 1.0f);
        return;
    }

    if (g_iDebugViewMode == 2)
    {
        float2 velocityVis = velocity * 4.0f;
        outColor = float4(velocityVis.x * 0.5f + 0.5f,
                          velocityVis.y * 0.5f + 0.5f,
                          0.5f,
                          1.0f);
        return;
    }

    if (g_iDebugViewMode == 3)
    {
        outColor = tex2D(colorSampler, inTexCoord);
        return;
    }

    if (g_iMotionBlurEnabled == 0)
    {
        outColor = tex2D(colorSampler, inTexCoord);
        return;
    }

    outColor = SampleMotionBlur(inTexCoord, velocity);
}

technique Technique1
{
    pass Pass1
    {
        CullMode = NONE;
        VertexShader = compile vs_3_0 VertexShader1();
        PixelShader = compile ps_3_0 PixelShader1();
    }
}
