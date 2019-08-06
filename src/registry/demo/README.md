# Cobalt Demo Config Registration Files

These config registration files are used for a few purposes:
* Manually doing an end-to-end demo of Cobalt.
* Running the automated end-to-end tests
* Running the continuous in-the-cloud test.

There are therefore a few places in the code base that have been hard-coded
to expect particular contents of these files. If you make any changes to
these files you must synchronize your changes with the following code
locations:

* end_to_end_tests/cobalt_e2e_test.go
* tools/demo/demo_reporter.py
