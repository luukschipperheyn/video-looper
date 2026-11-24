#include "gl/PictureChoice.hpp"

PictureChoice choosePictures(PictureMoment moment, float writeOpacity, float recordOpacity) {
	PictureChoice choice;
	choice.shown = moment == PictureMoment::Idle ? Picture::Program : Picture::Audition;
	choice.previewed = Picture::Audition;
	choice.exported = Picture::Audition;
	choice.previewOpacity = recordOpacity;
	return choice;
}
