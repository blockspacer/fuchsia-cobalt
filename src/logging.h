// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_LOGGING_H_
#define COBALT_SRC_LOGGING_H_

#ifdef HAVE_GLOG
#include <glog/logging.h>

#define INIT_LOGGING(val)                  \
  {                                        \
    google::InitGoogleLogging(val);        \
    google::InstallFailureSignalHandler(); \
  }

#elif defined(__Fuchsia__)
#include "lib/syslog/cpp/logger.h"
#include "src/lib/fxl/logging.h"

#define INIT_LOGGING(val)

#define VLOG(verboselevel) FX_VLOGST(verboselevel, "core")
#define LOG(level) FX_LOGST(level, "core")
#define LOG_FIRST_N(verboselevel, n) FX_LOGST_FIRST_N(verboselevel, n, "core")

#define CHECK(condition) FX_CHECKT(condition, "core")
#define CHECK_EQ(val1, val2) FX_CHECKT((val1 == val2), "core")
#define CHECK_NE(val1, val2) FX_CHECKT((val1 != val2), "core")
#define CHECK_LE(val1, val2) FX_CHECKT((val1 <= val2), "core")
#define CHECK_LT(val1, val2) FX_CHECKT((val1 < val2), "core")
#define CHECK_GE(val1, val2) FX_CHECKT((val1 >= val2), "core")
#define CHECK_GT(val1, val2) FX_CHECKT((val1 > val2), "core")

#else
#error "Either HAVE_GLOG or __Fuchsia__ must be defined"
#endif

#endif  // COBALT_SRC_LOGGING_H_
