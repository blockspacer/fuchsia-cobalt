// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "tools/test_app2/test_app.h"

#include "gflags/gflags.h"
#include "glog/logging.h"

int main(int argc, char* argv[]) {
  google::SetUsageMessage(
      "Cobalt test client application.\n"
      "There are three modes of operation controlled by the -mode flag:\n"
      "interactive: The program runs an interactive command-loop.\n"
      "send-once: The program sends a single Envelope described by the flags.\n"
      "automatic: The program runs forever sending many Envelopes with "
      "randomly generated values.");
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  auto app = cobalt::TestApp::CreateFromFlagsOrDie(argc, argv);
  app->Run();

  exit(0);
}
