#include "remote/UsageLog.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <utility>

namespace {

std::string stamp() {
	using clock = std::chrono::system_clock;
	auto now = clock::now();
	auto secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
	int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now - secs).count());
	std::time_t t = clock::to_time_t(secs);
	std::tm tm{};
	gmtime_r(&t, &tm);
	char buf[40];
	std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	              tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
	return buf;
}

} // namespace

UsageLog::UsageLog(std::filesystem::path path, std::uintmax_t capBytes, int trimEvery)
	: path(std::move(path)), cap(capBytes), trimEvery(trimEvery < 1 ? 1 : trimEvery) {}

void UsageLog::write(std::string_view event, std::string_view name, std::string_view valueJson) {
	if (!out.is_open()) {
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		out.open(path, std::ios::app);
		if (!out) return;
	}
	out << "{\"at\":\"" << stamp() << "\",\"e\":\"" << event << "\",\"name\":\"" << name << '"';
	if (!valueJson.empty()) out << ",\"v\":" << valueJson;
	out << "}\n";
	out.flush();
	if (++sinceTrim < trimEvery) return;
	sinceTrim = 0;
	std::error_code ec;
	auto size = std::filesystem::file_size(path, ec);
	if (!ec && size > cap) trim();
}

void UsageLog::trim() {
	out.close();
	std::ifstream in(path, std::ios::binary);
	std::string data((std::istreambuf_iterator<char>(in)), {});
	in.close();
	auto reopen = [&] {
		out.open(path, std::ios::app);
	};
	size_t cut = data.size() / 2;
	size_t nl = data.find('\n', cut);
	if (nl == std::string::npos) {
		reopen();
		return;
	}
	auto tmp = path;
	tmp += ".tmp";
	{
		std::ofstream rewrite(tmp, std::ios::binary | std::ios::trunc);
		if (!rewrite) {
			reopen();
			return;
		}
		rewrite << data.substr(nl + 1);
	}
	std::error_code ec;
	std::filesystem::rename(tmp, path, ec);
	if (ec) std::filesystem::remove(tmp, ec);
	reopen();
}
