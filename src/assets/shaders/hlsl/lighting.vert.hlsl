struct VSOutput {
    float4 gl_Position : SV_Position;
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

VSOutput main(VSInput input) {
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0)
    };
    float2 texCoords[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };
    VSOutput output;
    output.gl_Position = float4(positions[input.vertexIndex], 0.0, 1.0);
    output.fragTexCoord = texCoords[input.vertexIndex];
    return output;
}