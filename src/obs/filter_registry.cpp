#include "filter_registry.hpp"

#include <algorithm>

namespace nvoice::obs {

FilterRegistry &FilterRegistry::instance()
{
	static FilterRegistry registry;
	return registry;
}

std::uint64_t FilterRegistry::add(obs_source_t *filter, std::shared_ptr<FilterShared> shared)
{
	OBSWeakSource weak = OBSGetWeakRef(filter);

	std::lock_guard lock(mutex_);
	const std::uint64_t id = nextId_++;
	entries_.push_back(FilterHandle{id, std::move(shared), std::move(weak)});
	return id;
}

void FilterRegistry::remove(std::uint64_t id)
{
	// Move the entry out first so its weak ref is released after unlocking.
	FilterHandle removed;
	{
		std::lock_guard lock(mutex_);
		auto it = std::find_if(entries_.begin(), entries_.end(),
				       [id](const FilterHandle &h) { return h.id == id; });
		if (it == entries_.end())
			return;
		removed = std::move(*it);
		entries_.erase(it);
	}
}

std::vector<FilterHandle> FilterRegistry::snapshot() const
{
	std::lock_guard lock(mutex_);
	return entries_;
}

} // namespace nvoice::obs
