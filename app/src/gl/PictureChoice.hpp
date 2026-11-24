#pragma once

enum class Picture { Source, Audition, Program };

enum class PictureMoment { Idle, Cue, Overdub, Bake };

struct PictureChoice {
	Picture shown;
	Picture previewed;
	Picture exported;
	float previewOpacity;
};

PictureChoice choosePictures(PictureMoment moment, float writeOpacity, float recordOpacity);
