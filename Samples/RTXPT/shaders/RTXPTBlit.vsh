struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEX_COORD;
};

void main(in uint VertexId : SV_VertexID,
          out PSInput Output)
{
    Output.UV  = float2(VertexId >> 1, VertexId & 1) * 2.0;
    Output.Pos = float4(Output.UV.x * 2.0 - 1.0, 1.0 - Output.UV.y * 2.0, 0.0, 1.0);
}
