// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_SRC_TRACING_H_
#define COBALT_SRC_TRACING_H_

#if defined(__Fuchsia__)

#include <trace/event.h>

#else

#define TRACE_INSTANT(args...)
#define TRACE_COUNTER(args...)
#define TRACE_DURATION(args...)
#define TRACE_DURATION_BEGIN(args...)
#define TRACE_DURATION_END(args...)
#define TRACE_ASYNC_BEGIN(args...)
#define TRACE_ASYNC_INSTANT(args...)
#define TRACE_ASYNC_END(args...)
#define TRACE_FLOW_BEGIN(args...)
#define TRACE_FLOW_STEP(args...)
#define TRACE_FLOW_END(args...)
#define TRACE_KERNEL_OBJECT(args...)
#define TRACE_BLOB(args...)

#endif

#endif  // COBALT_SRC_TRACING_H_
