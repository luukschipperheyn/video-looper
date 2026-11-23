#include "io/PageCache.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

bool same(float a, float b) {
	return std::fabs(a - b) < 1e-4f;
}

bool contains(const std::vector<std::string>& paths, const std::string& path) {
	return std::find(paths.begin(), paths.end(), path) != paths.end();
}

} // namespace

PageCache::PageCache() {
	worker = std::thread(&PageCache::run, this);
}

PageCache::~PageCache() {
	shutdown();
}

void PageCache::shutdown() {
	{
		std::lock_guard<std::mutex> lock(mu);
		if (stopped) return;
		stopped = true;
		quit = true;
	}
	cv.notify_all();
	if (worker.joinable()) worker.join();
}

void PageCache::setPage(std::vector<std::string> paths, float start, float length) {
	std::vector<std::string> unique;
	for (std::string& path : paths) {
		if (!contains(unique, path)) unique.push_back(std::move(path));
	}
	{
		std::lock_guard<std::mutex> lock(mu);
		bool samePage = unique == wanted && same(start, loopStart) && same(length, loopLength);
		if (samePage) return;
		bool sameRange = same(start, loopStart) && same(length, loopLength);
		wanted = std::move(unique);
		loopStart = start;
		loopLength = length;
		std::vector<Slot> stay;
		for (Slot& slot : slots) {
			if (sameRange && contains(wanted, slot.path))
				stay.push_back(std::move(slot));
			else if (slot.source)
				retired.push_back(std::move(slot.source));
		}
		slots = std::move(stay);
	}
	cv.notify_one();
}

bool PageCache::take(const std::string& path, float start, float length, AVFFileSource& out,
                     std::vector<unsigned char>& frame, int& w, int& h) {
	{
		std::lock_guard<std::mutex> lock(mu);
		if (!same(start, loopStart) || !same(length, loopLength)) return false;
		auto it = std::find_if(slots.begin(), slots.end(),
		                       [&](const Slot& slot) { return slot.ready && slot.path == path && slot.source; });
		if (it == slots.end()) return false;
		out.swap(*it->source);
		frame = std::move(it->frame);
		w = it->w;
		h = it->h;
		slots.erase(it);
		// The caller now owns this decoder. Leave it out of the page so it is not opened again beside the live one.
		wanted.erase(std::remove(wanted.begin(), wanted.end(), path), wanted.end());
	}
	return true;
}

void PageCache::retire(AVFFileSource& src) {
	auto held = std::make_unique<AVFFileSource>();
	held->swap(src);
	{
		std::lock_guard<std::mutex> lock(mu);
		retired.push_back(std::move(held));
	}
	cv.notify_one();
}

bool PageCache::missing() const {
	for (const std::string& path : wanted) {
		bool have = false;
		for (const Slot& slot : slots)
			have = have || slot.path == path;
		if (!have) return true;
	}
	return false;
}

std::string PageCache::nextMissing() const {
	for (const std::string& path : wanted) {
		bool have = false;
		for (const Slot& slot : slots)
			have = have || slot.path == path;
		if (!have) return path;
	}
	return {};
}

void PageCache::run() {
	for (;;) {
		std::string path;
		float start = 0;
		float length = 1;
		std::vector<std::unique_ptr<AVFFileSource>> trash;
		{
			std::unique_lock<std::mutex> lock(mu);
			cv.wait(lock, [&] { return quit || !retired.empty() || missing(); });
			if (quit) {
				for (Slot& slot : slots)
					if (slot.source) trash.push_back(std::move(slot.source));
				slots.clear();
			}
			for (auto& held : retired)
				trash.push_back(std::move(held));
			retired.clear();
			if (!quit) {
				path = nextMissing();
				start = loopStart;
				length = loopLength;
			}
		}
		for (auto& held : trash)
			if (held) held->stop();
		if (quit) return;
		if (path.empty()) continue;

		auto src = std::make_unique<AVFFileSource>();
		std::vector<unsigned char> frame;
		int w = 0;
		int h = 0;
		bool ok = src->startFile(path);
		if (ok) {
			float from = std::clamp(start, 0.0f, 1.0f);
			float len = std::clamp(length, 0.0f, 1.0f);
			float to = std::clamp(from + len, from + 0.001f, 1.0f);
			src->setLoopRange(from, to);
			ok = src->copyFrame(frame, w, h);
		}
		if (!ok) src->stop();

		bool discard = false;
		{
			std::lock_guard<std::mutex> lock(mu);
			bool wantedStill = contains(wanted, path);
			bool rangeStill = same(start, loopStart) && same(length, loopLength);
			auto existing =
				std::find_if(slots.begin(), slots.end(), [&](const Slot& slot) { return slot.path == path; });
			if (quit || !wantedStill || !rangeStill || existing != slots.end())
				discard = true;
			else {
				Slot slot;
				slot.path = path;
				if (ok) slot.source = std::move(src);
				slot.frame = std::move(frame);
				slot.w = w;
				slot.h = h;
				slot.ready = ok;
				slot.failed = !ok;
				slots.push_back(std::move(slot));
			}
		}
		if (discard && src) src->stop();
	}
}
