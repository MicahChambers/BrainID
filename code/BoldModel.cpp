#include "BoldModel.hpp"

#include <indii/ml/aux/matrix.hpp>

BoldModel::BoldModel() : theta_sigmas(THETA_SIZE)
{
    if(THETA_SIZE + STATE_SIZE*SIMUL_STATES != SYSTEM_SIZE) {
        std::cerr << "Incorrect system size" << std::endl;
        exit(-1);
    }

    theta_sigmas(TAU_S) = 1.07/8;
    theta_sigmas(TAU_F) = 1.51/8;
    theta_sigmas(EPSILON) = .014/8;
    theta_sigmas(TAU_0) = 1.5/8;
    theta_sigmas(ALPHA) = .004/8;
    theta_sigmas(E_0) = .072/8;
    theta_sigmas(V_0) = .006/8;

    small_g = .95e-5;
    //  var_e = 3.92e-6;
    var_e = 3.92e-3;
    sigma_e = sqrt(var_e);
}

BoldModel::~BoldModel()
{

}

//TODO, I would like to modify these functions so that the vector s
//will just be modified in place, which would reduce the amount of copying
//necessary. This might not work though, because Particle Filter likes to
//keep all the particles around for history.
aux::vector BoldModel::transition(const aux::vector& s,
        const double t, const double delta)
{
    aux::zero_vector u(INPUT_SIZE);
    return transition(s, t, delta, u);
}

aux::vector BoldModel::transition(const aux::vector& dustin,
        const double time, const double delta_t, const aux::vector& u_t)
{
    aux::vector dustout(SYSTEM_SIZE);
    static aux::symmetric_matrix cov(1);
    cov(0,0) = 1;
    static aux::GaussianPdf rng(aux::zero_vector(1), cov);
//    double v_t;
 
    //transition the parameters
    //the GaussianPdf class is a little shady for non univariate, zero-mean
    //cases, so we will just sample from the N(0,1) case and then convert
    //the variables to a correct variance by multiplying by the std-dev
    //The std-deviations are 1/2 the stated std-deviations listed in 
    //Johnston et al.
    dustout(TAU_S)   = dustin(TAU_S)   + rng.sample()[0] * theta_sigmas(TAU_S);
    dustout(TAU_F)   = dustin(TAU_F)   + rng.sample()[0] * theta_sigmas(TAU_F);
    dustout(EPSILON) = dustin(EPSILON) + rng.sample()[0] * theta_sigmas(EPSILON);
    dustout(TAU_0)   = dustin(TAU_0)   + rng.sample()[0] * theta_sigmas(TAU_0);
    dustout(ALPHA)   = dustin(ALPHA)   + rng.sample()[0] * theta_sigmas(ALPHA);
    dustout(E_0)     = dustin(E_0)     + rng.sample()[0] * theta_sigmas(E_0);
    dustout(V_0)     = dustin(V_0)     + rng.sample()[0] * theta_sigmas(V_0);

    //transition the actual state variables
    //TODO, potentially add some randomness here.
    for(int ii=0 ; ii<SIMUL_STATES ; ii++) {
        //This is a bit of a kludge, but it is unavoidable right now
        //once this function returns void and dustin isn't const this
        //won't be as necessary, or this could be done when the prior
        //is generated. 
        //Note: Removed code because weight SHOULD filter out NAN particles
//        v_t = dustin[indexof(V_T,ii)];
//        if(v_t < 0) {
//            fprintf(stderr, "Warning, had to move volume to");
//            fprintf(stderr, "zero because it was negative\n");
//            v_t = 0;
//        }
        //V_t* = (1/tau_0) * ( f_t - v_t ^ (1/\alpha)) 
        double dot = (  ( dustin[indexof(F_T,ii)] - 
                    pow(dustin[indexof(V_T,ii)], 1./dustin[ALPHA]) ) / 
                    dustin[TAU_0]  )  + (rng.sample())[0];
        dustout[indexof(V_T,ii)] = dustin[indexof(V_T,ii)] + dot*delta_t;
//        if(dustout[indexof(V_T,ii)] < 0)
//            dustout[indexof(V_T,ii)] = 0;

        //Q_t* = \frac{1}{tau_0} * (\frac{f_t}{E_0} * (1- (1-E_0)^{1/f_t}) - 
        //              \frac{q_t}{v_t^{1-1/\alpha})
        double tmpA = (dustin[indexof(F_T,ii)] / dustin[E_0]) * 
                    (1 - pow( 1. - dustin[E_0], 1./dustin[indexof(F_T,ii)]));
        double tmpB = dustin[indexof(Q_T,ii)] / 
                    pow(dustin[indexof(V_T,ii)], 1.-1./dustin[ALPHA]);
        dot =  ( tmpA - tmpB )/dustin[TAU_0];
        dustout[indexof(Q_T,ii)] = dustin[indexof(Q_T,ii)] + dot*delta_t;

        //S_t* = \epsilon*u_t - 1/\tau_s * s_t - 1/\tau_f * (f_t - 1)
        dot = u_t[0]*dustin[EPSILON]- dustin[indexof(S_T,ii)]/dustin[TAU_S] - 
                    (dustin[indexof(F_T,ii)] - 1.) / dustin[TAU_F];
        dustout[indexof(S_T,ii)] = dustin[indexof(S_T,ii)] + dot*delta_t;

        //f_t* = s_t;
        dot = dustin[indexof(S_T,ii)];
        dustout[indexof(F_T,ii)] = dustin[indexof(F_T,ii)] + dot*delta_t;
    }

    //std::cerr  <<"Printing output state" << endl;
//    outputVector(std::cerr, w);
//    std::cerr << endl;
    return dustout;
}

aux::vector BoldModel::measure(const aux::vector& s)
{
    aux::vector y(MEAS_SIZE);
    for(int i = 0 ; i < MEAS_SIZE ; i++) {
        y[i] = s[V_0] * ( A1 * ( 1 - s[indexof(Q_T,i)]) - A2 * (1 - s[indexof(V_T,i)]));
    }
    return y;
}

double BoldModel::weight(const aux::vector& s, const aux::vector& y)
{
    //these are really constant throughout the execution
    //of the program, so no need to calculate over and over
    static aux::symmetric_matrix cov(1);
    cov(0,0) = 1;
    static aux::GaussianPdf rng(aux::zero_vector(MEAS_SIZE), cov);
    
    aux::vector location(MEAS_SIZE);
//    fprintf(stderr, "Actual:\n");
//    outputVector(std::cerr, y);
//    fprintf(stderr, "Measure: \n");
//    outputVector(std::cerr, measure(s));
//    fprintf(stderr, "\nParticle:\n");
//    outputVector(std::cerr , s);
//    fprintf(stderr, "\n");
    
    location = (y-measure(s))/sigma_e;
//    fprintf(stderr, "Location calculated:\n");
//    outputVector(std::cerr , location);
//    fprintf(stderr, "\n");
    return rng.densityAt(location);
//    fprintf(stderr, "Weight calculated: %e\n", out);
//    return out;
}

//TODO make some of these non-gaussian
aux::GaussianPdf BoldModel::suggestPrior()
{
    //set the averages of the variables
    aux::vector mu(SYSTEM_SIZE);
    mu[TAU_S] = 4.98;
    mu[TAU_F] = 8.31;
    mu[EPSILON] = 0.069;
    mu[TAU_0] = 8.38;
    mu[ALPHA] = .189;
    mu[E_0] = .635;
    mu[V_0] = 1.49e-2;
    for(int ii = THETA_SIZE ; ii < SYSTEM_SIZE; ii++) {
        mu[ii] = 1;
    }

    //set the variances, assume independence between the variables
    aux::symmetric_matrix sigma(SYSTEM_SIZE);
    sigma.clear();
    sigma(TAU_S, TAU_S) = 1.07;
    sigma(TAU_F, TAU_F) = 1.51;
    sigma(EPSILON, EPSILON) = .014;
    sigma(TAU_0, TAU_0) = 1.50;
    sigma(ALPHA, ALPHA) = .004;
    sigma(E_0, E_0) = .072;
    sigma(V_0, V_0) = .006;

    std::cerr << "Mu:" << std::endl;
    outputVector(std::cerr, mu);
    std::cerr << std::endl;
    
    std::cerr << "sigma:" << std::endl;
    outputMatrix(std::cerr, sigma);
    std::cerr << std::endl;

    for(int ii = THETA_SIZE ; ii < SYSTEM_SIZE; ii++) {
        sigma(ii,ii) = .75;
    }

    return aux::GaussianPdf(mu, sigma);
}


void outputVector(std::ostream& out, aux::vector vec) {
  aux::vector::iterator iter, end;
  iter = vec.begin();
  end = vec.end();
  while (iter != end) {
    out << *iter;
    iter++;
    if (iter != end) {
      out << ' ';
    }
  }
}

void outputMatrix(std::ostream& out, aux::matrix mat) {
  unsigned int i, j;
  for (j = 0; j < mat.size2(); j++) {
    for (i = 0; i < mat.size1(); i++) {
      out << mat(i,j);
      if (i != mat.size1() - 1 || j != mat.size2() - 1) {
	out << '\t';
      }
    }
  }
}
