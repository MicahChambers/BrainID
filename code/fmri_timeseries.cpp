#include "itkOrientedImage.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkImageLinearIteratorWithIndex.h"
#include "itkImageSliceIteratorWithIndex.h"
#include "itkMetaDataObject.h"

#include "segment.h"

#include <itkMaskImageFilter.h>
#include <itkImageFileReader.h>
#include <itkStatisticsImageFilter.h>
#include "modNiftiImageIO.h"

#include <sstream>
#include <iostream>

#include <vcl_list.h>
#include <vul/vul_arg.h>

#define TIMEDIM 3
#define SLICEDIM 1
#define SECTIONDIM 0
typedef itk::OrientedImage<double, 4> ImageTimeSeries;

using namespace std;


void writeVolume(Image4DType::Pointer input, std::string filename, int index)
{
    Image3DType::Pointer out = extract(input, index);
    itk::ImageFileWriter< Image3DType >::Pointer writer = 
        itk::ImageFileWriter< Image3DType >::New();
    writer->SetImageIO(itk::modNiftiImageIO::New());
    writer->SetFileName(filename);  
    writer->SetInput(out);
    writer->Update();
    fprintf(stderr, "Saved as %s\n", filename.c_str());
}

//The labelmap should already have been masked through a maxprob image for
//graymatter
//TODO: Make the first element in each time series the section label
int main( int argc, char **argv ) 
{
    /* Input related */
    vul_arg<string> a_fmridir(0 ,"Directory with fmri timeseries");
    vul_arg<string> a_labels(0 ,"Labelmap image");
    vul_arg<string> a_mask("-m", "Greymatter mask", "");
    
    /* Processing */
    vul_arg<double> a_skip("-skip" ,"Amount of time to skip at the beginning", 0.);
    vul_arg<unsigned int> a_spline("-spline" ,"Number of regions to use with spline"
                " detrending, this is the number of knots minus 2, since a beginning"
                " knot and ending knot are also used", 3.);
//    vul_arg<bool> a_globalnorm("-gn", "Normalize globaly by all Greymatter"
//                " voxels (as opposed by section and voxel)", true);
    
    /* Output related */
    vul_arg<string> a_fullout("-c", "Cloned output of fmri timeseries", "");
    vul_arg<string> a_maskout("-om", "Masked bold 4D Image", "");
    vul_arg<string> a_filtered("-f", "Filtered/Normalized bold 4D Image", "");
    vul_arg<string> a_volume("-v", "Name to save volume at t=10 to", "");
    vul_arg<string> a_timeseries("-t", "Timeseries file, sections X time", "");
    vul_arg<string> a_regionprefix("-rp", "Region file prefix, with each full"
                " region from the input in a separet 4D file. x X y x Z X time", "");
    vul_arg< vcl_list<LabelType> > a_sections("-sections", "Sections to read from"
                " none = all");
    vul_arg< vcl_list<int> > a_pos("-pos", "3D coordinates of regions to output");

    /* TODO, compare the list of actual labels with list of input labels */
    
    vul_arg_parse(argc, argv);
    
    ostringstream oss;

    fprintf(stderr, "Reading Dicom Directory: %s...\n", a_fmridir().c_str());
    Image4DType::Pointer fmri_img = read_dicom(a_fmridir(), a_skip());
    fprintf(stderr, "Done reading\n");
    
    if(!a_fullout().empty()) {
        //create a 4D image writer to save the image of appropriate size.
        itk::ImageFileWriter< Image4DType >::Pointer writer4d = 
            itk::ImageFileWriter< Image4DType >::New();
        writer4d->SetImageIO(itk::modNiftiImageIO::New());
        writer4d->SetFileName(a_fullout());  
        writer4d->SetInput(fmri_img);
        writer4d->Update();
    }
    
    if(!a_volume().empty()) {
        fprintf(stderr, "Saving volume in middle for region checking purposes\n");
        oss.str("");
        oss << a_volume() << "-10.nii.gz";
        writeVolume(fmri_img, oss.str(), 10);
        
        oss.str("");
        oss << a_volume() << "-20.nii.gz";
        writeVolume(fmri_img, oss.str(), 20);
        
        oss.str("");
        oss << a_volume() << "-30.nii.gz";
        writeVolume(fmri_img, oss.str(), 30);
        fprintf(stderr, "Done.\n");
    }

    //label index
    itk::ImageFileReader<Label3DType>::Pointer labelmap_read = 
                itk::ImageFileReader<Label3DType>::New();
    labelmap_read->SetFileName( a_labels() );
    Label3DType::Pointer labelmap_img = labelmap_read->GetOutput();
    labelmap_img->Update();

    //mask file
    Label3DType::Pointer mask_img;
    if(!a_mask().empty()) {
        itk::ImageFileReader<Label3DType>::Pointer mask_read = 
                    itk::ImageFileReader<Label3DType>::New();
        mask_read->SetFileName( a_mask() );
        mask_img = mask_read->GetOutput();
        mask_img->Update();
    }

    fprintf(stderr, "Applying greymatter mask to label...\n");
    itk::MaskImageFilter< Label3DType, Label3DType, Label3DType >::Pointer maskf =
                itk::MaskImageFilter< Label3DType, Label3DType, Label3DType >::New();
    maskf->SetInput1(labelmap_img);
    maskf->SetInput2(mask_img);
    maskf->Update();
    labelmap_img = maskf->GetOutput();

    fprintf(stderr, "Applying mask to fmri image...\n");
    fmri_img = applymask<DataType, 4, LabelType, 3>(fmri_img, mask_img);

    if(!a_maskout().empty()) {
        itk::ImageFileWriter< Image4DType >::Pointer writer = 
                    itk::ImageFileWriter< Image4DType >::New();
        writer->SetInput(fmri_img);
        writer->SetImageIO(itk::modNiftiImageIO::New());
        writer->SetFileName(a_maskout());
        writer->Update();
    }

    fprintf(stderr, "Normalizing by voxel...\n");
    fmri_img = normalizeByVoxel(fmri_img, mask_img, a_spline());
    fprintf(stderr, "Done\n");
       
    cout << "filtered " <<  a_filtered() << " " << a_filtered().empty() << endl;
    if(!a_filtered().empty()) {
        fprintf(stderr, "Writing Filtered Image\n");

        itk::ImageFileWriter< Image4DType >::Pointer writer = 
                    itk::ImageFileWriter< Image4DType >::New();
        writer->SetInput(fmri_img);
        writer->SetImageIO(itk::modNiftiImageIO::New());
        writer->SetFileName(a_filtered());
        writer->Update();
        fprintf(stderr, "Done.\n");
    }
        
    Image4DType::Pointer timeseries;
    fprintf(stderr, "Generating Timeseries...\n");
    if(!a_pos().empty()) {
        timeseries = summ(fmri_img, a_pos());
    } else {
        timeseries = summ(fmri_img, labelmap_img, a_sections());
    }
    fprintf(stderr, "Done\n");

    cout << "time " <<  a_timeseries() << " " << a_timeseries().empty() << endl;
    if(!a_timeseries().empty()) {
        fprintf(stderr, "Writing Time Image\n");

        itk::ImageFileWriter< Image4DType >::Pointer writer = 
                    itk::ImageFileWriter< Image4DType >::New();
        unsigned int offset = 0;
        itk::ExposeMetaData(timeseries->GetMetaDataDictionary(), "offset", offset);
        fprintf(stderr, "Offset: %u", offset);
        writer->SetInput(timeseries);
        writer->SetImageIO(itk::modNiftiImageIO::New());
        writer->SetFileName(a_timeseries());
        writer->Update();
        fprintf(stderr, "Done.\n");
    }
        
    if(!a_regionprefix().empty()) {
        std::list<LabelType> rlabels = getlabels(labelmap_img);
        if(!a_sections().empty()) {
            removeMissing(a_sections(), rlabels);
        }
        std::list<LabelType>::iterator it = rlabels.begin();
        while(it != rlabels.end()) {
            if(*it != 0) {
                itk::ImageFileWriter< Image4DType >::Pointer writer = 
                    itk::ImageFileWriter< Image4DType >::New();
                oss.str("");
                oss << a_regionprefix() << "-" << *it << ".nii.gz";
                
                fprintf(stderr, "Splitting by region...\n");
                writer->SetInput(splitByRegion(fmri_img, labelmap_img, *it));
                fprintf(stderr, "Writing image: %s\n", oss.str().c_str());
                writer->SetFileName(oss.str());
                writer->Update();
                fprintf(stderr, "Done\n");
            }
            it++;
        }
    }

    return 0;
}

