# lib::statusor::StatusOr
This is a copy of the StatusOr library from tensorflow.

You can find the original source of these files at:
https://github.com/tensorflow/tensorflow/blob/master/tensorflow/compiler/xla

If you want to update the libraries, replace the xla namespace with
statusor, and fix the includes to replace tensorflow/compiler/xla/
with src/lib/statusor. We also use Cobalt's Status instead of
tensorflow's Status class. The final change is to remove references to the TF_*
macros that cobalt does not use.

This library should be deleted and replaced with Abseil once they add StatusOr.
