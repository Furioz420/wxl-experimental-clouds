// The base cloud sheet, regrown in place: same dome, same textures, a richer field.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// THE CLOUDS ARE NOT A FILE. The engine grows its base cloud layer on the CPU -- a band of rows
// per frame into a pixel buffer, uploaded to one of a texture pair, the pair swapped when a sheet
// completes -- and the pattern it grows is the same layered noise it has grown since 2004: small
// blobs on a short tile. Nothing about the DOME changed in twenty years of the game, only the
// field drawn on it, so that is exactly what is replaced here.
//
// The stock generator still runs. It keeps every piece of state this module would otherwise have
// to reverse-engineer the maintenance of -- the row cursor, the sheet parity, the scroll phase,
// the density buffer that feeds the cloud bump map and collision -- and then the band it just
// drew is redrawn over with a fractal field and re-uploaded through the engine's own path. The
// texture pair, the incremental schedule and the zone's own coverage all keep working because
// they were never touched.

#include "wxl-retail-clouds/Clouds.hpp"

#include "config.hpp"
#include "common/Log.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "offsets/engine/Sky.hpp"

#include <cmath>
#include <cstdint>

namespace
{
    namespace sky = wxl::offsets::engine::sky;

    wxl::clouds::CloudTuning g_tuning;

    using GenerateFn = void(__fastcall*)(void* self, void* edx);
    GenerateFn g_origGenerate = nullptr;

    using TextureGetGxTexFn = void* (__cdecl*)(void* htex, int a, int b);
    using GxTexUpdateFn     = void (__cdecl*)(void* gxTex, int x0, int yStart, int width,
                                              int yEnd, int immediate);

    /// The clouds object, as last seen by the detour. Only so the panel's redraw button can reach
    /// the engine's own full-redraw flag; never dereferenced off the update path's thread.
    void* g_lastSelf = nullptr;

    /// The phase the current sheet was latched at. The engine draws a sheet over many frames and
    /// swaps it in whole; a band drawn at a fresher phase than its neighbours would put a seam
    /// through the pattern, so the field is frozen per sheet exactly as the stock scroll is.
    float g_sheetPhase = 0.0f;

    template <typename T>
    T Field(void* self, uintptr_t off)
    {
        return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(self) + off);
    }

    /// Permutation for the value lattice. Fixed seed: two clients on the same sky is a property,
    /// not an accident.
    uint8_t g_perm[512];
    bool    g_permReady = false;

    void BuildPerm()
    {
        uint32_t s = 0x9E3779B9u;
        uint8_t p[256];
        for (int i = 0; i < 256; ++i) p[i] = static_cast<uint8_t>(i);
        for (int i = 255; i > 0; --i)
        {
            s = s * 1664525u + 1013904223u;
            const int j = static_cast<int>(s % static_cast<uint32_t>(i + 1));
            const uint8_t t = p[i]; p[i] = p[j]; p[j] = t;
        }
        for (int i = 0; i < 512; ++i) g_perm[i] = p[i & 255];
        g_permReady = true;
    }

    float LatticeAt(int x, int y, int wrap, int salt)
    {
        // True modulo, not a mask: the cell count is whatever the slider says, and the sheet only
        // tiles if the lattice wraps at exactly that count.
        int xm = x % wrap; if (xm < 0) xm += wrap;
        int ym = y % wrap; if (ym < 0) ym += wrap;
        return static_cast<float>(g_perm[g_perm[(xm + salt) & 255] + (ym & 255)]) * (1.0f / 255.0f);
    }

    /// Periodic value noise: the octave's lattice wraps at its own cell count, so the sheet tiles
    /// seamlessly at every octave and therefore in the sum.
    float Octave(float u, float v, int cells, int salt)
    {
        const float fu = u * static_cast<float>(cells);
        const float fv = v * static_cast<float>(cells);
        const int iu = static_cast<int>(std::floor(fu));
        const int iv = static_cast<int>(std::floor(fv));
        float tu = fu - static_cast<float>(iu);
        float tv = fv - static_cast<float>(iv);
        // The quintic fade, not the cubic. The cubic's second derivative is discontinuous at every
        // cell edge, and on a slowly-drifting sky that discontinuity IS the visible square: a
        // diamond plateau per lattice cell. The quintic is why Perlin revised his own noise.
        tu = tu * tu * tu * (tu * (tu * 6.0f - 15.0f) + 10.0f);
        tv = tv * tv * tv * (tv * (tv * 6.0f - 15.0f) + 10.0f);

        const float a = LatticeAt(iu,     iv,     cells, salt);
        const float b = LatticeAt(iu + 1, iv,     cells, salt);
        const float c = LatticeAt(iu,     iv + 1, cells, salt);
        const float d = LatticeAt(iu + 1, iv + 1, cells, salt);
        const float ab = a + (b - a) * tu;
        const float cd = c + (d - c) * tu;
        return ab + (cd - ab) * tv;
    }

    /// The field, in [0,1]. Octaves at doubling frequency, each drifted at its own rate so the
    /// sheet EVOLVES between steps instead of sliding as one rigid sheet -- which is the single
    /// visible difference between weather and wallpaper.
    float Fbm(float u, float v, float phase)
    {
        const wxl::clouds::CloudTuning& g = g_tuning;
        float sum = 0.0f, amp = 1.0f, tot = 0.0f;
        // Rounded to a whole cell count: a periodic lattice only tiles on an integer period, so
        // the slider moves in whole cells and the doubling keeps every octave on one.
        int cells = static_cast<int>(g.scale + 0.5f);
        if (cells < 1) cells = 1;
        for (int o = 0; o < g.octaves; ++o)
        {
            const float rate = 0.5f + 0.35f * static_cast<float>(o);
            const float du = phase * g.drift * 0.010f * rate;
            const float dv = phase * g.drift * 0.004f * rate;
            float val = Octave(u + du, v + dv, cells, o * 57);
            // The billow fold: peaks where the raw octave crosses its middle, rounded on both
            // sides -- puffs instead of ridges. Blended rather than switched, because full billow
            // on every octave reads as soap foam and none reads as stains.
            val += (1.0f - std::fabs(2.0f * val - 1.0f) - val) * g.cotton;
            sum += amp * val;
            tot += amp;
            amp *= 0.55f;
            cells <<= 1;
            if (cells > 4096) break;
        }
        return sum / tot;
    }

    /// The shaped coverage at a point: the same threshold and edge the visible field uses, so the
    /// light-march below shades against exactly what is drawn.
    float CoverageAt(float u, float v, float cover, float gain)
    {
        // THE TWIST BEFORE THE LOOKUP. Thresholded fbm makes blobs whatever else is done to it;
        // a cauliflower edge is a domain pushed around by a broader field before the pattern is
        // read at all. The warp field wraps on the same lattice, so the sheet still tiles.
        const float w = g_tuning.warp * 0.12f;
        if (w > 0.0f)
        {
            int wc = static_cast<int>(g_tuning.scale + 0.5f);
            if (wc < 1) wc = 1;
            wc <<= 1;
            u += (Octave(u, v, wc, 173) - 0.5f) * w;
            v += (Octave(u, v, wc, 219) - 0.5f) * w;
        }
        float a = (Fbm(u, v, g_sheetPhase) - cover) * gain;
        a = (a < 0.0f) ? 0.0f : (a > 1.0f) ? 1.0f : a;
        // Squared: fbm past a threshold has hard-shouldered blobs, and the square pulls the
        // shoulder into the soft ramp a cloud edge actually has.
        return a * a;
    }

    /// Redraws one band of the sheet and re-uploads it, mirroring the engine's own tail exactly.
    void RedrawBand(void* self, uint32_t start, uint32_t rows, uint8_t parity)
    {
        const uint32_t width = Field<uint32_t>(self, sky::kCloudsWidth);
        const uint32_t shift = Field<uint32_t>(self, sky::kCloudsStrideShift);
        uint8_t* rgba = Field<uint8_t*>(self, sky::kCloudsRgba);
        uint8_t* dens = Field<uint8_t*>(self, sky::kCloudsDensityByte);
        if (!rgba || !dens || width == 0) return;
        if (start >= width) return;
        if (start + rows > width) rows = width - start;

        // The zone still owns how cloudy its sky is: the engine's own threshold byte is the base
        // coverage, and the panel only leans on it.
        const float zoneCover = static_cast<float>(Field<uint8_t>(self, sky::kCloudsCoverage))
                                * (1.0f / 255.0f);
        float cover = zoneCover + g_tuning.coverageBias;
        cover = (cover < 0.0f) ? 0.0f : (cover > 0.95f) ? 0.95f : cover;
        const float gain = g_tuning.sharpness / (1.0f - cover);

        // WHERE THE SUN IS, from the engine's own celestial light -- the same vector it hands its
        // model lighting. Projected onto the sheet, negated because the stored direction is the
        // way the light TRAVELS and the march walks toward its source. A sun near the zenith has
        // no direction on the sheet worth marching; the march folds toward zero with it and noon
        // clouds come out evenly lit, which is what noon does.
        float sunU = 0.0f, sunV = 0.0f, sunFlat = 0.0f;
        {
            using GetInfoFn = uint8_t* (__cdecl*)();
            if (uint8_t* info = reinterpret_cast<GetInfoFn>(sky::kDayNightGetInfo)())
            {
                const float* d = reinterpret_cast<const float*>(info + sky::kInfoLightDir);
                const float len = std::sqrt(d[0] * d[0] + d[1] * d[1]);
                if (len > 1e-3f)
                {
                    sunU = -d[0] / len;
                    sunV = -d[1] / len;
                    sunFlat = (len > 1.0f) ? 1.0f : len;
                }
            }
        }

        const float inv = 1.0f / static_cast<float>(width);
        // Steps sized to the puffs rather than to the texel, so the shading scale follows the
        // pattern scale instead of dissolving when either slider moves.
        const float step = 0.014f;
        const float marchGain = 2.4f * g_tuning.depth * sunFlat;

        for (uint32_t r = 0; r < rows; ++r)
        {
            const uint32_t row = start + r;
            uint8_t* px = rgba + (static_cast<size_t>(row) << shift) * 4;
            uint8_t* db = dens + (static_cast<size_t>(row) << shift);
            const float v = static_cast<float>(row) * inv;
            for (uint32_t x = 0; x < width; ++x)
            {
                const float u = static_cast<float>(x) * inv;
                const float a = CoverageAt(u, v, cover, gain);

                float light = 1.0f;
                float rim = 0.0f;
                if (a > 0.003f && marchGain > 0.0f)
                {
                    // A SHORT MARCH TOWARD THE SUN through the field itself: what stands between
                    // this texel and the light decides how lit it is. Three taps are enough --
                    // the field is smooth at the scale the taps stride.
                    float occl = 0.0f;
                    occl += CoverageAt(u + sunU * step,        v + sunV * step,        cover, gain);
                    occl += CoverageAt(u + sunU * step * 2.0f, v + sunV * step * 2.0f, cover, gain) * 0.75f;
                    occl += CoverageAt(u + sunU * step * 3.5f, v + sunV * step * 3.5f, cover, gain) * 0.5f;
                    const float transmit = std::exp(-occl * marchGain);
                    light = g_tuning.ambient + (1.0f - g_tuning.ambient) * transmit;

                    // THE SILVER LINING, one more tap on the same line: a THIN texel whose
                    // sun-side neighbour is thick is the lit edge of something -- the one glow
                    // every eye knows a cloud by.
                    const float ahead = CoverageAt(u + sunU * step * 1.5f,
                                                   v + sunV * step * 1.5f, cover, gain);
                    float edge = (ahead - a) * (1.0f - a);
                    if (edge > 0.0f)
                        rim = edge * transmit * g_tuning.lining;
                }

                float c = light + rim;
                c = (c > 1.0f) ? 1.0f : c;
                const uint8_t cb = static_cast<uint8_t>(c * 255.0f);
                const uint8_t av = static_cast<uint8_t>(a * 255.0f);
                px[0] = cb; px[1] = cb; px[2] = cb; px[3] = av;
                px += 4;
                // The engine's own consumers of the density byte -- the bump map, the cloud
                // collision -- keep reading a field that matches what is on screen.
                *db++ = av;
            }
        }

        void* pair0 = Field<void*>(self, sky::kCloudsTexturePair + 4u * ((parity - 1u) & 1u));
        if (!pair0) return;
        auto getGx  = reinterpret_cast<TextureGetGxTexFn>(sky::kTextureGetGxTex);
        auto update = reinterpret_cast<GxTexUpdateFn>(sky::kGxTexUpdate);
        if (void* gx = getGx(pair0, 1, 0))
            update(gx, 0, static_cast<int>(start), static_cast<int>(width),
                   static_cast<int>(start + rows), 1);
    }

    void __fastcall HkGenerate(void* self, void* edx)
    {
        // Everything the band redraw needs is read BEFORE the original runs, because the original
        // advances the cursor, may reset it for a full redraw, and flips the parity when a sheet
        // completes -- after it returns, the band it drew can no longer be reconstructed.
        const bool     want   = g_tuning.enabled && Field<uint32_t>(self, sky::kCloudsEnabled) != 0;
        const uint8_t  full   = Field<uint8_t>(self, sky::kCloudsFullRedraw);
        const uint8_t  parity = Field<uint8_t>(self, sky::kCloudsParity);
        const uint32_t cursor = Field<uint32_t>(self, sky::kCloudsRowCursor);
        const uint32_t perFrm = Field<uint32_t>(self, sky::kCloudsRowsPerFrame);
        const uint32_t width  = Field<uint32_t>(self, sky::kCloudsWidth);
        const float    phase  = Field<float>(self, sky::kCloudsPhase);

        g_origGenerate(self, edx);
        g_lastSelf = self;

        if (!want) return;
        // The original bows out during light transitions without touching the phase; a band that
        // was never drawn must not be redrawn.
        if (Field<float>(self, sky::kCloudsPhase) == phase) return;

        const uint32_t start = full ? 0u : cursor;
        const uint32_t rows  = full ? width : perFrm;
        // A sheet's field is frozen at the phase its first band saw; the engine swaps sheets in
        // whole, so the step lands exactly where the stock pattern's does.
        if (start == 0) g_sheetPhase = phase;

        if (!g_permReady) BuildPerm();
        RedrawBand(self, start, rows, parity);
    }

    bool Install()
    {
        if (!wxl::hook::Install("Clouds_Generate", sky::kCloudsGenerate,
                                &HkGenerate, &g_origGenerate))
            return false;
        WLOG_INFO("clouds: sheet generator detour installed");
        return true;
    }
}

namespace wxl::clouds
{
    CloudTuning& Tuning() { return g_tuning; }

    void RequestRedraw()
    {
        if (!g_lastSelf) return;
        *(reinterpret_cast<uint8_t*>(g_lastSelf) + sky::kCloudsFullRedraw) = 1;
    }
}

WXL_REGISTER_FEATURE("retail-clouds", wxl::features::retailClouds, Install)
