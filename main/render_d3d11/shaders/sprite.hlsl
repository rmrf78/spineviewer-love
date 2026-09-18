cbuffer ViewConstants : register(b0)
{
	float2 viewportSize;
	float2 padding;
};

struct VSInput
{
	float3 position : POSITION;
	float rhw : TEXCOORD0;
	float4 color : COLOR0;
	float2 uv : TEXCOORD1;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR0;
	float2 uv : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
	PSInput output;
	float2 ndc;
	ndc.x = input.position.x / viewportSize.x * 2.0f - 1.0f;
	ndc.y = 1.0f - input.position.y / viewportSize.y * 2.0f;
	output.position = float4(ndc, input.position.z, 1.0f);
	output.color = input.color;
	output.uv = input.uv;
	return output;
}

Texture2D spriteTexture : register(t0);
Texture2D maskTexture : register(t1);
SamplerState spriteSampler : register(s0);

float4 PSMain(PSInput input) : SV_TARGET
{
	return spriteTexture.Sample(spriteSampler, input.uv) * input.color;
}

float4 PSColorKeyResolve(PSInput input) : SV_TARGET
{
	const float4 sampleColor = spriteTexture.Sample(spriteSampler, input.uv);
	if (sampleColor.a <= 0.08f)
		return float4(1.0f / 255.0f, 0.0f, 1.0f / 255.0f, 1.0f);

	const float3 straightColor = saturate(sampleColor.rgb / max(sampleColor.a, 1.0f / 255.0f));
	return float4(straightColor, 1.0f);
}

float4 PSMasked(PSInput input) : SV_TARGET
{
	float2 maskUv = input.position.xy / viewportSize;
	float maskAlpha = maskTexture.Sample(spriteSampler, maskUv).a;
	return spriteTexture.Sample(spriteSampler, input.uv) * input.color * maskAlpha;
}

float4 PSMaskedInverted(PSInput input) : SV_TARGET
{
	float2 maskUv = input.position.xy / viewportSize;
	float maskAlpha = 1.0f - maskTexture.Sample(spriteSampler, maskUv).a;
	return spriteTexture.Sample(spriteSampler, input.uv) * input.color * maskAlpha;
}
