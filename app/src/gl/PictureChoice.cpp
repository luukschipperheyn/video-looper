#include "gl/PictureChoice.hpp"

PictureChoice choosePictures(PictureMoment moment, float writeOpacity) {
	PictureChoice choice;
	choice.shown = moment == PictureMoment::Idle ? Picture::Program : Picture::Audition;
	choice.previewed = Picture::Audition;
	choice.exported = Picture::Program;
	choice.previewOpacity = writeOpacity;
	return choice;
}
