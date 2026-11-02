struct Vertex { float4 position : SV_POSITION; float4 color : COLOR; };
Vertex vs_main(float2 position : POSITION, float4 color : COLOR) {
    Vertex v; v.position = float4(position, 0.0, 1.0); v.color = color; return v;
}
float4 ps_main(Vertex v) : SV_TARGET { return v.color; }
