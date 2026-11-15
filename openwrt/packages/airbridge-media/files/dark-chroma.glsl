// Administrator-reviewed fixed GPU shader; not user-selectable and not an executable hook.
// Dark-scene chroma noise reduction for software-decoded video.
//
// Low-bitrate sources carry isolated chroma samples in near-black areas that
// decode to coloured (typically green) specks; upscaling enlarges every such
// sample into a visible blob. For dark pixels only, replace the chroma sample
// with the mean of nearby samples of similar colour. Luma is never changed,
// bright/mid-tone pixels are passed through and real colour edges are kept
// because dissimilar neighbours are excluded.
//
// Runs at the source chroma resolution, before scaling, and only for
// standard-definition sources (chroma width up to 400, luma up to 800), where
// enlargement to 4K is largest. On the CM5 any extra pass on 1080p sources
// dropped frames (49 samples: 35-42 drops in 7 s; 12 samples: 12-13), so larger
// sources are passed through at no cost. It does not run for zero-copy DRM
// PRIME hardware frames, which bypass the GPU renderer.

//!HOOK CHROMA
//!BIND HOOKED
//!BIND LUMA
//!WHEN HOOKED.w 401 <
//!DESC AirBridge dark-scene chroma noise reduction (SD sources)

#define RADIUS    3
#define LIMIT     0.047   // similarity window at full strength, about 12 8-bit codes
#define DARK_FULL 0.16    // normalized luma at or below which strength is full
#define DARK_NONE 0.28    // normalized luma at or above which pixels pass through

vec4 hook()
{
    vec4 c = HOOKED_tex(HOOKED_pos);
    float luma = LUMA_tex(LUMA_pos).r;
    float limit = LIMIT * (1.0 - smoothstep(DARK_FULL, DARK_NONE, luma));
    if (limit <= 0.0)
        return c;

    vec2 sum = c.rg;
    float n = 1.0;
    for (int y = -RADIUS; y <= RADIUS; y++) {
        for (int x = -RADIUS; x <= RADIUS; x++) {
            if (x == 0 && y == 0)
                continue;
            vec2 s = HOOKED_tex(HOOKED_pos + HOOKED_pt * vec2(float(x), float(y))).rg;
            vec2 d = abs(s - c.rg);
            float w = step(max(d.x, d.y), limit);
            sum += s * w;
            n += w;
        }
    }
    c.rg = sum / n;
    return c;
}
