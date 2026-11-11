// Point-expansion, the shape the game's terrain/minimap GS uses: pointlist
// input, maxvertexcount, TriangleStream expansion to one triangle. The input
// matches triangle_vs's output (SV_POSITION + COLOR) and the output matches
// ps_main's input, so the committed VS/PS need no changes.
struct GSIO { float4 position : SV_POSITION; float4 color : COLOR; };
[maxvertexcount(3)]
void gs_main(point GSIO vin[1], inout TriangleStream<GSIO> stream) {
    GSIO o;
    o.color = vin[0].color;
    // Screen-space clockwise (D3D default front face): top, bottom-right,
    // bottom-left. NDC y+ is up, so y+0.4 is the top vertex on screen.
    o.position = float4(vin[0].position.x,       vin[0].position.y + 0.4, 0.0, 1.0); stream.Append(o);
    o.position = float4(vin[0].position.x + 0.4, vin[0].position.y - 0.4, 0.0, 1.0); stream.Append(o);
    o.position = float4(vin[0].position.x - 0.4, vin[0].position.y - 0.4, 0.0, 1.0); stream.Append(o);
}
