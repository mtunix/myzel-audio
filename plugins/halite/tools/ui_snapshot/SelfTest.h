#pragma once

#include "common/testing/ScratchSession.h"

/** The editor self-test (--selftest): returns the number of failed checks. */
int runUiSelfTest (const myzel::testing::ScratchSession&);
