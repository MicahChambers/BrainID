#include "DoubleWell.hpp"

#include "indii/ml/sde/StochasticAdaptiveRungeKutta.hpp"

#include <iostream>
#include <fstream>

namespace aux = indii::ml::aux;

/**
 * @file StochasticAdaptiveRungeKuttaHarness.cpp
 *
 * Test of StochasticAdaptiveRungeKutta with DoubleWell model.
 *
 * This test simulates a number of trajectories from DoubleWell using
 * indii::ml::sde::StochasticAdaptiveRungeKutta, plotting each.
 *
 * Results are as follows:
 *
 * \image html StochasticAdaptiveRungeKuttaHarness.png "Results"
 * \image latex StochasticAdaptiveRungeKuttaHarness.eps "Results"
 */

/**
 * Dimensionality of the process.
 */
const unsigned int M = 1;

/**
 * Number of sample trajectories.
 */
const unsigned int N = 1;

/**
 * Time length of each trajectory.
 */
const double LENGTH = 600.0;

/**
 * Run tests.
 */
int main(int argc, const char* argv[]) {
  double t;
  aux::vector y(M);
  unsigned int i;
  std::ofstream fout("results/StochasticAdaptiveRungeKuttaHarness.out");

  DoubleWell model;
  indii::ml::sde::StochasticAdaptiveRungeKutta<> solver(&model);

  solver.setErrorBounds(1.0e-3, 1.0e-2);

  for (i = 0; i < N; i++) {
    t = 0.0;
    y(0) = aux::Random::uniform(-1.0, 1.0);

    solver.setTime(t);
    solver.setState(y);
    solver.setStepSize(1.0e-4);

    while (t < LENGTH) {
      t = solver.step(LENGTH);
      y = solver.getState();

      fout << t << '\t' << y(0) << std::endl;
    }
    fout << std::endl;
  }

  return 0;
}
