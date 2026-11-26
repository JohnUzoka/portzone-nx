#pragma once

#include <memory>
#include <string>
#include <vector>

namespace pz::images
{

using Bytes = std::shared_ptr<const std::vector<unsigned char>>;

// A PNG or JPEG: from memory, the SD card cache (fresh for a week), or the network, in that order.
// Blocking; call off the UI thread. nullptr when it cannot be had or is not a PNG/JPEG under `maxBytes`.
Bytes get(const std::string& url, size_t maxBytes);

} // namespace pz::images
