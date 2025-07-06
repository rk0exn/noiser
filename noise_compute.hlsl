struct NoiseRect
{
	uint2 pos;
	uint size;
	uint4 color;
};

cbuffer Params : register(b0)
{
	uint width;
	uint height;
	uint numRects;
	uint seed;
};

StructuredBuffer<NoiseRect> rects : register(t0);
Texture2D<float4> inputTex : register(t1);
RWTexture2D<float4> result : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= width || DTid.y >= height)
		return;
	float4 color = inputTex.Load(int3(DTid.xy, 0));
	for (uint i = 0; i < numRects; ++i)
	{
		NoiseRect r = rects[i];
		if (DTid.x >= r.pos[0] && DTid.x < r.pos[0] + r.size &&
            DTid.y >= r.pos[1] && DTid.y < r.pos[1] + r.size)
		{
			float4 noiseColor = float4(
                r.color.r / 255.0,
                r.color.g / 255.0,
                r.color.b / 255.0,
                r.color.a / 255.0
            );
			result[DTid.xy] = lerp(color, noiseColor, noiseColor.a);
			return;
		}
	}
	result[DTid.xy] = color;
}
