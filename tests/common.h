#pragma once
#include <unordered_map>

#include "oxenc/base32z.h"
#include "oxenc/base64.h"
#include "oxenc/bt.h"
#include "oxenc/hex.h"
#include "oxenc/span.h"

using namespace oxenc;

// NOTE: has to be AFTER the oxenc includes
#include <catch2/catch.hpp>
