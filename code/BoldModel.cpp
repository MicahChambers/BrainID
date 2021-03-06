#include "BoldModel.hpp"
#include "tools.h"

#include <indii/ml/aux/matrix.hpp>
#include <indii/ml/aux/GaussianPdf.hpp>
#include <indii/ml/aux/DiracPdf.hpp>

#include <boost/math/special_functions/fpclassify.hpp>

#include <vector>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>

static const char* stateString[] = {"TAU_0", "ALPHA", "E_0", "V_0", "TAU_S", 
                "TAU_F", "EPSILON", "V_T", "Q_T", "S_T", "F_T"};

const double PI = acos(-1);

/* Constructor:
 * stddev     - the standard deviation of the weighting function
 * weightfunc - enum telling which weight function to use
 * sections   - the number of measurements per measurement time, (1 for single voxel)
 */
BoldModel::BoldModel(aux::vector stddev, int weightfunc, size_t sections) : 
            weightf(weightfunc), sigma(stddev), SIMUL_STATES(sections),
            STATE_SIZE(GVAR_SIZE+LVAR_SIZE*sections + sections),
            MEAS_SIZE(sections),
            INPUT_SIZE(1)//, segments(sections)
{
    defaultstate = defloc(sections);

    //this is only a problem if the user put in a bad vector
    //in which case the u will be overwritten with 0's
    this->input = aux::zero_vector(sections);
    
    std::cerr << "Sizes:" << std::endl;
    std::cerr << "GVAR_SIZE    " << this->GVAR_SIZE    << std::endl;
    std::cerr << "LVAR_SIZE    " << this->LVAR_SIZE    << std::endl;
    std::cerr << "SIMUL_STATES " << this->SIMUL_STATES << std::endl;
    std::cerr << "STATE_SIZE   " << this->STATE_SIZE   << std::endl;
    std::cerr << "MEAS_SIZE    " << this->MEAS_SIZE    << std::endl;
    std::cerr << "INPUT_SIZE   " << this->INPUT_SIZE   << std::endl;

    std::cerr << "Sigma:" << std::endl;
    for(unsigned int i = 0 ; i < this->sigma.size() ; i++)
        std::cerr << this->sigma(i) << std::endl;
}

BoldModel::~BoldModel()
{

}

/* Returns the Steady state measurement level for the given parameters */
aux::vector BoldModel::steadyMeas(const aux::vector& dustin, const aux::vector u_t)
{
    aux::vector out = dustin;
    for(unsigned int ii=0 ; ii<SIMUL_STATES ; ii++) {
        out[indexof(S_T,ii)] = 0;
        out[indexof(F_T,ii)] = out[indexof(TAU_F, ii)]*
                    out[indexof(EPSILON, ii)]*u_t[0] + 1;
        out[indexof(V_T,ii)] = pow(out[indexof(F_T,ii)], 
                    out[indexof(ALPHA,ii)]);
        out[indexof(Q_T,ii)] = (out[indexof(V_T,ii)]/out[indexof(E_0, ii)])*
                    (1-pow(1-out[indexof(E_0,ii)], 1/out[indexof(F_T,ii)]));
    }
    return measure(out);
}

/* Returns the Steady state "state variables" for the given parameters */
aux::vector BoldModel::steadyState(const aux::vector& dustin, const aux::vector u_t)
{
    aux::vector out = dustin;
    for(unsigned int ii=0 ; ii<SIMUL_STATES ; ii++) {
        out[indexof(S_T,ii)] = 0;
        out[indexof(F_T,ii)] = out[indexof(TAU_F, ii)]*
                    out[indexof(EPSILON, ii)]*u_t[0] + 1;
        out[indexof(V_T,ii)] = pow(out[indexof(F_T,ii)], 
                    out[indexof(ALPHA,ii)]);
        out[indexof(Q_T,ii)] = (out[indexof(V_T,ii)]/out[indexof(E_0, ii)])*
                    (1-pow(1-out[indexof(E_0,ii)], 1/out[indexof(F_T,ii)]));
    }
    return out;
}

/* Integrates the state space model (s) for delta seconds (usually very small),
 * t isn't usually used because the function is time invariant
 */
int BoldModel::transition(aux::vector& s, const double t, const double delta) const
{
    //use the default input
    return transition(s, t, delta, input);
}

/* Integrates the state space model (s) for delta seconds (usually very small),
 * t isn't usually used because the function is time invariant
 */
int BoldModel::transition(aux::vector& dustin, const double time, 
            const double delta_t, const aux::vector& u_t) const
{
    double dotV, dotQ, dotS;
    double tmpA, tmpB;
    double tmp = 0;

    //transition the actual state variables
    for(unsigned int ii=0 ; ii<SIMUL_STATES ; ii++) {
        unsigned int v_t = indexof(V_T,ii);
        unsigned int q_t = indexof(Q_T,ii);
        unsigned int s_t = indexof(S_T,ii);
        unsigned int f_t = indexof(F_T,ii);
        // Normalized Blood Volume
        //V_t* = (1/tau_0) * ( f_t - v_t ^ (1/\alpha)) 
        dotV = (  ( dustin[f_t] - 
                    pow(dustin[v_t], 1./dustin[indexof(ALPHA, ii)]) ) / 
                    dustin[indexof(TAU_0,ii)]  );

        // Normalized Deoxyhaemoglobin Content
        //Q_t* = \frac{1}{tau_0} * (\frac{f_t}{E_0} * (1- (1-E_0)^{1/f_t}) - 
        //              \frac{q_t}{v_t^{1-1/\alpha})
        tmpA = (dustin[f_t] / dustin[indexof(E_0, ii)]) * 
                    (1 - pow( 1. - dustin[indexof(E_0, ii)], 1./dustin[f_t]));
        tmpB = dustin[q_t] / 
                    pow(dustin[v_t], 1.-1./dustin[indexof(ALPHA,ii)]);
        dotQ =  ( tmpA - tmpB )/dustin[indexof(TAU_0,ii)];

        // Second Derivative of Cerebral Blood Flow
        //S_t* = \epsilon*u_t - 1/\tau_s * s_t - 1/\tau_f * (f_t - 1)
        dotS = u_t[0]*dustin[indexof(EPSILON, ii)] 
                    - dustin[s_t]/dustin[indexof(TAU_S, ii)]
                    - (dustin[f_t] - 1.) / dustin[indexof(TAU_F,ii)];
        // Normalized Cerebral Blood Flow
        //f_t* = s_t;
        dustin[f_t] += dustin[s_t]*delta_t;
        dustin[v_t] += dotV*delta_t;
        dustin[q_t] += dotQ*delta_t;
        dustin[s_t] += dotS*delta_t;
        tmp += dustin[f_t] + dustin[v_t] + dustin[q_t] + dustin[s_t];
    }

    /* Check for Nan, (nan operated with anything is nan),
     * inf - inf = nan, so nan or inf in any member 
     * will cause this to fail
    */
    if(boost::math::isnan<double>(tmp) || boost::math::isinf<double>(tmp)) {
        dustin = defaultstate;
        return -1;
    }
        
    return 0;
}

/* Given the particle, measure the output */
aux::vector BoldModel::measure(const aux::vector& s) const
{
    aux::vector y(MEAS_SIZE);
    for(size_t i = 0 ; i < MEAS_SIZE ; i++) {
        double k1 = this->k1*s[indexof(E_0,i)];
        double k2 = this->k2*s[indexof(E_0,i)];
        double k3 = this->k3;
        y[i] = s[indexof(V_0, i)]* ((k1 + k2)*(1 - s[indexof(Q_T,i)]) -
                    (k2+k3)*(1 - s[indexof(V_T,i)]));
    }
    return y;
} 
/* Calculate the weight at the current time, given a particle, s, 
 * and a measurement, y
 * 
 * If s[STATE_SIZE-MEAS_SIZE+1] is negative, then this is working
 * with a "drift" variable, aka an arbitrary number is being ADDED
 * to the measurement to account for low frequency drift. In this case
 * y should be the RAW Bold signal
 * The drift variable will adapt in due course of the regularized particle
 * filter, as long as there was some variance in the initial distribution of
 * these values
 *
 * If s[STATE_SIZE-MEAS_SIZE+1] is positive, then it should be a difference
 * term from a previous measurement, in which case y should be the delta
 * in the Bold from a previous measurement. It is up to the user to make
 * sure those s values are correctly inserted.
 *
 * If s[STATE_SIZE-MEAS_SIZE+1] is 0, then y should be from a signal
 * with drift removed by some extra-pf method. The variance in these
 * drift terms will be 0, which will result in no variance from the
 * regularized particle filter. Thus they will stay 0 perpetually.
 *
*/
double BoldModel::weight(const aux::vector& s, const aux::vector& y) const
{
    aux::vector meas = measure(s);
    double weight = 1;
    switch (weightf) {
        /* Cauchy Distribution */
        case CAUCHY:
    	for(unsigned int i = 0 ; i < MEAS_SIZE ; i++)
            weight *= gsl_ran_cauchy_pdf( y[i]-meas[i]+
                        s[STATE_SIZE-MEAS_SIZE+i], sigma(i));
        break;
        /* Hyperbolic "Distribution" */
        case HYP:
    	for(unsigned int i = 0 ; i < MEAS_SIZE ; i++)
            weight *= sigma(i)/(y[i]-meas[i]+
                        s[STATE_SIZE-MEAS_SIZE+i]);
        break;
        /* Laplace Distribution */
        case LAPLACE:
    	for(unsigned int i = 0 ; i < MEAS_SIZE ; i++)
            weight *= gsl_ran_laplace_pdf( y[i]-meas[i]+
                        s[STATE_SIZE-MEAS_SIZE+i], sigma(i)/sqrt(2));
        break;
        /* Normal Distribution (default) */
        case NORM:
    	for(unsigned int i = 0 ; i < MEAS_SIZE ; i++)
            weight *= gsl_ran_gaussian_pdf( y[i]-meas[i]+
                        s[STATE_SIZE-MEAS_SIZE+i], sigma(i));
        default:
        break;
    }
    return fabs(weight);
}


//Note that scale contains std. deviation OR k and loc contains either
//mean or theta depending on the distribution
double BoldModel::generateComponent(gsl_rng* rng, aux::vector& fillme, 
            const std::vector<BoldModel::Dist>& dists) const
{
    double weight, tmp;
    //going to distribute all the state variables the same even if they are
    //in different sections

    double A;
    double B;
    weight = 1;
    for(size_t i = 0 ; i< dists.size(); i++) {
        if(dists[i].type == NORMAL) {
            tmp = gsl_ran_gaussian(rng, dists[i].scale);
            fillme[i] = tmp + dists[i].loc;
            weight *= gsl_ran_gaussian_pdf(tmp, dists[i].scale);
        } else if( dists[i].type == GAMMA_MODE) {
            B = (dists[i].loc + sqrt(pow(dists[i].loc,2)+4*pow(dists[i].scale,2)))/2;
            A = dists[i].loc/B + 1;
            fillme[i] = gsl_ran_gamma(rng, A, B);
            weight *= gsl_ran_gamma_pdf(fillme[i], A, B);
        } else if(dists[i].type == GAMMA_MU ) {
            B = pow(dists[i].scale, 2)/dists[i].loc;
            A = dists[i].loc/B;
            fillme[i] = gsl_ran_gamma(rng, A, B);
            weight *= gsl_ran_gamma_pdf(fillme[i], A, B);
        } else if(dists[i].type == EXP_MEAN ) {
            fillme[i] = gsl_ran_exponential(rng, dists[i].loc);
            weight *= gsl_ran_exponential_pdf(fillme[i], dists[i].loc);
        } else if(dists[i].type == EXP_STD ) {
            fillme[i] = gsl_ran_exponential(rng, dists[i].scale);
            weight *= gsl_ran_exponential_pdf(fillme[i], dists[i].scale);
        } else if(dists[i].type == RAYLEIGH_MEAN) {
            fillme[i] = gsl_ran_rayleigh(rng, dists[i].loc*sqrt(2./PI));
            weight *= gsl_ran_rayleigh_pdf(fillme[i], dists[i].loc*sqrt(2./PI));
        } else if(dists[i].type == RAYLEIGH_STD ) {
            fillme[i] = gsl_ran_rayleigh(rng, dists[i].scale*sqrt(2./(4-PI)));
            weight *= gsl_ran_rayleigh_pdf(fillme[i], dists[i].scale*sqrt(2./(4-PI)));
        } else if(dists[i].type == CONST) {
            fillme[i] = dists[i].loc;
        }
    }
    
    return 1./weight;
}

/* Helper functions to generate a prior */

void BoldModel::generatePrior(aux::DiracMixturePdf& x0, int samples, 
            double sigma_scale, bool flat) const
{
    aux::vector width = sigma_scale*defscale(SIMUL_STATES);
    
    std::vector<Dist> newdist = defdist(SIMUL_STATES, width);
    generatePrior(x0, samples, newdist , flat);
}

void BoldModel::generatePrior(aux::DiracMixturePdf& x0, int samples, 
            const aux::vector mean, double sigma_scale, bool flat) const
{
    aux::vector width = sigma_scale*defscale(SIMUL_STATES);
    std::vector<Dist> newdist = defdist(SIMUL_STATES, width, mean);
    
    generatePrior(x0, samples, newdist, flat);
}

void BoldModel::generatePrior(aux::DiracMixturePdf& x0, int samples, 
            aux::vector width, bool flat) const
{
    std::vector<Dist> newdist = defdist(SIMUL_STATES, width);
    generatePrior(x0, samples, newdist, flat);
}
    
void BoldModel::generatePrior(aux::DiracMixturePdf& x0, int samples, 
            const aux::vector mean, aux::vector width, bool flat) const
{
    std::vector<Dist> newdist = defdist(SIMUL_STATES, width, mean);
    generatePrior(x0, samples, newdist, flat);
}

/* Main Generate Prior Function,
 * x0      - the mixture PDF that is being filled (added to)
 * samples - the number of samples to add to x0
 * dists   - the distributions for the different members of the particle
 * flat    - flatten the prior by dividing by the density at the point*/
void BoldModel::generatePrior(aux::DiracMixturePdf& x0, int samples,
            std::vector<struct BoldModel::Dist>& dists, bool flat) const
{
    boost::mpi::communicator world;
    const unsigned int rank = world.rank();
    
    gsl_rng* rng = gsl_rng_alloc(gsl_rng_ranlxd2);
    {
        unsigned int seed;
        FILE* file = fopen("/dev/urandom", "r");
        fread(&seed, 1, sizeof(unsigned int), file);
        fclose(file);
        gsl_rng_set(rng, seed^rank);
        std::cout << "Seeding with " << (unsigned int)(seed^rank) << "\n";
    }
    
    aux::vector comp(STATE_SIZE);
    double weight;
    
    for(unsigned int i = 0 ; i < dists.size(); i++) {
        if(dists[i].scale == 0.0)
            dists[i].type = CONST;
    }
    for(unsigned int i = 0 ; i < dists.size() ; i++) {
        std::cout <<  dists[i].type << " " <<  dists[i].scale << std::endl;
    }
    std::cout << std::endl;
    
    for(unsigned int i = 0 ; i < dists.size(); i++) {
        if(dists[i].type == CONST)
            std::cout << "CONST" << std::endl;
        else if(dists[i].type == GAMMA_MU)
            std::cout << "GAMMA_MU" << std::endl;
        else if(dists[i].type == GAMMA_MODE)
            std::cout << "GAMMA_MODE" << std::endl;
        else if(dists[i].type == NORMAL)
            std::cout << "NORMAL" << std::endl;
    }

    for(int i = 0 ; i < samples; i++) {
        weight = generateComponent(rng, comp, dists);
        x0.add(comp, flat ? weight : 1.0);
    }
    gsl_rng_free(rng);
}

/* Takes a particle and determines if it is invalid, if so then it
 * changes the weight (weightout) and then returns true because the
 * particles' weight changed
 * This would return true for instance if tau_0 was negative
 */
bool BoldModel::reweight(aux::vector& checkme, double& weightout) const
{
    size_t count = 0;
    //only S_T and drift vars are allowed to be negative
    for(unsigned int j = 0 ; j < STATE_SIZE-MEAS_SIZE; j++) {
        if(indexof(S_T, count) == j) {
            count++;
        } else if(checkme[j] < 0) {
            weightout = 0.0;
            checkme = defaultstate;
            return true;
        }
    }
    return false;
}

/* Default Location, used for all the prior distribution calculations */
aux::vector BoldModel::defloc(unsigned int simul)
{
    aux::vector ret(simul+simul*LVAR_SIZE+GVAR_SIZE);
    for(unsigned int ii = 0 ; ii < simul; ii++) {
        ret[indexof(TAU_0, ii)] = .98; //Hu = .98, Vakorin = 1.18
        ret[indexof(ALPHA, ii)] = .33; //Hu = .33, 
        ret[indexof(E_0, ii)] = .34; //Hu = .34
        ret[indexof(V_0, ii)] = 0.04; //Hu = .03
        ret[indexof(TAU_S, ii)] = 1.54; //Hu = 1.54, Vakorin = 2.72
        ret[indexof(TAU_F, ii)] = 2.46; //Hu = 2.46, Vakorin = .56
        ret[indexof(EPSILON, ii)] = .7; //Hu = .54

        ret[indexof(V_T,ii)] = 1;
        ret[indexof(Q_T,ii)] = 1;
        ret[indexof(S_T,ii)] = 0;
        ret[indexof(F_T,ii)]= 1;
    }
    
    for(unsigned int i = 0; i < simul; i++)
        ret[GVAR_SIZE+simul*LVAR_SIZE+i] = 0;
    return ret;
}

/* Default Scale, used for all the prior distribution calculations */
aux::vector BoldModel::defscale(unsigned int simul)
{
    aux::vector ret(simul+simul*LVAR_SIZE+GVAR_SIZE, 0);
    
    for(unsigned int ii = 0 ; ii < simul ; ii++) {
        //set the variances for all the variables to 3*sigma
        ret(indexof(TAU_0  ,ii)) = .25; //originally .25
        ret(indexof(ALPHA  ,ii)) = .045;
        ret(indexof(E_0    ,ii)) = .03;
        ret(indexof(V_0    ,ii)) = .03;
        ret(indexof(TAU_S  ,ii)) = .25; //originally .25
        ret(indexof(TAU_F  ,ii)) = .25; //originally .25
        ret(indexof(EPSILON,ii)) = .6; //originally .1

        ret(indexof(V_T,ii)) = 0;
        ret(indexof(Q_T,ii)) = 0;
        ret(indexof(S_T,ii)) = 0;
        ret(indexof(F_T,ii)) = 0;
    }
    for(unsigned int i = 0 ; i < simul; i++) {
        ret(GVAR_SIZE+simul*LVAR_SIZE+i) = 0;
    }

    return ret;
}

std::vector<BoldModel::Dist> BoldModel::defdist(unsigned int simul)
{
    return defdist(simul, defscale(simul), defloc(simul));
}
   
std::vector<BoldModel::Dist> BoldModel::defdist(unsigned int simul, 
            const aux::vector& scale)
{
    return defdist(simul, scale, defloc(simul));
}

/* Generates the default distribution structure */
std::vector<BoldModel::Dist> BoldModel::defdist(unsigned int simul, 
            const aux::vector& scale, const aux::vector& loc)
{
    std::vector<BoldModel::Dist> ret(loc.size());
    for(unsigned int ii = 0 ; ii < simul ; ii++) {
        ret[indexof(TAU_S  ,ii)].type = GAMMA_MU;
        ret[indexof(TAU_F  ,ii)].type = GAMMA_MU;
        ret[indexof(EPSILON,ii)].type = GAMMA_MU;
        ret[indexof(TAU_0  ,ii)].type = GAMMA_MU;
        ret[indexof(ALPHA  ,ii)].type = GAMMA_MU;
        ret[indexof(E_0    ,ii)].type = GAMMA_MU;
        ret[indexof(V_0    ,ii)].type = GAMMA_MU;

        ret[indexof(V_T,ii)].type = CONST;
        ret[indexof(Q_T,ii)].type = CONST;
        ret[indexof(S_T,ii)].type = NORMAL;
        ret[indexof(F_T,ii)].type = CONST;
    }
    for(unsigned int i = 0 ; i < simul; i++) {
        ret[GVAR_SIZE+simul*LVAR_SIZE+i].type = NORMAL;
    }

    for(unsigned int i = 0 ; i < ret.size() ; i++) {
        if(scale[i] == 0.0)
            ret[i].type = CONST;
        ret[i].scale = scale[i];
        ret[i].loc = loc[i];
    }

    return ret;
}

aux::vector BoldModel::getA(double E_0) 
{
    aux::vector out(2);
    out[0] = k1*E_0 + k2*E_0;
    out[1] = k2*E_0 + k3;
    return out;
}
