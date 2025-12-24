struct VSOutput {
    [[vk::location(0)]] float linearDepth : TEXCOORD0;
};

float4 main(VSOutput input) : SV_Target {
    return float4(input.linearDepth, 0.0, 0.0, 1.0);
}
