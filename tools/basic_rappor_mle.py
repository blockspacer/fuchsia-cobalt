#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Computes the Maximum-Likelihood Estimator for Basic Rappor
# This script is only for manual use by Cobalt developers. It is not part
# of the Cobalt runtime. It is not part of any automated system. It is not
# tested as part of the standard Cobalt tests but it does include some
# tests that may be run manually.


# Computes n-choose-k. This code was copied from:
# http://stackoverflow.com/questions/3025162/statistics-combinations-in-python
def choose(n, k):
  """
    A fast way to calculate binomial coefficients by Andrew Dalke (contrib).
    """
  if 0 <= k <= n:
    ntok = 1
    ktok = 1
    for t in xrange(1, min(k, n - k) + 1):
      ntok *= n
      ktok *= t
      n -= 1
    return ntok // ktok
  else:
    return 0


# Tests the function choose(). The test prints an error message and returns
# False if it finds an error in the choose() function.
def test_choose():
  for n in xrange(0, 100, 11):
    sum = 0
    for k in xrange(0, n + 1):
      sum = sum + choose(n, k)
    if sum != 2**n:
      print "**** test_choose failed for n=%d with sum=" % sum
      return False
  return True


# Computes and returns the value of the probability mass function
# Prob(Y=y) where Y ~ Binomial(q, lam) + Binomial(p, n - lam)
# p and q must be floats in the range [0, 1]
# lam, n and y must be integers satisfying
# n > 0
# 0 <= lam <= n
# 0 <= y <= n
def pmf(lam, n, y, p, q):
  prob = 0
  # "lam" stands for lambda which is a reserved word in Python.
  #
  # Consider Y to be the sum of lambda Bernoulli(q) variables and n - lambda
  # Bernoulli(p) variables. We think of the desired probability as being
  # the sum of terms from min_i to max_i where each term is the probability
  # that i of the Bernoulli(q) variables and y - i of the Bernoulli(p) variables
  # equal 1 and the other Bernoulli variables equal 0.
  min_i = max(0, y + lam - n)
  max_i = min(y, lam)
  for i in xrange(min_i, max_i + 1):
    prob = (
        prob + choose(lam, i) * q**i *
        (1.0 - q)**(lam - i) * choose(n - lam, y - i) * p**(y - i) *
        (1.0 - p)**(n - lam - y + i))
  return prob


# Computes and returns the value of lambda that maximizes the value
# of pmf(lambda, n, y, p, q) over all lambda from 0 to n.
def mle(n, y, p, q):
  best_estimate = 0
  max_prob = 0
  for lam in xrange(0, n + 1):
    prob = pmf(lam, n, y, p, q)
    if prob > max_prob:
      max_prob = prob
      best_estimate = lam
  return best_estimate


# Tests that the function pmf is in fact a probability mass function for
# the given parameters. The test prints a failure message and returns False
# if the sum of pmf(lam, n, y, p, q) over y in [0, n] is not equal to 1
def do_pmf_test(lam, n, p, q):
  prob = 0
  for y in xrange(0, n + 1):
    prob = prob + pmf(lam, n, y, p, q)
  if prob < 0.999999 or prob > 1.000001:
    print "**** test_pmf failed for lam=%d, n=%d, p=%f, q=%f, prob=%f" % (
        lam, n, p, q, prob)
    return False
  return True


def test_pmf():
  for n in {1, 10, 31, 54}:
    for lam in xrange(0, n + 1):
      q = 0.9
      p = 0.1
      if not do_pmf_test(lam, n, p, q):
        return False
      q = 0.75
      p = 0.25
      if not do_pmf_test(lam, n, p, q):
        return False
      q = 0.7
      p = 0.4
      if not do_pmf_test(lam, n, p, q):
        return False
      q = 0.7
      p = 0.2
      if not do_pmf_test(lam, n, p, q):
        return False
      q = 0.5
      p = 0.0
      if not do_pmf_test(lam, n, p, q):
        return False
  return True


def main():
  n = 100
  p = 0.2
  q = 0.8
  print "\n"
  print "Basic RAPPOR Maximum-Likelihood Estimates"
  print "p=%f q=%f n=%d\n" % (p, q, n)
  for y in xrange(0, n + 1):
    print "--------------------------------------"
    print "For y=%d..." % y
    print "unbiased estimate=%f" % ((float(y) - n * p) / (q - p))
    print "maximum-likelihood estimate=%d" % mle(n, y, p, q)

  print "\n"
  print "--------------------------------------"
  print "--------------------------------------"
  print "Running tests of this script"
  if test_choose():
    print "test_choose passed"
  if test_pmf():
    print "test_pmf passed"


if __name__ == "__main__":
  main()
