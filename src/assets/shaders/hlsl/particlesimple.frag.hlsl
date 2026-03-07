struct VSOutput {
    [[vk::location(0)]] float4 color : COLOR;
};

float4 main(VSOutput input) : SV_Target {
    return input.color;
}
