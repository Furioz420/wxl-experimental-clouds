// The base cloud sheet, regrown: what the field looks like and the knobs it answers to.
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

#pragma once

namespace wxl::clouds
{
    /// Everything the regrown sheet can be tuned by. All of it is look; none of it feeds a query.
    struct CloudTuning
    {
        /// The runtime switch. Off calls the stock generator alone, which is the comparison every
        /// other number here is judged against.
        bool enabled = true;

        /// Cells of the broadest octave across the sheet. Two is a sky with a couple of great
        /// masses in it; six is scattered flock. Non-integer values are honest -- the lattice is
        /// continuous under the wrap.
        float scale = 3.0f;

        /// Octaves summed. Each one adds detail half the size of the last; past the texel there is
        /// nothing left to add.
        int octaves = 5;

        /// Bias added to the zone's own coverage threshold. The zone still decides how cloudy its
        /// sky is -- this leans on that decision without replacing it. Negative is cloudier.
        float coverageBias = 0.0f;

        /// How hard the edge of a cloud turns on. Low is haze; high is cut paper.
        float sharpness = 2.2f;

        /// How far a low-frequency field displaces every lookup. Straight fbm past a threshold
        /// makes blobs; cauliflower is a TWISTED domain, and this is the twist.
        float warp = 0.35f;

        /// Billow blend. Zero is plain fbm; one folds every octave into rounded puffs -- the
        /// cotton. The middle keeps some of both.
        float cotton = 0.6f;

        /// Drift speed multiplier over the engine's own phase. The sheet advances a whole pattern
        /// step at a time exactly as the stock one does -- this scales how far each step travels.
        float drift = 1.0f;

        /// How deeply a cloud shades ITSELF. This is a short light-march through the field toward
        /// the sun, baked into the sheet: bases darken under thick cloud, sun-side flanks stay
        /// lit, and the forms read as volumes instead of stains. Zero is the flat sheet back.
        float depth = 0.6f;

        /// The silver lining: thin edges facing the sun glow. The single most recognisable thing
        /// about a lit cloud, and the cheapest -- one extra tap along the same march.
        float lining = 1.0f;

        /// The floor under the self-shading. Even the shadowed base of a cloud is lit by the whole
        /// sky; zero here is charcoal smoke, not weather.
        float ambient = 0.55f;
    };

    CloudTuning& Tuning();

    /// Asks the engine to redraw the whole sheet on its next update, through the same flag its own
    /// LOD callback uses. A no-op until the first update has run.
    void RequestRedraw();
}
