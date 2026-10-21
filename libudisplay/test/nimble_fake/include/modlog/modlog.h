#pragma once
/* Logging is dropped in the fake; arguments are still type-checked. */
#include <stdio.h>
#define MODLOG_DFLT(level, ...) do { if (0) printf(__VA_ARGS__); } while (0)
