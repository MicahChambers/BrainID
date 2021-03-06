#ifndef BOLDPF_H
#define BOLDPF_H
#include <indii/ml/filter/ParticleFilter.hpp>
#include <indii/ml/filter/StratifiedParticleResampler.hpp>
#include <indii/ml/aux/DiracMixturePdf.hpp>
#include <indii/ml/aux/vector.hpp>
#include <indii/ml/aux/matrix.hpp>

#include <indii/ml/aux/Almost2Norm.hpp>
#include <indii/ml/aux/AlmostGaussianKernel.hpp>

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

#include "tools.h"
#include "segment.h"
#include "modNiftiImageIO.h"
#include "RegularizedParticleResamplerMod.hpp"

#include <vector>
#include <iostream>
#include <string>

#include <indii/ml/filter/ParticleResampler.hpp>
#include <indii/ml/aux/Almost2Norm.hpp>
#include <indii/ml/aux/AlmostGaussianKernel.hpp>
#include "boost/numeric/bindings/traits/ublas_matrix.hpp"
#include "boost/numeric/bindings/traits/ublas_vector.hpp"
#include "boost/numeric/bindings/traits/ublas_symmetric.hpp"
#include "boost/numeric/bindings/lapack/lapack.hpp"
#include "boost/numeric/bindings/lapack/lapack.hpp"
#include <boost/math/special_functions/fpclassify.hpp>

namespace aux = indii::ml::aux;

/* BoldPF Handles the creation of a particle filter using the BOLD 
 * model. It can output data from various points in the algorithm
 * by setting callpoints in the CallPoints struct. A copy of this
 * struct and the function to call at all those points is taken
 * for setCallBack. The callback function should take a BoldPF and
 * a void*, the void* will have whatever pointer is passed to run
 * passed to it, thus data can be pulled out by passing it in to 
 * run then having the callback function change it.
*/
class BoldPF 
{
public:
    /* Types */
    typedef indii::ml::filter::ParticleFilter<double> Filter;
    enum Status {ERROR=-1, UNSTARTED=0, RUNNING=1, PAUSED=2, DONE=3};
    enum Method {DIRECT, DELTA, DC};
    
    /* Callback types */
    struct CallPoints{
        bool start;
        bool postMeas;
        bool postFilter; //AKA when no measurement exists;
        bool end;
    };
    typedef int(*CallBackFunction)(BoldPF*, void*);

    /* Constructor
     * measurements - a standard vector of measurement aux::vectors
     * activations  - a std::vector of Activation structs, time/level pairs
     * weightvar    - variance of the weighting function's pdf
     * longstep     - amount of time between measurements
     * numparticles - number of particles to use
     * shortstep    - simulation timesteps
     * method       - how to deal with drift in signal, 
     *                 DIRECT doesn't worry about it
     *                 DELTA weights based on delta in Bold signal
     *                 DC uses a parameter to estimate constant gain
     * weightf      - weightf, PDF to use for weighting 
     * flatten      - initializes weight to 1/probability of point
     */
    BoldPF(const std::vector<aux::vector>& measurements, 
                const std::vector<Activation>& activations,  double weightvar,
                double longstep, std::ostream* output, 
                unsigned int numparticles = 1000, double shortstep = 1./64,
                unsigned int method = DIRECT, int weightf = 0, bool flatten = true);

    /* Destructor */
    ~BoldPF();
    
    /* Primary Functions */
    int run(void* pass);
    int pause() { return status = PAUSED; };
    int restart(bool resetstate = true); 

    /* Accessors */
    double getContTime() const {return disctime_s*dt_s;};
    unsigned int getDiscTimeL() const {return disctime_l;};
    unsigned int getDiscTimeS() const {return disctime_s;};
    int getNumParticles() const;
    double getShortStep() const;
    double getLongStep() const;
    aux::DiracMixturePdf& getDistribution();
    int getStatus();

    bool isDC() { return method == DC; };
    bool isDelta() { return method == DELTA; };

    BoldModel& getModel() { return *model; };

    void setCallBack(const CallPoints& cpt, CallBackFunction cback)
    {
        callback = cback;
        call_points = cpt;
    };

private:
    /* Variables */
    //Identity
    Filter* filter;
    BoldModel* model;
    
    //timestep data
    double dt_l;
    double dt_s;

    //Indices
    unsigned int disctime_l;
    unsigned int disctime_s;

    //-1, error
    // 0, not started
    // 1, started
    // 2, started, but paused
    // 3, done
    int status;
    int method;

    //log output
    std::ostream* debug;

    //resamplers
    indii::ml::filter::StratifiedParticleResampler resampler;
    RegularizedParticleResamplerMod<aux::Almost2Norm, aux::AlmostGaussianKernel>*
                resampler_reg;

    //input and measurement vectors
    std::vector<aux::vector> measure;
    std::vector<Activation> stim;

    //constants
    const unsigned int ESS_THRESH;
    
    //Callback data
    CallPoints call_points;
    static int nop(BoldPF*, void*) { return 0; };
    int (*callback)(BoldPF*, void*);

    /* Saves in the state variable the previous measurement, for delta */
    void latchBold();
};

//to go into cpp file eventually
#include <fstream>
#include <cmath>

#include "BoldPF.h"

int BoldPF::getNumParticles() const
{
    return filter->getFilteredState().getDistributedSize();
};

double BoldPF::getShortStep() const
{
    return dt_s;
};

double BoldPF::getLongStep() const
{
    return dt_l;
};

aux::DiracMixturePdf& BoldPF::getDistribution()
{
    return filter->getFilteredState();
};

int BoldPF::getStatus()
{
    return status;
};

/* Calculates the Covariance matrix based on a DiracMixturePdf,
 * for some reason the version that is part of DiracMixturePdf has
 * a bug, hence this function.
 */
aux::matrix calcCov(indii::ml::aux::DiracMixturePdf& p)
{
    boost::mpi::communicator world;
    aux::vector mu = p.getDistributedExpectation();
    aux::matrix sum(mu.size(), mu.size(), 0);
    aux::matrix old;

    for(unsigned int i = 0 ; i < p.getSize() ;i++) {
        if(p.getWeight(i) > 0)
            sum += p.getWeight(i)*outer_prod(p.get(i)-mu, p.get(i)-mu);
        if(boost::math::isnan<double>(sum(8,8))) {
            std::cerr << "Sum" << std::endl;
            outputMatrix(std::cerr, sum);
            std::cerr << std::endl << "old" << std::endl;
            outputMatrix(std::cerr, old);
            std::cerr << std::endl << "mu" << std::endl;
            outputVector(std::cerr, mu);
            std::cerr << std::endl << "get(i)" << i << " weight " << p.getWeight(i) << std::endl;
            std::cerr << "weight > 0 ? " << (p.getWeight(i) > 0) << std::endl;
            outputVector(std::cerr, p.get(i));
            std::cerr << std::endl;
        }
    }
    sum = boost::mpi::all_reduce(world, sum, std::plus<matrix>());
    return sum/p.getDistributedTotalWeight();
}

/* Calculates the Standard Deviation matrix based on a DiracMixturePdf,
 * for some reason the version that is part of DiracMixturePdf has
 * a bug, hence this function.
 */
aux::matrix calcStdDev(indii::ml::aux::DiracMixturePdf& p)
{
    static int count = 0;
    namespace aux = indii::ml::aux;
    namespace ublas = boost::numeric::ublas;
    namespace lapack = boost::numeric::bindings::lapack;
    boost::mpi::communicator world;
    aux::matrix cov = calcCov(p);
    
    aux::vector diag_v(cov.size1());
    int err =  lapack::syev('V', 'U', cov, diag_v);
    if(err != 0) {
        throw(-1);
    }
    aux::matrix tmp;
    
    for(unsigned int i = 0 ; i < diag_v.size() ; i++) {
        if(diag_v[i] < 0) {
            count++;
            if(abs(diag_v[i]) < 1e-10)
                diag_v[i] = 0;
            else {
                fprintf(stderr, "Eig:");
                for(uint32_t j = 0 ; j < diag_v.size() ; j++)
                    fprintf(stderr, "%f ", diag_v[j]);
                fprintf(stderr, "\nWTF?");
                throw(-5);
            }
        }
        diag_v[i] = sqrt(diag_v[i]);
    }
    ublas::diagonal_matrix<double, ublas::column_major, ublas::unbounded_array<double> >
                diag_m(diag_v.size(), diag_v.data());
    
    tmp = prod(cov, diag_m);
    cov = prod(tmp, trans(cov));

//    fprintf(stdout, "(%i)\n", count);
    return cov;
}

/* 
 * Resets the state of the BoldPF, time status etc. Should be
 * called if something breaks and you want to try again
 */
int BoldPF::restart(bool resetstate)
{
    filter->setTime(0);
    disctime_l = 0;
    disctime_s = 0;
    status = UNSTARTED; 
    if(resetstate) {
        for(uint32_t i = 0 ; i < getDistribution().getSize() ; i++) {
            for(uint32_t j = 0 ; j < getModel().getMeasurementSize() ; j++) {
                getDistribution().get(i)[getModel().indexof(getModel().V_T, j)] = 1;
                getDistribution().get(i)[getModel().indexof(getModel().Q_T, j)] = 1;
                getDistribution().get(i)[getModel().indexof(getModel().F_T, j)] = 1;
                getDistribution().get(i)[getModel().indexof(getModel().S_T, j)] = 0;
            }
        }
    }
    return status;
}

/* Run - runs the particle filter
 * pass - variable to pass to callback function
**/
int BoldPF::run(void* pass = NULL)
{
    boost::mpi::communicator world;
    unsigned int rank = world.rank();
//    unsigned int size = world.size();
    using std::endl;

    if(status == ERROR || status == RUNNING || status == DONE) {
        return status;
    }

    double ess = filter->getFilteredState().getSize();
    *debug << "mu size: " << filter->getFilteredState().getDistributedExpectation().size()
                << std::endl << "dimensions: " 
                << filter->getFilteredState().getDimensions() << std::endl;
    
    /* Simulation Section */
    double conttime = disctime_s*dt_s;
    unsigned int stim_index = 0;

    *debug << "Starting at " << conttime
                << ", disctime_s: " << disctime_s
                << ", disctime_l: " << disctime_l 
                << ", dt_l: " << dt_l << " dt_s: " << dt_s 
                << ", measure size: " << measure.size()
                << ", stim size: " << stim.size() << "\n";
    
    if(call_points.start) 
        callback(this, pass);

    /* 
     * Run the particle filter either until we reach a predetermined end
     * time, or until we are done processing measurements.
     */
     bool resampled = false;
     status = RUNNING;
     while(status == RUNNING && disctime_s*dt_s < dt_l*measure.size()) {
        /* time */
        conttime = disctime_s*dt_s;
        
        /* Update Input if there is any*/
        while(stim_index < stim.size() && stim[stim_index].time <= conttime) {
            model->setinput(aux::vector(1, stim[stim_index].level));
            stim_index++;
        }
        

        /* Check to see if it is time to update */
        if(conttime >= disctime_l*dt_l) { 
            //acquire the latest measurement
            *debug << "Measuring at " <<  conttime << "\n";
            aux::vector meas(measure[disctime_l]);
            outputVector(*debug, meas);
            *debug << "\n";

            //step forward in time, with measurement
            filter->filter(conttime, meas);
            if(call_points.postMeas) 
                callback(this, pass);

            //check to see if resampling is necessary
            if(filter->getFilteredState().getDistributedTotalWeight() < .0001 ||
                        filter->getFilteredState().getDistributedTotalWeight() > 1000) {
                *debug << "Normalizing total weight reached " << 
                            filter->getFilteredState().getDistributedTotalWeight()
                            << "\n";
                filter->getFilteredState().distributedNormalise();
            }
            ess = filter->getFilteredState().calculateDistributedEss();
            
            //check for errors, could be caused by total collapse of particles,
            //for instance if all the particles go to an unreasonable value like 
            //inf/nan/neg
            if(boost::math::isnan<double>(ess)) {
                *debug << "\n" << "Error! ESS was " << ess << "\n";
                status = ERROR;
                break;
            } 
        
            aux::vector tmpmu = filter->getFilteredState().
                        getDistributedExpectation();
            aux::symmetric_matrix statecov = calcCov(filter->getFilteredState());

            matrix stdDev;
            try{
                stdDev = calcStdDev(filter->getFilteredState());
            } catch(int err){
                filter->getFilteredState().dirty();
                statecov = calcCov(filter->getFilteredState());
                
                if(rank == 0){
                    *debug << "Ess: " << ess << endl;
                    *debug << "Mu: ";
                    outputVector(*debug, tmpmu);
                    *debug << endl << "Cov: ";
                    outputMatrix(*debug, statecov);
                    *debug << endl << err << endl;
                }
                status = ERROR;
                break;
            }

            //time to resample, force at least one resampling after 20 seconds
            if(ess < ESS_THRESH || (conttime > 20. && !resampled)) {
                resampled = true;
                *debug << " ESS: " << ess;
                *debug << "\nStratified Resampling\n";
                filter->resample(&resampler);
                *debug << "\nRegularized Resampling\n";
                filter->setFilteredState( resampler_reg->resample(
                                filter->getFilteredState(), stdDev) );
            } else {
                *debug << " ESS: " << ess << ", No Resampling Necessary!\n";
            }

            /* Perform updates to delta variables */
            if(method == DELTA) 
                latchBold();
            
            disctime_l++;
        } else { //no update available, just step update states
            filter->filter(conttime);
            if(call_points.postFilter) 
                callback(this, pass);
        }
   
        /* Update Time, disctime */
        disctime_s++;
    }

    /* Check to see if algorith finished, otherwise it was paused */
    if(disctime_s*dt_s >= dt_l*measure.size()) {
        status = DONE;
        if(call_points.end) 
            callback(this, pass);
    }

    *debug << "Stop time: "<< conttime << endl;
    
    return status;
};

bool isclose(double a, double b) 
{
    return fabs(a*1000 - b*1000) < 1;
}

/* Used to linearize the BOLD process and regress the step sizes rather
 * than the actual Bold measurements. part of the delta option.
 */
void BoldPF::latchBold()
{
    for(unsigned int ii = 0 ; ii < filter->getFilteredState().getSize(); ii++) {
        aux::vector& p = filter->getFilteredState().get(ii);
        aux::vector m = model->measure(p);
        for(unsigned int jj = 0; jj < model->getMeasurementSize(); jj++)
            p[model->getStateSize() - model->getMeasurementSize() + jj] = m[jj];
    }

    //make dirty
    filter->getFilteredState().distributedNormalise();
};

/* Used to calculate the average measurement from a vector. Note
 * that the input is a vector of aux::vectors, which allows 
 * for a mean vector to be calculated. For one voxel, all the
 * aux::vectors will be size 1
 */
aux::vector bold_mean(const std::vector<aux::vector>& in)
{
    aux::vector sum(in.back().size(), 0);
    for(size_t i = 0 ; i < in.size() ; i++) 
        sum = sum + in[i];
    return sum/in.size();
}

/* Used to calculate the average measurement from a vector (in) */
aux::vector bold_stddev(const std::vector<aux::vector>& in, const aux::vector& mean)
{
    aux::vector sum(in.back().size(), 0);
    for(size_t i = 0 ; i < in.size() ; i++)
        sum = sum + scalar_pow(mean - in[i], 2);
    return element_sqrt(sum/in.size());
}

/* Constructor
 * measurements - a standard vector of measurement aux::vectors
 * activations  - a std::vector of Activation structs, time/level pairs
 * weightvar    - variance of the weighting function's pdf
 * longstep     - amount of time between measurements
 * numparticles - number of particles to use
 * shortstep    - simulation timesteps
 */
BoldPF::BoldPF(const std::vector<aux::vector>& measurements, 
            const std::vector<Activation>& activations,  double weightvar,
            double longstep, std::ostream* output, unsigned int numparticles,
            double shortstep, unsigned int method_p, int weightf, bool flatten) : 
            dt_l(longstep), dt_s(shortstep), 
            disctime_l(0), disctime_s(0), status(UNSTARTED), method(method_p),
            resampler(numparticles), measure(measurements), stim(activations), 
            //ESS_THRESH(50 > .15*numparticles ? 50 : .15*numparticles)
            ESS_THRESH(50)
{
    boost::mpi::communicator world;
    const unsigned int rank = world.rank();
    const unsigned int size = world.size();
    
    using std::endl;
    /* initialize debug/output */
    debug = output;

    /* Initalize the model and filter*/
    aux::vector weightv(measurements.front().size(), weightvar);
    model = new BoldModel(weightv, weightf, measurements.front().size());
    aux::DiracMixturePdf tmp(model->getStateSize());
    filter = new indii::ml::filter::ParticleFilter<double>(model, tmp);

    /* 
     * Particles Setup 
     */
    *debug << "Generating prior" << std::endl;
    unsigned int localparticles = 16000/size;
    
    //give excess particles to last rank
    if(rank == (size-1))
        localparticles += numparticles - 16000*size;

    aux::vector boldmu = bold_mean(measurements);
    aux::vector boldstd = bold_stddev(measurements, boldmu);

    /* Generate Prior */
    aux::vector width = model->defscale(measurements.front().size());
    aux::vector loc = model->defloc(measurements.front().size());
    
    for(unsigned int ii = model->getStateSize() - model->getMeasurementSize(); 
                ii < model->getMeasurementSize() ; ii++) {
        width[ii] = 0;
        loc[ii] = 0;
    }

    *debug << "Location: " << std::endl;
    outputVector(*debug, loc);
    *debug << std::endl << "Scale: " << std::endl;
    outputVector(*debug, width);
    *debug << std::endl;

    model->generatePrior(filter->getFilteredState(), localparticles, loc, width, flatten); 
    filter->getFilteredState().distributedNormalise();
    
    //Redistribute - doesn't cost anything if distrib. was already fine 
    *debug << "Redistributing" << endl;
    filter->getFilteredState().redistributeBySize(); 

    *debug << "Size: " <<  filter->getFilteredState().getDistributedSize() << endl;

    /* 
     * Create 
     * Regularized Resampler
     */
    aux::Almost2Norm norm;
    aux::AlmostGaussianKernel kernel(model->getStateSize(), 1);
    resampler_reg = new RegularizedParticleResamplerMod
                <aux::Almost2Norm, aux::AlmostGaussianKernel>
                (norm, kernel, model);

    call_points.start = false;
    call_points.postMeas = false;
    call_points.postFilter = false;
    call_points.end = false;
    callback = nop;
            
    if(method == DELTA) 
        latchBold();
};

/* Destructor */
BoldPF::~BoldPF()
{
    delete filter;
    delete model;
    delete resampler_reg;
};

#endif //BOLDPF_H
