#pragma once

#include <vector>

#include "gl/PictureChoice.hpp"

// The picture path, asked for one named image. The Compositor is the real one.
class PictureRead {
public:
	virtual ~PictureRead() = default;
	virtual const std::vector<unsigned char>& read(Picture picture) = 0;
};
