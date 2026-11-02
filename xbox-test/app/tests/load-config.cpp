// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse every donor mutation against the actual launcher mapper, including
// mapped-only loads that Play may later prepare for executable guest code.
#define CONFIG_TEST_MAPPER "../../../app/host/wxl_mapper.h"
#define CONFIG_TEST_MAPPED 1
#include "../../uwph/host/tests/load-config-test.cpp"
