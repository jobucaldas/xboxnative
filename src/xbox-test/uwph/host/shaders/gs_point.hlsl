// Point-sprite expansion, the shape the game's terrain/minimap GS uses:
// pointlist input, maxvertexcount, PointStream expansion to a triangle.
struct GSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
[maxvertexcount(3)]
void gs_main(point float2 vin[1] : TEXCOORD0, inout PointStream<GSOut> stream) {
    GSOut o;
    o.position = float4(vin[0].x, vin[0].y + 0.4, 0.0, 1.0); o.uv = float2(0.5, 0.0); stream.Append(o);
    o.position = float4(vin[0].x - 0.4, vin[0].y - 0.4, 0.0, 1.0); o.uv = float2(0.0, 1.0); stream.Append(o);
    o.position = float4(vin[0].x + 0.4, vin[0].y - 0.4, 0.0, 1.0); o.uv = float2(1.0, 1.0); stream.Append(o);
}
