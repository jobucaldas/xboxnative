// Focused variant for Halo Wars' flagged GS scan: pointlist input,
// maxvertexcount(4), trianglestrip output, one stream, RT-array index output.
// The console scan does not expose the shader body or resource reads, so this
// fixture deliberately does not invent CB/SRV dependencies.
struct HaloGSIO { float4 position : SV_POSITION; float4 color : COLOR; };
struct HaloGSOut {
    float4 position : SV_POSITION;
    float4 color : COLOR;
    uint rtIndex : SV_RenderTargetArrayIndex;
};
[maxvertexcount(4)]
void gs_halo(point HaloGSIO vin[1], inout TriangleStream<HaloGSOut> stream) {
    HaloGSOut o;
    o.color = vin[0].color;
    o.rtIndex = 0;
    o.position = float4(vin[0].position.xy + float2(-0.4, 0.4), 0.0, 1.0); stream.Append(o);
    o.position = float4(vin[0].position.xy + float2( 0.4, 0.4), 0.0, 1.0); stream.Append(o);
    o.position = float4(vin[0].position.xy + float2(-0.4,-0.4), 0.0, 1.0); stream.Append(o);
    o.position = float4(vin[0].position.xy + float2( 0.4,-0.4), 0.0, 1.0); stream.Append(o);
}
