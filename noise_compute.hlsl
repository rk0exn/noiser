cbuffer Params : register(b0)
{
	uint width;
	uint height;
	uint seed;
};

Texture2D<float4> inputTex : register(t1);
RWTexture2D<float4> result : register(u0);

float rand(float2 co)
{
	return frac(sin(dot(co + float2(seed, seed), float2(12.9898, 78.233))) * 43758.5453);
};

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	int width_i = int(width);
	int height_i = int(height);
	
	int2 px = int2(DTid.xy);
	if (px.x >= width_i || px.y >= height_i)
		return;
	
	float2 uv = float2(px) / float2(width, height);
	
	float scan = 0.9 + 0.1 * sin(uv.y * 80.0); // 80.0のところは書き換え可能、最大目安600.0
	
	float xOffset = 2.0 * sin(uv.y * 30.0 + seed * 0.01);
	int distortedX = clamp(int(px.x + xOffset), 0, int(width) - 1);
	int2 distortPx = int2(distortedX, px.y);
	
	int2 off = int2(3, 0); // (3, 0)のところは書き換え可能(大きすぎ注意、最大目安は15まで)
	float r = inputTex.Load(int3(clamp(distortPx + off, int2(0, 0), int2(width - 1, height - 1)), 0)).r;
	float g = inputTex.Load(int3(distortPx, 0)).g;
	float b = inputTex.Load(int3(clamp(distortPx - off, int2(0, 0), int2(width - 1, height - 1)), 0)).b;
	float a = inputTex.Load(int3(distortPx, 0)).a;

	float4 col = float4(r, g, b, a);
	
	float n = (rand(uv * float2(width, height)) - 0.5) * 0.04;
	col.rgb += n;
	
	col.rgb *= scan;

	result[px] = col;
}