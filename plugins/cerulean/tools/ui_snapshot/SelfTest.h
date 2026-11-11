#pragma once

#include "common/testing/ScratchSession.h"

/** Exercises the editor's interaction logic with synthetic events; returns the number of failures. */
int runUiSelfTest (const myzel::testing::ScratchSession& scratch);
