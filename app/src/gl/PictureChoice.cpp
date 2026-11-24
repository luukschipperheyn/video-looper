#include "gl/PictureChoice.hpp"

PictureChoice choosePictures(PictureMoment moment, float writeOpacity, float recordOpacity) {
	PictureChoice choice;
	choice.shown = moment == PictureMoment::Idle ? Picture::Program : Picture::Audition;
	choice.previewed = Picture::Audition;
	choice.exported = Picture::Program;
	// Idle has no write, so the preview shows the Source at the record opacity. Cue and Overdub follow the write.
	choice.previewOpacity = moment == PictureMoment::Idle ? recordOpacity : writeOpacity;
	return choice;
}
