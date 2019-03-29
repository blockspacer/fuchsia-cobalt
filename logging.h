// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_LOGGING_H_
#define COBALT_LOGGING_H_

#ifdef HAVE_GLOG
#include <glog/logging.h>

#define INIT_LOGGING(val) \
{ \
  google::InitGoogleLogging(val); \
  google::InstallFailureSignalHandler(); \
}

#elif defined(__Fuchsia__)
#include "src/lib/fxl/logging.h"

#define INIT_LOGGING(val)

#define VLOG(verboselevel) FXL_VLOG(verboselevel)
#define LOG(level) FXL_LOG(level)

#define CHECK(condition) FXL_CHECK(condition)
#define CHECK_EQ(val1, val2) FXL_CHECK((val1 == val2))
#define CHECK_NE(val1, val2) FXL_CHECK((val1 != val2))
#define CHECK_LE(val1, val2) FXL_CHECK((val1 <= val2))
#define CHECK_LT(val1, val2) FXL_CHECK((val1 < val2))
#define CHECK_GE(val1, val2) FXL_CHECK((val1 >= val2))
#define CHECK_GT(val1, val2) FXL_CHECK((val1 > val2))

#else
#error "Either HAVE_GLOG or __Fuchsia__ must be defined"
#endif

#endif  // COBALT_LOGGING_H_
