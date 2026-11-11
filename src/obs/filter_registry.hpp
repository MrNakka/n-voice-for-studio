// N Voice for Studio - OBS integration
// Tracks live N Voice filter instances so the dock can show their meters.
//
// Threading:
//  - add()/remove() are called from OBS filter create/destroy.
//  - snapshot() is called from the UI thread.
//  - The audio thread never touches the registry.
#pragma once

#include <nvoice/engine/audio_engine.hpp>

#include <obs.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace nvoice::obs {

// State shared between a filter instance and the UI. Kept alive by
// shared_ptr so the UI can finish reading meters even if the filter is
// destroyed mid-frame.
struct FilterShared {
	engine::AudioEngine engine;
};

struct FilterHandle {
	std::uint64_t id = 0;
	std::shared_ptr<FilterShared> shared;
	OBSWeakSource weakFilter; // resolve with obs_weak_source_get_source()
};

class FilterRegistry {
public:
	static FilterRegistry &instance();

	std::uint64_t add(obs_source_t *filter, std::shared_ptr<FilterShared> shared);
	void remove(std::uint64_t id);

	// Copies the current list. Callers must resolve weak refs and release any
	// strong refs outside of registry calls (releasing may destroy a filter,
	// which calls remove()).
	std::vector<FilterHandle> snapshot() const;

private:
	FilterRegistry() = default;

	mutable std::mutex mutex_;
	std::vector<FilterHandle> entries_;
	std::uint64_t nextId_ = 1;
};

} // namespace nvoice::obs
