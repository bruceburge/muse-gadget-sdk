#pragma once
// Host mock of sdkconfig.h: only the Kconfig symbols the units under test need.
// (The real image_fetch.h is used in host tests; this makes it compile.)
#define CONFIG_HOMEHUB_LED_BACKEND_MUSE 1
