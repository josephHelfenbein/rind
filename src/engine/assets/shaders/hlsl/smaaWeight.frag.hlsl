struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD0;
};

struct PushConstants {
    float2 invScreenSize;
    uint flags;
    uint pad;
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0)]]
Texture2D<float4> edgesTexture;

[[vk::binding(1)]]
Texture2D<float4> areaTexture;

[[vk::binding(2)]]
Texture2D<float4> searchTexture;

[[vk::binding(3)]]
SamplerState sampleSampler;

static const int SMAA_MAX_SEARCH_STEPS = 8;
static const int SMAA_MAX_SEARCH_STEPS_DIAG = 8;
static const float SMAA_AREATEX_MAX_DISTANCE = 16.0;
static const float SMAA_AREATEX_MAX_DISTANCE_DIAG = 20.0;
static const float2 SMAA_AREATEX_PIXEL_SIZE = 1.0 / float2(160.0, 560.0);
static const float SMAA_AREATEX_SUBTEX_SIZE = 1.0 / 7.0;
static const float2 SMAA_SEARCHTEX_SIZE = float2(66.0, 33.0);
static const float2 SMAA_SEARCHTEX_PACKED_SIZE = float2(64.0, 16.0);

#define SMAA_RT_METRICS float4(pc.invScreenSize.x, pc.invScreenSize.y, 1.0 / pc.invScreenSize.x, 1.0 / pc.invScreenSize.y)

void SMAAMovc(bool2 cond, inout float2 variable, float2 value) {
    [flatten] if (cond.x) variable.x = value.x;
    [flatten] if (cond.y) variable.y = value.y;
}

float2 SMAADecodeDiagBilinearAccess(float2 e) {
    e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);
    return round(e);
}

float4 SMAADecodeDiagBilinearAccess(float4 e) {
    e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);
    return round(e);
}

float SMAASearchLength(float2 e, float offset) {
    float2 scale = SMAA_SEARCHTEX_SIZE * float2(0.5, -1.0);
    float2 bias = SMAA_SEARCHTEX_SIZE * float2(offset, 1.0);

    scale += float2(-1.0, 1.0);
    bias += float2(0.5, -0.5);

    scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
    bias *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;

    return searchTexture.SampleLevel(sampleSampler, mad(scale, e, bias), 0).r;
}

float2 SMAAArea(float2 dist, float e1, float e2) {
    float2 texcoord = SMAA_AREATEX_MAX_DISTANCE * round(4.0 * float2(e1, e2)) + dist;
    texcoord = SMAA_AREATEX_PIXEL_SIZE * texcoord + 0.5 * SMAA_AREATEX_PIXEL_SIZE;
    return areaTexture.SampleLevel(sampleSampler, texcoord, 0).rg;
}

float2 SMAAAreaDiag(float2 dist, float2 e, float offset) {
    float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE_DIAG, SMAA_AREATEX_MAX_DISTANCE_DIAG), e, dist);
    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);
    texcoord.x += 0.5;
    texcoord.y = mad(SMAA_AREATEX_SUBTEX_SIZE, offset, texcoord.y);
    return areaTexture.SampleLevel(sampleSampler, texcoord, 0).rg;
}

float2 SMAASearchDiag1(float2 texcoord, float2 dir, out float2 e) {
    float4 coord = float4(texcoord, -1.0, 1.0);
    float3 t = float3(SMAA_RT_METRICS.xy, 1.0);
    e = float2(0.0, 0.0);
    [loop]
    while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9) {
        coord.xyz = mad(t, float3(dir, 1.0), coord.xyz);
        e = edgesTexture.SampleLevel(sampleSampler, coord.xy, 0).rg;
        coord.w = dot(e, float2(0.5, 0.5));
    }
    return coord.zw;
}

float2 SMAASearchDiag2(float2 texcoord, float2 dir, out float2 e) {
    float4 coord = float4(texcoord, -1.0, 1.0);
    coord.x += 0.25 * SMAA_RT_METRICS.x;
    float3 t = float3(SMAA_RT_METRICS.xy, 1.0);
    e = float2(0.0, 0.0);
    [loop]
    while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9) {
        coord.xyz = mad(t, float3(dir, 1.0), coord.xyz);
        e = edgesTexture.SampleLevel(sampleSampler, coord.xy, 0).rg;
        e = SMAADecodeDiagBilinearAccess(e);
        coord.w = dot(e, float2(0.5, 0.5));
    }
    return coord.zw;
}

float2 SMAACalculateDiagWeights(float2 texcoord, float2 e) {
    float2 weights = float2(0.0, 0.0);

    float4 d;
    float2 end;
    if (e.r > 0.0) {
        d.xz = SMAASearchDiag1(texcoord, float2(-1.0, 1.0), end);
        d.x += float(end.y > 0.9);
    } else {
        d.xz = float2(0.0, 0.0);
    }
    d.yw = SMAASearchDiag1(texcoord, float2(1.0, -1.0), end);

    [branch]
    if (d.x + d.y > 2.0) {
        float4 coords = mad(float4(-d.x + 0.25, d.x, d.y, -d.y - 0.25), SMAA_RT_METRICS.xyxy, texcoord.xyxy);
        float4 c;
        c.xy = edgesTexture.SampleLevel(sampleSampler, coords.xy + float2(-SMAA_RT_METRICS.x, 0.0), 0).rg;
        c.zw = edgesTexture.SampleLevel(sampleSampler, coords.zw + float2( SMAA_RT_METRICS.x, 0.0), 0).rg;
        c.yxwz = SMAADecodeDiagBilinearAccess(c.xyzw);

        float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw);

        SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0));

        weights += SMAAAreaDiag(d.xy, cc, 0.0);
    }

    d.xz = SMAASearchDiag2(texcoord, float2(-1.0, -1.0), end);
    if (edgesTexture.SampleLevel(sampleSampler, texcoord + float2(SMAA_RT_METRICS.x, 0.0), 0).r > 0.0) {
        d.yw = SMAASearchDiag2(texcoord, float2(1.0, 1.0), end);
        d.y += float(end.y > 0.9);
    } else {
        d.yw = float2(0.0, 0.0);
    }

    [branch]
    if (d.x + d.y > 2.0) {
        float4 coords = mad(float4(-d.x, -d.x, d.y, d.y), SMAA_RT_METRICS.xyxy, texcoord.xyxy);
        float4 c;
        c.x  = edgesTexture.SampleLevel(sampleSampler, coords.xy + float2(-SMAA_RT_METRICS.x, 0.0), 0).g;
        c.y  = edgesTexture.SampleLevel(sampleSampler, coords.xy + float2(0.0, -SMAA_RT_METRICS.y), 0).r;
        c.zw = edgesTexture.SampleLevel(sampleSampler, coords.zw + float2( SMAA_RT_METRICS.x, 0.0), 0).gr;
        float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw);

        SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0));

        weights += SMAAAreaDiag(d.xy, cc, 0.0).gr;
    }

    return weights;
}

float SearchXLeft(float2 texcoord, float end) {
    float2 e = float2(0.0, 1.0);
    [loop]
    while (texcoord.x > end && e.g > 0.8281 && e.r == 0.0) {
        e = edgesTexture.SampleLevel(sampleSampler, texcoord, 0).rg;
        texcoord = mad(-float2(2.0, 0.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(e, 0.0), 3.25);
    return mad(SMAA_RT_METRICS.x, offset, texcoord.x);
}

float SearchXRight(float2 texcoord, float end) {
    float2 e = float2(0.0, 1.0);
    [loop]
    while (texcoord.x < end && e.g > 0.8281 && e.r == 0.0) {
        e = edgesTexture.SampleLevel(sampleSampler, texcoord, 0).rg;
        texcoord = mad(float2(2.0, 0.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(e, 0.5), 3.25);
    return mad(-SMAA_RT_METRICS.x, offset, texcoord.x);
}

float SearchYUp(float2 texcoord, float end) {
    float2 e = float2(1.0, 0.0);
    [loop]
    while (texcoord.y > end && e.r > 0.8281 && e.g == 0.0) {
        e = edgesTexture.SampleLevel(sampleSampler, texcoord, 0).rg;
        texcoord = mad(-float2(0.0, 2.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(e.gr, 0.0), 3.25);
    return mad(SMAA_RT_METRICS.y, offset, texcoord.y);
}

float SearchYDown(float2 texcoord, float end) {
    float2 e = float2(1.0, 0.0);
    [loop]
    while (texcoord.y < end && e.r > 0.8281 && e.g == 0.0) {
        e = edgesTexture.SampleLevel(sampleSampler, texcoord, 0).rg;
        texcoord = mad(float2(0.0, 2.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(e.gr, 0.5), 3.25);
    return mad(-SMAA_RT_METRICS.y, offset, texcoord.y);
}

float4 main(VSOutput input) : SV_Target {
    float2 texcoord = input.fragTexCoord;
    float4 METRICS = SMAA_RT_METRICS;

    float4 offset0 = mad(METRICS.xyxy, float4(-0.25, -0.125, 1.25, -0.125), texcoord.xyxy);
    float4 offset1 = mad(METRICS.xyxy, float4(-0.125, -0.25, -0.125, 1.25), texcoord.xyxy);
    float4 offset2 = mad(METRICS.xxyy,
                         float4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS),
                         float4(offset0.xz, offset1.yw));

    float2 pixcoord = texcoord * METRICS.zw;

    float2 e = edgesTexture.SampleLevel(sampleSampler, texcoord, 0).rg;
    float4 weights = float4(0, 0, 0, 0);

    if (e.g > 0.0) {
        weights.rg = SMAACalculateDiagWeights(texcoord, e);

        [branch]
        if (weights.r == -weights.g) {
            float2 d;
            float3 coords;
            coords.x = SearchXLeft(offset0.xy, offset2.x);
            coords.y = offset1.y;
            d.x = coords.x;

            float e1 = edgesTexture.SampleLevel(sampleSampler, coords.xy, 0).r;
            coords.z = SearchXRight(offset0.zw, offset2.y);
            d.y = coords.z;
            d = abs(round(mad(METRICS.zz, d, -pixcoord.xx)));

            float2 sqrt_d = sqrt(d);

            float e2 = edgesTexture.SampleLevel(sampleSampler, coords.zy + float2(METRICS.x, 0), 0).r;

            weights.rg = SMAAArea(sqrt_d, e1, e2);
        } else {
            e = float2(0.0, 0.0);
        }
    }

    if (e.r > 0.0) {
        float2 d;
        float3 coords;
        coords.y = SearchYUp(offset1.xy, offset2.z);
        coords.x = offset0.x;
        d.x = coords.y;
        float e1 = edgesTexture.SampleLevel(sampleSampler, coords.xy, 0).g;

        coords.z = SearchYDown(offset1.zw, offset2.w);
        d.y = coords.z;

        d = abs(round(mad(METRICS.ww, d, -pixcoord.yy)));

        float2 sqrt_d = sqrt(d);
        float e2 = edgesTexture.SampleLevel(sampleSampler, coords.xz + float2(0, METRICS.y), 0).g;

        weights.ba = SMAAArea(sqrt_d, e1, e2);
    }

    return weights;
}