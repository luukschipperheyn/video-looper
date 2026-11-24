#pragma once

#include "io/AVFFileSource.hpp"
#include "io/AVFWebcamSource.hpp"
#include "io/VideoSource.hpp"

// The AVFoundation decoders.
inline DecoderFactory avfDecoders() {
	return {[] { return std::make_unique<AVFFileSource>(); }, [] { return std::make_unique<AVFWebcamSource>(); }};
}
