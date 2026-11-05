#pragma once
// Traffic fades in and out at the edge of the box it is drawn in.

namespace trafficfade {

// Takes the drawn box's half-sizes: across (x and z) and up (y).
void Install(float across, float up);

}  // namespace trafficfade
