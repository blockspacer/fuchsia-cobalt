// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements reading the Cobalt configuration from a git repository.

package config_parser

import (
	"fmt"
	"io/ioutil"
	"net/url"
	"os"
	"os/exec"
	"time"
)

// Clones the specified repository to the specified destination.
func cloneRepo(repoUrl string, destination string, gitTimeout time.Duration) error {
	cmd := exec.Command("git", "clone",
		// Truncate the history to the latest commit.
		"--depth", "1",
		repoUrl, destination)

	// *exec.ExitError is the documented return type of Cmd.Run().
	if err := cmd.Start(); err != nil {
		return err
	}

	// We implement a timeout running cmd.
	done := make(chan error)
	// In a separate goroutine, we wait on cmd to return. When that happens,
	// the result of cmd.Wait is sent via the "done" channel serving as a signal
	// that we are done waiting.
	go func() { done <- cmd.Wait() }()

	// Here, we select on channels. In effect, we wait to see whether a value is
	// sent through "done" first (indicating the command is done running) or
	// if the time.After timer fires first. If the latter occurs, we kill the
	// process started by cmd and return an error.
	select {
	case err := <-done:
		return err
	case <-time.After(gitTimeout):
		cmd.Process.Kill()
		return fmt.Errorf("git took too long to run.")
	}

	return nil
}

// Only allow URLs served over HTTPS.
func checkUrl(repoUrl string) (err error) {
	u, err := url.Parse(repoUrl)
	if err != nil {
		return err
	}

	if u.Scheme != "https" {
		return fmt.Errorf("For security reasons, we only accept repositories served over https.")
	}

	return nil
}

// ReadConfigFromRepo clones repoUrl into a temporary directory and reads the
// configuration from it. For the organization expected of the repository, see
// ReadConfigFromDir in config_reader.go.
// gitTimeout is the maximum amount of time to wait for a git command to finish.
func ReadConfigFromRepo(repoUrl string, gitTimeout time.Duration) (c []ProjectConfig, err error) {
	if err = checkUrl(repoUrl); err != nil {
		return c, err
	}

	repoPath, err := ioutil.TempDir(os.TempDir(), "cobalt_config")
	if err != nil {
		return c, err
	}

	defer os.RemoveAll(repoPath)

	if err := cloneRepo(repoUrl, repoPath, gitTimeout); err != nil {
		return c, fmt.Errorf("Error cloning repository (%v): %v", repoUrl, err)
	}

	return ReadConfigFromDir(repoPath)
}
