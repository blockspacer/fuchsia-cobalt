# cobalt_lossmin
A library for solving sparse optimization problems
that appear in the RAPPOR algorithm.

## Overview
*cobalt_lossmin* is a library for solving the convex
optimization problems of the form:

$$ \min_{x} {F(x) = L(x) + l_1 \cdot \| x \|_1 +
l_2 \cdot \| x \|_2^2},$$

where $ L(x): \mathbb{R}^n \to \mathbb{R} $ is a continuosly differentiable
convex function, and $l_1, l_2 \geq 0$ are real numbers;
the minimization is over $x$.

We call $F$ the (penalized) "loss function". We also sometimes call $L$
the "unpenalized loss function".
The two terms involving the norms of $x$ are collectively called
"penalty" or "regularization".

The default definition of $L$ is the linear regression objective
function:

$$ L(x) = \frac{1}{2m} \| A \cdot x - b \|_2^2, $$

where $A$ is a (constant) $m \times n$ real matrix, $b \in \mathbb{R}^m$
is a (constant) vector, and $x \in \mathbb{R}^n$ is the variable over which
we minimize. $A$ is assumed to be sparse.

The implementation is based on lossmin library developed by Nevena Lazic
at Google. The design and a considerable part of the source code are taken
from the lossmin library.

## Design

The *cobalt_lossmin* library implements an (abstract) LossMinimizer class,
an (abstract) GradientEvaluator class,
and a ParallelBoostingWithMomentum class:

1. GradientEvaluator class owns $A, A^T$, and $b$.
Most importantly, it can compute the value of $L(x)$ and its gradient.

2. The LossMinimizer class implements the iterative optimization procedure
*Run* in which $x$ is modified at each iteration (also called epoch)
to decrease the value of $F(x)$, until convergence. The abstract LossMinimizer
class does not specify the specific algorithm used within iterations but it
uses GradientEvaluator class to compute $L(x)$ values and gradients.

3. ParallelBoostingWithMomentum is a class publicly derived from LossMinimizer.
It specifies how the iterations inside Run are performed; it implements
the algorithm described in:
I. Mukherjee, K. Canini, R. Frongillo, and Y. Singer,
*Parallel Boosting with Momentum*, ECML PKDD 2013.
The algorithm uses the information from previous iterations to approximate
the local curvature of the problem in order to choose steps better than pure
gradient descent steps, but without approximating Hessian directly.
Most of computations and memory usage are associated with the gradient
computations which can be parallelized.

## Notes

* The library uses Eigen for linear algebra computations and fully
exploits sparsity of $A$ and $A^T$ (the latter was not the case in the original
lossmin library).
* Parallel version is currently not implemented but the matrix-vector products
can be parallelized.
* Another possible extension to the library could be another class derived from
LossMinimizer (besides ParallelBoostingWithMomentum) that would implement
a single iteration of Run using the coordinate descent algorithm described in
Friedman, Jerome, Trevor Hastie, and Rob Tibshirani.
"Regularization paths for generalized linear models via coordinate descent."
Journal of statistical software 33.1 (2010): 1..



