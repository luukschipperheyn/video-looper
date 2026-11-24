#pragma once

// The Loop's resolution, chosen once at startup and unchanged during a set (ADR-0002).
// The Program window, the inset, and the second window only scale this image.
inline constexpr int kLoopWidth = 1280;
inline constexpr int kLoopHeight = 720;
