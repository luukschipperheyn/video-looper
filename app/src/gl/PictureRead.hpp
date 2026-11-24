#pragma once

#include <vector>

#include "gl/PictureChoice.hpp"

class PictureRead {
public:
	virtual ~PictureRead() = default;
	virtual const std::vector<unsigned char>& read(Picture picture) = 0;
};
