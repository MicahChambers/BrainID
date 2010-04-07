#ifndef CALLBACKS_H
#define CALLBACKS_H

#include <itkOrientedImage.h>
#include <itkImageFileWriter.h>
#include <sstream>
#include "BoldPF.h"

//base
struct cb_data
{
    unsigned int pos[3];
    itk::OrientedImage<float, 4>::Pointer image;
};

//callbacks to save measurements
struct cb_meas_data
{
    unsigned int pos[3];
    itk::OrientedImage<float, 4>::Pointer image;
};

int cb_meas_call(BoldPF* bold, void* data)
{
    boost::mpi::communicator world;
    const unsigned int rank = world.rank();
    
    cb_meas_data* cdata = (struct cb_meas_data*)data;

    itk::OrientedImage<float, 4>::IndexType index;
    for(unsigned int i = 0 ; i < 3 ; i++)
        index[i] = cdata->pos[i];
    cdata->pos[3] = bold->getDiscTimeL();
    
    aux::vector mu = bold->getDistribution().getDistributedExpectation();
    aux::vector meas =  bold->getModel().measure(mu);

    if(rank == 0) {
         cdata->image->SetPixel(index, meas[0]);
         outputVector(std::cout, mu);
         std::cout << "\n" << meas[0] << "\n";
    }
    return 0;
}

void cb_meas_init(cb_meas_data* cdata, BoldPF::CallPoints* cp,
            itk::OrientedImage<float, 4>::SizeType size)
{
    itk::OrientedImage<float, 4>::Pointer img = itk::OrientedImage<float, 4>::New();
    img->SetRegions(size);
    img->Allocate();
    img->FillBuffer(0);
    cdata->image = img;
    
    cp->start = false;
    cp->postMeas = true;
    cp->postFilter = false;
    cp->end = false;
}

//callback to save ALL particles
//0 - param
//1 - particle
//2 - not used
//3 - time
struct cb_part_data
{
    unsigned int pos[3];
    itk::OrientedImage<float, 4>::Pointer image;
    itk::OrientedImage<float, 4>::SizeType size;
    itk::OrientedImage<float, 4>::IndexType prev;
};

void cb_part_init(cb_part_data* cdata, BoldPF::CallPoints* cp,
            int parameters, int particles, int time)
{
    cdata->size[0] = parameters+1;
    cdata->size[1] = particles;
    cdata->size[2] = 1;
    cdata->size[3] = time;

    for(unsigned int i = 0 ; i < 4 ; i++)
        cdata->prev[i] = 0;
    
    cdata->image = itk::OrientedImage<float, 4>::New();
    cdata->image->SetRegions(cdata->size);
    cdata->image->Allocate();
       
    //set callback points
    cp->start = false;
    cp->postMeas = true;
    cp->postFilter = false;
    cp->end = false;
}

int cb_part_call(BoldPF* bold, void* data)
{
    boost::mpi::communicator world;
    const unsigned int rank = world.rank();
    const unsigned int size = world.size();

    cb_part_data* cdata = (struct cb_part_data*)data;
   
    //check to see if this is a different xyz position, if so
    //write out the old image
    itk::OrientedImage<float, 4>::IndexType index;
    for(unsigned int i=0; i < 3; i++)
        index[i] = cdata->pos[i];
    index[3] = 0;
    if(index != cdata->prev) {
        std::ostringstream oss("");
        for(int i = 0 ; i < 3 ; i++)
            oss << cdata->prev[i] << "_";
        oss << ".nii";
        itk::ImageFileWriter<itk::OrientedImage<float, 4> >::Pointer writer = 
                    itk::ImageFileWriter<itk::OrientedImage<float, 4> >::New();
        writer->SetFileName(oss.str());
        writer->SetInput(cdata->image);
        writer->Update();
        cdata->prev = index;
    }
    
    index[0] = 0; //param
    index[1] = 0; //particle
    index[2] = 0;
    cdata->pos[3] = bold->getDiscTimeL();
  
    std::vector< std::vector< DiracPdf > > xsFull;
    std::vector< aux::vector > wsFull;

    boost::mpi::gather(world, bold->getDistribution().getAll(), xsFull, 0); 
    boost::mpi::gather(world, bold->getDistribution().getWeights(), wsFull, 0); 

    if(rank == 0) {
        for(size_t rank_ii = 0 ; rank_ii < size ; rank_ii++) { 
            for(size_t ee = 0 ; ee < xsFull[rank_ii].size() ; ee++) {
                index[0] = 0;
                for(size_t mm = 0 ; mm < cdata->size[0] ; mm++) {
                    //index1 - particle, index0 param
                    cdata->image->SetPixel(index, xsFull[rank_ii][ee].
                                getExpectation()[mm]); 
                    index[0]++;
                }
                //write weight after the parameters
                cdata->image->SetPixel(index, wsFull[rank_ii][ee]);
                index[1]++;  //doesn't get reset across particles...
            }
        }
    }
    return 0;
}

#endif //CALLBACKS_H
