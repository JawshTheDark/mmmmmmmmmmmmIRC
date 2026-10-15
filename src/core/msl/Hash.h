// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Pure-C++ hex digests for the MSL $md5/$sha1/$sha256 identifiers. Kept Qt-free
// so the engine core (and its tests/engine-probe) stay dependency-light.
#pragma once

#include <string>

namespace lurker::msl {

std::string md5Hex(const std::string& data);     // 32 lowercase hex chars
std::string sha1Hex(const std::string& data);    // 40
std::string sha256Hex(const std::string& data);  // 64

}  // namespace lurker::msl
