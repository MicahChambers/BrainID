//image readers
#include "itkImageFileReader.h"

//standard libraries
#include <ctime>
#include <cstdio>
#include <iomanip>
#include <vector>
#include <list>
#include <algorithm>

#include <itkImageFileWriter.h>
#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkImageSeriesReader.h>
#include <itkMetaDataDictionary.h>
#include <itkNaryElevateImageFilter.h>
#include <itkMaskImageFilter.h>

#include <boost/mpi.hpp>
#include "segment.h"
#include "tools.h"
#include <itkLabelStatisticsImageFilterMod.h>
#include <itkImageLinearIteratorWithIndex.h>
#include <itkImageLinearConstIteratorWithIndex.h>

#include <itkSubtractConstantFromImageFilter.h>
#include <itkDivideByConstantImageFilter.h>
#include <itkSquareImageFilter.h>
#include <itkSqrtImageFilter.h>
#include <itkNaryAddImageFilter.h>
#include <itkAddImageFilter.h>
#include <itkSubtractImageFilter.h>
#include <itkDivideImageFilter.h>
#include <itkDivideByConstantImageFilter.h>
#include <itkMultiplyByConstantImageFilter.h>
#include <itkCastImageFilter.h>
#include <itkBinaryThresholdImageFilter.h>

#include <gsl/gsl_spline.h>

typedef itk::AddImageFilter< Image3DType > AddF3;
typedef itk::AddImageFilter< Image4DType > AddF4;

typedef itk::SubtractImageFilter< Image3DType > SubF3;
typedef itk::SubtractImageFilter< Image4DType > SubF4;

typedef itk::MultiplyByConstantImageFilter< Image3DType, double, Image3DType > ScaleF;
typedef itk::BinaryThresholdImageFilter<Label3DType, Label3DType> ThreshF;
typedef itk::LabelStatisticsImageFilterMod<Image3DType, Label3DType> StatF3D;
typedef itk::MaskImageFilter<Image3DType, Label3DType, Image3DType> MaskF;
//typedef itk::LabelStatisticsImageFilter<Image3DType, Label3DType> StatF3D;

typedef itk::LabelStatisticsImageFilterMod<Image4DType, Label4DType> StatF4D;
//typedef itk::LabelStatisticsImageFilter<Image4DType, Label4DType> StatF4D;
typedef itk::NaryAddImageFilter< Image3DType, Image3DType > AddNF;
typedef itk::SquareImageFilter< Image3DType, Image3DType > SqrF3;
typedef itk::SqrtImageFilter< Image3DType, Image3DType > SqrtF3;
typedef itk::SubtractConstantFromImageFilter< Image4DType, double, 
            Image4DType > SubCF;
typedef itk::DivideImageFilter< Image4DType, Image4DType, Image4DType > DivF;
typedef itk::DivideByConstantImageFilter< Image4DType, double, Image4DType > DivCF;

/* Remove any elements that arent' in the reference list 
 * ref will be sorted, but otherwise will be unchanged*/

template<class T, unsigned int SIZE>
void outputinfo(Image4DType::Pointer in) {
    fprintf(stderr, "Dimensions:\n");
    for(size_t ii = 0 ; ii < SIZE ; ii++)
        fprintf(stderr, "%zu ", in->GetRequestedRegion().GetSize()[ii]);
    
    fprintf(stderr, "\nIndex:\n");
    for(size_t ii = 0 ; ii < SIZE ; ii++)
        fprintf(stderr, "%zu ", in->GetRequestedRegion().GetIndex()[ii]);

    fprintf(stderr, "\nSpacing:\n");
    for(size_t ii = 0 ; ii < SIZE ; ii++)
        fprintf(stderr, "%f ", in->GetSpacing()[ii]);
    
    fprintf(stderr, "\nOrigin:\n");
    for(size_t ii = 0 ; ii < SIZE ; ii++)
        fprintf(stderr, "%f ", in->GetOrigin()[ii]);

    fprintf(stderr, "\nDirection:\n");
    for(size_t ii = 0 ; ii<SIZE ; ii++) {
        for( size_t jj=0; jj<SIZE ; jj++) {
            fprintf(stderr, "%f ", in->GetDirection()(ii,jj));
        }
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
}

Image3DType::Pointer extract(Image4DType::Pointer input, size_t index)
{
    Image4DType::IndexType index4D = {{0, 0, 0, 0}};
    
    itk::ImageLinearIteratorWithIndex<Image4DType> fmri_it
                ( input, input->GetRequestedRegion() );
    index4D[3] = index;
    fmri_it.SetDirection(0);
    fmri_it.SetIndex(index4D);

    Image3DType::Pointer out = Image3DType::New();
    
    Image3DType::RegionType out_region;
    Image3DType::IndexType out_index;
    Image3DType::SizeType out_size;
    
    Image3DType::SpacingType space;
    Image3DType::DirectionType direc;
    Image3DType::PointType origin; 

    for(int i = 0 ; i < 3 ; i++) 
        out_size[i] = input->GetRequestedRegion().GetSize()[i];
    
    for(int i = 0 ; i < 3 ; i++) 
        out_index[i] = input->GetRequestedRegion().GetIndex()[i];
    
    for(int i = 0 ; i < 3 ; i++) 
        space[i] = input->GetSpacing()[i];
    
    for(int i = 0 ; i < 3 ; i++) 
        origin[i] = input->GetOrigin()[i];
    
    for(int ii = 0 ; ii<3 ; ii++) {
        for(int jj = 0 ; jj<3 ; jj++) {
            direc(ii, jj) = input->GetDirection()(ii, jj);
        }
    }
    
    out_region.SetSize(out_size);
    out_region.SetIndex(out_index);
    out->SetRegions( out_region );

    out->SetDirection(direc);
    out->SetSpacing(space);
    out->SetOrigin(origin);

    out->Allocate();
    
    itk::ImageLinearIteratorWithIndex<Image3DType> out_it 
                ( out, out->GetRequestedRegion() );
    out_it.SetDirection(0);
    out_it.GoToBegin();

    while(!out_it.IsAtEnd()) {
        while(!out_it.IsAtEndOfLine()) {
            out_it.Value() = fmri_it.Value();
            ++fmri_it;
            ++out_it;
        }
        out_it.NextLine();
        fmri_it.NextLine();
    }
    
    return out;
}

template<class T, unsigned int SIZE1, unsigned int SIZE2>
void copyInformation(typename itk::OrientedImage<T, SIZE1>::Pointer in1, 
            typename itk::OrientedImage<T, SIZE2>::Pointer in2)
{
    typename itk::OrientedImage<T, SIZE2>::SpacingType space;
    typename itk::OrientedImage<T, SIZE2>::DirectionType direc; 
    typename itk::OrientedImage<T, SIZE2>::PointType origin; 
    for(size_t ii = 0 ; ii < SIZE2; ii++) {
        if(ii >= SIZE1)
            space[ii] = 1;
        else
            space[ii] = in1->GetSpacing()[ii];
    }

    for(size_t ii = 0 ; ii<SIZE2 ; ii++) {
        for( size_t jj=0; jj<SIZE2 ; jj++) {
            if(ii >= SIZE1 || jj >= SIZE1) {
                if(ii == jj) 
                    direc(ii, jj) = 1;
                else 
                    direc(ii, jj) = 0;
            } else {
                direc(ii, jj) = in1->GetDirection()(ii, jj);
            }
        }
    }
    
    for(size_t ii = 0 ; ii < SIZE2; ii++) {
        if(ii >= SIZE1)
            origin[ii] = 0;
        else
            origin[ii] = in1->GetOrigin()[ii];
    }
}

Image4DType::Pointer initTimeSeries(Image4DType::Pointer fmri_img, int sections)
{       
    //create a 4D output image of appropriate size.
    Image4DType::Pointer outputImage = Image4DType::New();

    Image4DType::RegionType out_region;
    Image4DType::IndexType out_index = {{0,0,0,0}};
    Image4DType::SizeType out_size = {{1, 1, 1, 1}};
    
    out_size[SECTIONDIM] = sections;
    out_size[TIMEDIM] = fmri_img->GetRequestedRegion().GetSize()[3];
    fprintf(stderr, " numsection : %lu\n", out_size[SECTIONDIM]);
    fprintf(stderr, " tlen       : %lu\n", out_size[TIMEDIM]);
    
    out_region.SetSize(out_size);
    out_region.SetIndex(out_index);

    outputImage->SetRegions( out_region );
    outputImage->Allocate();
    outputImage->FillBuffer(0);
    outputImage->SetMetaDataDictionary(fmri_img->GetMetaDataDictionary());
    itk::EncapsulateMetaData(outputImage->GetMetaDataDictionary(), "Dim3", 
                std::string("time"));
    itk::EncapsulateMetaData(outputImage->GetMetaDataDictionary(), "Dim0", 
                std::string("section"));
    outputImage->CopyInformation(fmri_img);

    return outputImage;
}

std::list<LabelType> getlabels(Label3DType::Pointer labelmap)
{
    /* Just used to acquire the list of labels */
    itk::LabelStatisticsImageFilterMod<Label3DType, Label3DType>::Pointer
                stats = 
                itk::LabelStatisticsImageFilterMod<Label3DType, Label3DType>::New();
    
    stats->SetLabelInput(labelmap);
    stats->SetInput(labelmap);
    stats->Update();
    return stats->GetLabels();
}

template <typename T>
typename itk::OrientedImage<T,4>::Pointer stretch(
            typename itk::OrientedImage<T,3>::Pointer in, int length)
{
    typedef typename itk::OrientedImage<T,3> I3D;
    typedef typename itk::OrientedImage<T,4> I4D;
    typedef typename itk::NaryElevateImageFilter< I3D, I4D > NaryF;
    
    typename NaryF::Pointer elevateFilter = NaryF::New();
//    typename itk::NaryElevateImageFilter< typename itk::OrientedImage<T,3>, 
//                typename itk::OrientedImage<T,4> >::Pointer elevateFilter =
//                itk::NaryElevateImageFilter < typename itk::OrientedImage<T,3>, 
//                typename itk::OrientedImage<T,4> >::New(); 
    itk::MetaDataDictionary dict = in->GetMetaDataDictionary();
    itk::MetaDataDictionary dict2;
    in->SetMetaDataDictionary(dict2);
    for(int i=0; i < length ; i++)
        elevateFilter->PushBackInput(in);
    elevateFilter->Update();
    copyInformation<T, 3, 4>(in, elevateFilter->GetOutput());
    elevateFilter->GetOutput()->SetMetaDataDictionary(dict);
    return elevateFilter->GetOutput();
}

/* Returns:
 * -1 if before is the lowest
 *  0 if current is the lowest or current ties for lowest
 *  1 if after is the lowest 
 */
int min(int before, int current, int after) {
    if(before < current) {
        if(before < after) {
            return -1;
        } else { //before >= after 
            return 1;
        }
    } else { //before >= current
        if( current <= after) {
            return 0;
        } else { //current > after 
            return 1;
        }
    }
}


Image3DType::Pointer get_average(const Image4DType::Pointer fmri_img)
{
    /* Used to zero out the addfilter */
    Image3DType::Pointer zero = extract(fmri_img, 0);
    zero->FillBuffer(0);
    
    /* Initialize the Addition */
    AddF3::Pointer add = AddF3::New();
    add->GraftOutput(zero);
    add->SetInput2(add->GetOutput());
    
    /* Calculate Sum of Images */
    for(size_t ii = 0 ; ii < fmri_img->GetRequestedRegion().GetSize()[3] ; ii++) {
        add->SetInput1(extract(fmri_img, ii));
        add->Update();
    }

    /* Calculate Average of Images */
    ScaleF::Pointer scale = ScaleF::New();
    scale->SetInput(add->GetOutput());
    scale->SetConstant(1./fmri_img->GetRequestedRegion().GetSize()[3]);
    scale->Update();
    return scale->GetOutput();
}

double get_average(const Image4DType::Pointer fmri_img, 
        const Label3DType::Pointer labelmap)
{
    Image3DType::Pointer perVoxAvg = get_average(fmri_img);

    /* Change mask to 0/1 */
    ThreshF::Pointer thresh = ThreshF::New();
    thresh->SetLowerThreshold(0);
    thresh->SetUpperThreshold(0);
    thresh->SetInsideValue(0);
    thresh->SetOutsideValue(1);
    thresh->SetInput(labelmap);
    thresh->Update();

    /******************************************************* 
     * Calculate the average for everything inside the mask
     */
    StatF3D::Pointer stats = StatF3D::New();
    stats->SetLabelInput(thresh->GetOutput());
    stats->SetInput(perVoxAvg);
    stats->Update();
    
    if(stats->GetNumberOfLabels() == 2)
        return stats->GetMean(1);
    else 
        return stats->GetMean(0);
}

/* ???? */
Image4DType::Pointer splitByRegion(const Image4DType::Pointer fmri_img,
            const Label3DType::Pointer labelmap, int label)
{
    std::ostringstream oss;
    
    /* For each region, threshold the labelmap to just the desired region */
    /* Change mask to 0/1 */
    ThreshF::Pointer thresh = ThreshF::New();
    thresh->SetLowerThreshold(label);
    thresh->SetUpperThreshold(label);
    thresh->SetInsideValue(1);
    thresh->SetOutsideValue(0);
    thresh->SetInput(labelmap);
    thresh->Update();

    return applymask<DataType, 4, LabelType, 3>(fmri_img, thresh->GetOutput());
}

//Reads a dicom directory then returns a pointer to the image
//does some of this memory need to be freed??
Image4DType::Pointer read_dicom(std::string directory, double skip)
{
    // create elevation filter
    itk::NaryElevateImageFilter<Image3DType, Image4DType>::Pointer elevateFilter
                = itk::NaryElevateImageFilter<Image3DType, Image4DType>::New();

    
    // create name generator and attach to reader
    itk::GDCMSeriesFileNames::Pointer nameGenerator = itk::GDCMSeriesFileNames::New();
    nameGenerator->SetUseSeriesDetails(true);
    nameGenerator->AddSeriesRestriction("0020|0100"); // acquisition number
    nameGenerator->SetDirectory(directory);

    // get series IDs
    const std::vector<std::string>& seriesUID = nameGenerator->GetSeriesUIDs();

    // create reader array
    itk::ImageSeriesReader<Image3DType>::Pointer *reader = 
                new itk::ImageSeriesReader<Image3DType>::Pointer[seriesUID.size()];

    // declare series iterators
    std::vector<std::string>::const_iterator seriesItr=seriesUID.begin();
    std::vector<std::string>::const_iterator seriesEnd=seriesUID.end();
    
    Image4DType::SpacingType space4;
    Image4DType::DirectionType direc; //c = labels->GetDirection();
    Image4DType::PointType origin; //c = labels->GetDirection();
        
    itk::GDCMImageIO::Pointer dicomIO;
    std::string value;

    double temporalres = 2;
    //reorder the input based on temporal number.
    while (seriesItr!=seriesEnd)
    {
        itk::ImageSeriesReader<Image3DType>::Pointer tmp_reader = 
                    itk::ImageSeriesReader<Image3DType>::New();

        dicomIO = itk::GDCMImageIO::New();
        tmp_reader->SetImageIO(dicomIO);

        std::vector<std::string> fileNames;

//        printf("Accessing %s\n", seriesItr->c_str());
        fileNames = nameGenerator->GetFileNames(seriesItr->c_str());
        tmp_reader->SetFileNames(fileNames);

        tmp_reader->ReleaseDataFlagOn();

        tmp_reader->Update();
//        tmp_reader->GetOutput()->SetMetaDataDictionary(
//                    dicomIO->GetMetaDataDictionary());
//    
//        itk::MetaDataDictionary& dict = 
//                    tmp_reader->GetOutput()->GetMetaDataDictionary();
        
        dicomIO->GetValueFromTag("0020|0100", value);
        printf("Temporal Number: %s\n", value.c_str());
        
        reader[atoi(value.c_str())-1] = tmp_reader;

        dicomIO->GetValueFromTag("0018|0080", value);
        temporalres = atof(value.c_str())/1000.;
//        itk::EncapsulateMetaData(dict, "TemporalResolution", temporalres);
    
        ++seriesItr;
    }

    for(int ii = 0 ; ii < 3 ; ii++)
        space4[ii] = reader[0]->GetOutput()->GetSpacing()[ii];
    space4[3] = temporalres;

    for(int ii = 0 ; ii<4 ; ii++) {
        for( int jj=0; jj<4 ; jj++) {
            if(ii == 3 || jj == 3) 
                direc(ii, jj) = 0;
            else
                direc(ii, jj) = reader[0]->GetOutput()->GetDirection()(ii, jj);
        }
    }
    direc(3, 3) = 1;
        
    // connect each series to elevation filter
    // skip the first two times
    unsigned int offset = 0;
    for(unsigned int ii=0; ii<seriesUID.size(); ii++) {
        if(ii*temporalres >= skip) {
            elevateFilter->PushBackInput(reader[ii]->GetOutput());
        } else {
            fprintf(stderr, "Skipping: %i, %f\n", ii, ii*temporalres);
            offset = (ii+1);
        }
    }
        

    //now elevateFilter can be used just like any other reader
    elevateFilter->Update();
    
    //create image readers
    Image4DType::Pointer fmri_img = elevateFilter->GetOutput();
//    fmri_img->CopyInformation(reader[0]->GetOutput());
    itk::EncapsulateMetaData(fmri_img->GetMetaDataDictionary(), "offset", offset);
    fmri_img->SetDirection(direc);
    fmri_img->SetSpacing(space4);
    fmri_img->Update();
        
    const std::vector<std::string>& keys = dicomIO->GetMetaDataDictionary().GetKeys();
    std::string label;
    for(size_t i = 0 ; i < keys.size() ;i++) {
        if(dicomIO->GetValueFromTag(keys[i], value)) {
            fprintf(stdout, "%s", keys[i].c_str());
            if(dicomIO->GetLabelFromTag(keys[i], label)){
                fprintf(stdout, " -> %s: %s\n", label.c_str(), value.c_str());
                itk::EncapsulateMetaData(fmri_img->GetMetaDataDictionary(), label, value);
            } else {
                fprintf(stdout, ": %s\n", value.c_str());
                itk::EncapsulateMetaData(fmri_img->GetMetaDataDictionary(), keys[i], value);
            }
        }
    }
   

    delete[] reader;
    return fmri_img;
};

Image4DType::Pointer pruneFMRI(const Image4DType::Pointer fmri_img,
            std::vector<Activation>& stim, double dt,
            unsigned int remove)
{
    /* Remove first few elements... */
    // .... from fmri_img
    Image4DType::Pointer new_img = prune<float>(fmri_img, 3, remove, 
                fmri_img->GetRequestedRegion().GetSize()[3]);

    // .... from stimulus, then shift times
    std::vector<Activation>::iterator it = stim.begin();
    std::vector<Activation>::iterator start = stim.begin();
    while(it != stim.end()) {
        if(it->time < dt*remove) {
            start = it;
        } else {
            break;
        }
        it++;
    }
    start->time = dt*remove;
    stim.erase(stim.begin(), start);
    
    for(unsigned int i = 0 ; i < stim.size() ;i++) {
        stim[i].time -= dt*remove;
    }

    return new_img;
}

/**************************************************************************
 * Blind Spline Generation
**************************************************************************/
int detrend_lmin(const Image4DType::Pointer fmri_img, Image4DType::IndexType index, 
            int knots, Image4DType::Pointer output)
{
    gsl_interp_accel *acc = gsl_interp_accel_alloc();
    gsl_spline *spline = gsl_spline_alloc(gsl_interp_cspline, knots);

    /* Go to index and start at time 0 at that voxel*/
    itk::ImageLinearConstIteratorWithIndex< Image4DType > 
                fmri_it(fmri_img, fmri_img->GetRequestedRegion());
    fmri_it.SetDirection(3);
    fmri_it.SetIndex(index);

    double xpos[knots];
    double medians[knots];

    int length = fmri_img->GetRequestedRegion().GetSize()[3];
    double rsize = (double)length / (knots-1);

    //generate a vector of point lists
    std::vector< std::list<double> > points(knots);
    for(fmri_it.GoToBeginOfLine(); !fmri_it.IsAtEndOfLine(); ++fmri_it) {
        unsigned int i = fmri_it.GetIndex()[3];
        if(i < rsize/2) {
//            printf("%u -> 0\n", i);
            points.front().push_back(fmri_it.Get());
        } else if(i > length - rsize/2) {
//            printf("%u -> last (%zu)\n", i, points.size());
            points.back().push_back(fmri_it.Get());
        } else {
//            printf("%u -> %i\n", i, 1+(int)(i/rsize-1./2));
            points[1+(int)(i/rsize-1./2)].push_back(fmri_it.Get());
        }
    }

    //find the median of each point list
    for(unsigned int i = 0 ; i < points.size() ; i++) {
        points[i].sort();
        if(points[i].size()%2 == 0) {
            std::list<double>::iterator it = points[i].begin();
            it++;
            medians[i] = *it;
            it++;
            medians[i] += *it;
            medians[i] /= 2.;
        } else {
            std::list<double>::iterator it = points[i].begin();
            it++;
            medians[i] = *it;
        }

//        printf("Median of: ");
//        std::list<double>::iterator it = points[i].begin();
//        while(it != points[i].end()) {
//            printf("%f, ", *it);
//            it++;
//        }
//        printf("\nis %f\n", medians[i]);
    }

    xpos[0] = 0;
    xpos[knots-1] = length-1;

    for(int i = 1 ; i < knots-1; i++) {
        xpos[i] = rsize/2+(i-1)*rsize+rsize/2;
//        printf("xpos %i: %f\n", i, xpos[i]);
    }
    
    itk::ImageLinearIteratorWithIndex< Image4DType > 
                out_it(output, output->GetRequestedRegion());
    out_it.SetDirection(3);
    out_it.SetIndex(index);
    gsl_spline_init(spline, xpos, medians, knots);
    for(out_it.GoToBeginOfLine(); !out_it.IsAtEndOfLine(); ++out_it) {
        out_it.Set(gsl_spline_eval(spline, out_it.GetIndex()[3], acc));
    }

    return 0;
};

int detrend_median(const Image4DType::Pointer fmri_img, Image4DType::IndexType index, 
            int knots, Image4DType::Pointer output)
{
    gsl_interp_accel *acc = gsl_interp_accel_alloc();
    gsl_spline *spline = gsl_spline_alloc(gsl_interp_cspline, knots);

    /* Go to index and start at time 0 at that voxel*/
    itk::ImageLinearConstIteratorWithIndex< Image4DType > 
                fmri_it(fmri_img, fmri_img->GetRequestedRegion());
    fmri_it.SetDirection(3);
    fmri_it.SetIndex(index);

    double xpos[knots];
    double medians[knots];

    int length = fmri_img->GetRequestedRegion().GetSize()[3];
    double rsize = (double)length / (knots-1);

    //generate a vector of point lists
    std::vector< std::list<double> > points(knots);
    for(fmri_it.GoToBeginOfLine(); !fmri_it.IsAtEndOfLine(); ++fmri_it) {
        unsigned int i = fmri_it.GetIndex()[3];
        if(i < rsize/2) {
//            printf("%u -> 0\n", i);
            points.front().push_back(fmri_it.Get());
        } else if(i > length - rsize/2) {
//            printf("%u -> last (%zu)\n", i, points.size());
            points.back().push_back(fmri_it.Get());
        } else {
//            printf("%u -> %i\n", i, 1+(int)(i/rsize-1./2));
            points[1+(int)(i/rsize-1./2)].push_back(fmri_it.Get());
        }
    }

    //find the median of each point list
    for(unsigned int i = 0 ; i < points.size() ; i++) {
        points[i].sort();
        if(points[i].size()%2 == 0) {
            std::list<double>::iterator it = points[i].begin();
            for(unsigned int j = 0 ; j != points[i].size()/2-1 ; j++)
                it++;
            medians[i] = *it;
            it++;
            medians[i] += *it;
            medians[i] /= 2.;
        } else {
            std::list<double>::iterator it = points[i].begin();
            for(unsigned int j = 0 ; j != points[i].size()/2 ; j++)
                it++;
            medians[i] = *it;
        }

//        printf("Median of: ");
//        std::list<double>::iterator it = points[i].begin();
//        while(it != points[i].end()) {
//            printf("%f, ", *it);
//            it++;
//        }
//        printf("\nis %f\n", medians[i]);
    }

    xpos[0] = 0;
    xpos[knots-1] = length-1;

    for(int i = 1 ; i < knots-1; i++) {
        xpos[i] = rsize/2+(i-1)*rsize+rsize/2;
//        printf("xpos %i: %f\n", i, xpos[i]);
    }
    
    itk::ImageLinearIteratorWithIndex< Image4DType > 
                out_it(output, output->GetRequestedRegion());
    out_it.SetDirection(3);
    out_it.SetIndex(index);
    gsl_spline_init(spline, xpos, medians, knots);
    for(out_it.GoToBeginOfLine(); !out_it.IsAtEndOfLine(); ++out_it) {
        out_it.Set(gsl_spline_eval(spline, out_it.GetIndex()[3], acc));
    }

    return 0;
};

int detrend_avg(const Image4DType::Pointer fmri_img, Image4DType::IndexType index, 
            int knots, Image4DType::Pointer output)
{
    gsl_interp_accel *acc = gsl_interp_accel_alloc();
    gsl_spline *spline = gsl_spline_alloc(gsl_interp_cspline, knots);

    /* Go to index and start at time 0 at that voxel*/
    itk::ImageLinearConstIteratorWithIndex< Image4DType > 
                fmri_it(fmri_img, fmri_img->GetRequestedRegion());
    fmri_it.SetDirection(3);
    fmri_it.SetIndex(index);

    const unsigned int COUNT_FB = 3; //2 points each for front and back
    double averages[knots];
    int counts[knots];
    uint32_t length = fmri_img->GetRequestedRegion().GetSize()[3];
    double rsize = (length - 2*COUNT_FB) / (knots-2);
//    std::cout << "rsize: " << rsize << ", length:" << length << "\n";
    for(int i = 0 ; i < knots ; i++) {
        counts[i] = 0;
        averages[i] = 0;
    }
    
    fmri_it.GoToBeginOfLine();
    for(uint32_t i=0; i < length; i++) {
        /* Figure out Region */
        if(i < COUNT_FB) {
            counts[0]++;
            averages[0] += fmri_it.Get();
        } else if(i >= length - COUNT_FB) {
            counts[knots-1]++;
            averages[knots-1] += fmri_it.Get();
        } else {
            counts[(int)((i-COUNT_FB)/rsize)+1]++;
            averages[(int)((i-COUNT_FB)/rsize)+1] += fmri_it.Get();
        }
        ++fmri_it;
    }

//    std::cout << "knots " << knots << "\n";
//    for(unsigned int i = 0 ; i < knots ; i++)
//        std::cout << i << ": " << counts[i] << "\n";

    double xpos[knots];
    xpos[0] = 0;
    xpos[knots-1] = length-1;
    unsigned int pos = COUNT_FB;
    for(int i = 1 ; i < knots-1 ; i++){
        xpos[i] = (pos + counts[i]-1 + pos)/2.;
        pos += counts[i];
    }

    for(int i = 0 ; i < knots ; i++) {
        averages[i] /= counts[i];
    }

    itk::ImageLinearIteratorWithIndex< Image4DType > 
                out_it(output, output->GetRequestedRegion());
    out_it.SetDirection(3);
    out_it.SetIndex(index);
    gsl_spline_init(spline, xpos, averages, knots);
    for(out_it.GoToBeginOfLine(); !out_it.IsAtEndOfLine(); ++out_it) {
        out_it.Set(gsl_spline_eval(spline, out_it.GetIndex()[3], acc));
    }

    return 0;
};

Image4DType::Pointer getspline(const Image4DType::Pointer fmri_img, 
            unsigned int knots)
{
    Image4DType::Pointer outimage = Image4DType::New();
    outimage->SetRegions(fmri_img->GetRequestedRegion());
    outimage->Allocate();
    outimage->FillBuffer(0);

    /* Fmri Iterators */
    itk::ImageLinearConstIteratorWithIndex< Image4DType > 
                fmri_it(fmri_img, fmri_img->GetRequestedRegion());
    fmri_it.SetDirection(0);
    
    itk::ImageLinearConstIteratorWithIndex< Image4DType >
                fmri_stop(fmri_img, fmri_img->GetRequestedRegion());
    fmri_stop.SetDirection(3);
    fmri_stop.GoToBegin();
    ++fmri_stop;

    for(fmri_it.GoToBegin(); fmri_it != fmri_stop ; fmri_it.NextLine()) {
        for( ; !fmri_it.IsAtEndOfLine(); ++fmri_it) {
            detrend_avg(fmri_img, fmri_it.GetIndex(), knots, outimage);
        }
    }

    outimage->CopyInformation(fmri_img);
    outimage->SetMetaDataDictionary(fmri_img->GetMetaDataDictionary());
    return outimage;
}

int dc_bump(const Image4DType::Pointer fmri_img, Image4DType::IndexType index, 
            Image4DType::Pointer output)
{
    /* Go to index and start at time 0 at that voxel*/
    itk::ImageLinearConstIteratorWithIndex< Image4DType > 
                fmri_it(fmri_img, fmri_img->GetRequestedRegion());
    fmri_it.SetDirection(3);
    fmri_it.SetIndex(index);
    
    //generate a vector of point lists, get median
    std::vector<double> points(fmri_img->GetRequestedRegion().GetSize()[3]);
    std::vector<double>::iterator vit = points.begin();
    for(fmri_it.GoToBeginOfLine(); !fmri_it.IsAtEndOfLine(); vit++, ++fmri_it) {
        *vit = fmri_it.Get();
    }
    sort(points.begin(), points.end());
    double median = points[points.size()/2];

    //calculate the absolute deviations, median
    vit = points.begin();
    while(vit != points.end()) {
        *vit = fabs(*vit - median);
        vit++;
    }
    sort(points.begin(), points.end());
    double mad = 1.4826*points[points.size()/2];
    
    itk::ImageLinearIteratorWithIndex< Image4DType > 
                out_it(output, output->GetRequestedRegion());
    out_it.SetDirection(3);
    out_it.SetIndex(index);
    for(out_it.GoToBeginOfLine(), fmri_it.GoToBeginOfLine(); !out_it.IsAtEndOfLine();
                ++out_it, ++fmri_it) {
        out_it.Set(fmri_it.Get()+mad);
    }

    return 0;
}

Image4DType::Pointer dc_bump(const Image4DType::Pointer fmri_img)
{
    Image4DType::Pointer outimage = Image4DType::New();
    outimage->SetRegions(fmri_img->GetRequestedRegion());
    outimage->Allocate();
    outimage->FillBuffer(0);

    /* Fmri Iterators */
    itk::ImageLinearConstIteratorWithIndex< Image4DType > 
                fmri_it(fmri_img, fmri_img->GetRequestedRegion());
    fmri_it.SetDirection(0);
    
    itk::ImageLinearConstIteratorWithIndex< Image4DType >
                fmri_stop(fmri_img, fmri_img->GetRequestedRegion());
    fmri_stop.SetDirection(3);
    fmri_stop.GoToBegin();
    ++fmri_stop;

    for(fmri_it.GoToBegin(); fmri_it != fmri_stop ; fmri_it.NextLine()) {
        for( ; !fmri_it.IsAtEndOfLine(); ++fmri_it) {
            dc_bump(fmri_img, fmri_it.GetIndex(), outimage);
        }
    }

    outimage->CopyInformation(fmri_img);
    outimage->SetMetaDataDictionary(fmri_img->GetMetaDataDictionary());
    return outimage;
}

Image4DType::Pointer getspline_m(const Image4DType::Pointer fmri_img, 
            unsigned int knots)
{
    
    Image4DType::Pointer outimage = Image4DType::New();
    outimage->SetRegions(fmri_img->GetRequestedRegion());
    outimage->Allocate();
    outimage->FillBuffer(0);

    /* Fmri Iterators */
    itk::ImageLinearConstIteratorWithIndex< Image4DType > 
                fmri_it(fmri_img, fmri_img->GetRequestedRegion());
    fmri_it.SetDirection(0);
    
    itk::ImageLinearConstIteratorWithIndex< Image4DType >
                fmri_stop(fmri_img, fmri_img->GetRequestedRegion());
    fmri_stop.SetDirection(3);
    fmri_stop.GoToBegin();
    ++fmri_stop;

    for(fmri_it.GoToBegin(); fmri_it != fmri_stop ; fmri_it.NextLine()) {
        for( ; !fmri_it.IsAtEndOfLine(); ++fmri_it) {
            detrend_median(fmri_img, fmri_it.GetIndex(), knots, outimage);
        }
    }

    outimage->CopyInformation(fmri_img);
    outimage->SetMetaDataDictionary(fmri_img->GetMetaDataDictionary());
    return outimage;
}

Image4DType::Pointer deSplineBlind(const Image4DType::Pointer fmri_img,
            unsigned int numknots, std::string base)
{
    boost::mpi::communicator world;
    const unsigned int rank = world.rank();
    std::cerr << "Making Spline" << std::endl;
//    Image4DType::Pointer spline = getspline(fmri_img, numknots);
    Image4DType::Pointer spline = getspline_m(fmri_img, numknots);
    
    if(rank == 0){
    std::string tmp = base;
    tmp.append("spline.nii.gz");
    printf("Writing %s\n", tmp.c_str());
    itk::ImageFileWriter< Image4DType >::Pointer writer = 
                itk::ImageFileWriter< Image4DType >::New();
    writer->SetInput(spline);
    writer->SetFileName(tmp);
    writer->Update();
    }

    /* Rescale (fmri - spline)/avg*/
    Image4DType::Pointer avg = extrude(Tmean(fmri_img), 
                fmri_img->GetRequestedRegion().GetSize()[3]);
    if(rank == 0 ){
    std::string tmp = base;
    tmp.append("fmri_img.nii.gz");
    printf("Writing %s\n", tmp.c_str());
    itk::ImageFileWriter< Image4DType >::Pointer writer = 
                itk::ImageFileWriter< Image4DType >::New();
    writer->SetInput(fmri_img);
    writer->SetFileName(tmp);
    writer->Update();
    }
    if(rank == 0){
    std::string tmp = base;
    tmp.append("avg.nii.gz");
    printf("Writing %s\n", tmp.c_str());
    itk::ImageFileWriter< Image4DType >::Pointer writer = 
                itk::ImageFileWriter< Image4DType >::New();
    writer->SetInput(avg);
    writer->SetFileName(tmp);
    writer->Update();
    }
    SubF4::Pointer sub = SubF4::New();   
    DivF::Pointer div = DivF::New();   
    sub->SetInput1(fmri_img);
    sub->SetInput2(spline);
    div->SetInput1(sub->GetOutput());
    div->SetInput2(avg);
    div->Update();

    div->GetOutput()->CopyInformation(fmri_img);
    div->GetOutput()->SetMetaDataDictionary(
                fmri_img->GetMetaDataDictionary() );
    return div->GetOutput();
}

/**************************************************************************
 * Informed Spline Generation
**************************************************************************/
int detrend_avg(const Image4DType::Pointer fmri_img, Image4DType::IndexType index, 
            const std::vector< unsigned int >& knots, 
            Image4DType::Pointer output)
{
    static gsl_interp_accel *acc = gsl_interp_accel_alloc();
    static gsl_spline *spline = gsl_spline_alloc(gsl_interp_cspline, knots.size());
        
    double min = 1e100;
    unsigned int posmin = 0;
    double min2 = 1e100;
    unsigned int posmin2 = 0;

    double xpos[knots.size()];
    double level[knots.size()];

    std::vector<DataType> tmp(3, 0);
    xpos[0] = knots[0];
    for(index[3] = 0 ; index[3] < 3 ; index[3]++) 
        tmp[index[3]] = fmri_img->GetPixel(index);
    std::sort(tmp.begin(), tmp.end());
    for(index[3] = 0 ; index[3] < 3 ; index[3]++) {
        if(tmp[1] - fmri_img->GetPixel(index) < .0001) {
            level[0] = tmp[1];
        }
    }
   
    posmin = 0;
    posmin2 = 0;
    for(index[3] = knots.back()-1 ; index[3] < knots.back()+2 ; index[3]++) 
        tmp[index[3]-knots.back()+1] = fmri_img->GetPixel(index);
    std::sort(tmp.begin(), tmp.end());
    for(index[3] = knots.back()-1 ; index[3] < knots.back()+2 ; index[3]++) {
        if(tmp[1] > fmri_img->GetPixel(index))
            posmin = index[3];
        if(tmp[1] == fmri_img->GetPixel(index)) 
            posmin2 = index[3];
    }
    xpos[knots.size()-1] = knots.back();
    index[3] = posmin;
    level[knots.size()-1] = fmri_img->GetPixel(index);

    //set all the other knots based on the lower 2/5
    for(unsigned int ii = 1 ; ii < knots.size()-1 ; ii++) {
        min = 1e100;
        min2 = 1e100;

        for(int jj = -3 ; jj <= 3; jj++) {
            index[3] = knots[ii]+jj;
            if(fmri_img->GetPixel(index) < min) {
                min2 = min;
                posmin2 = posmin;
                min = fmri_img->GetPixel(index);
                posmin = index[3];
            } else if(fmri_img->GetPixel(index) < min2) {
                min2 = fmri_img->GetPixel(index);
                posmin2 = index[3];
            }
        }
        xpos[ii] = knots[ii];
        level[ii] = min2;
    }

    itk::ImageLinearIteratorWithIndex< Image4DType > 
                out_it(output, output->GetRequestedRegion());
    out_it.SetDirection(3);
    index[3] = 0;
    out_it.SetIndex(index);
    gsl_spline_init(spline, xpos, level, knots.size());
    for(out_it.GoToBeginOfLine(); !out_it.IsAtEndOfLine(); ++out_it) {
        out_it.Set(gsl_spline_eval(spline, out_it.GetIndex()[3], acc));
    }

    return 0;
}

bool compare_l(Activation A, Activation B)
{
    return A.level < B.level;
}

bool compare_t(Activation A, Activation B)
{
    return A.time < B.time;
}


void getknots(std::list<Activation>& mins, 
            std::vector<Activation>& stim)
{
    const double RANGE = 20;
    std::list<Activation> fifo;
    std::vector<Activation>::iterator it = stim.begin();
    double frame_begin = it->time;
    double sum_prev_prev = 1;
    double sum_prev = 0;
    double sum = 0;
    double time_prev = RANGE;
    while(it->time - frame_begin < RANGE) {
        sum += ((it+1)->time-it->time)*(it->level);
        fifo.push_front(*it);
        it++;
    }
    sum -= (it-1)->level*(it->time-RANGE);

    Activation tmp;

    while(fifo.size() != 1 || it != stim.end()) {
        //find local minima
        if(sum > sum_prev && sum_prev <= sum_prev_prev) {
            tmp.time = time_prev;
            tmp.level = sum_prev;
            mins.push_back(tmp);
        }

        sum_prev_prev = sum_prev;
        sum_prev = sum;
        time_prev = fifo.back().time+RANGE;

        std::list<Activation>::reverse_iterator sec = ++fifo.rbegin();
        if(fifo.size() <= 1 || ( it != stim.end() && 
                    it->time - fifo.back().time - RANGE < 
                    sec->time - fifo.back().time ) ) {
            double delta = it->time - fifo.back().time - RANGE;
            sum -= fifo.back().level*delta;
            sum += delta*fifo.front().level;
            fifo.back().time += delta;
            fifo.push_front(*it);
            it++;
        } else {
            double delta = sec->time - fifo.back().time;
            sum -= fifo.back().level*delta;
            sum += delta*fifo.front().level;
            fifo.pop_back();
        }
    }
}

Image4DType::Pointer getspline(const Image4DType::Pointer fmri_img,
            const std::vector<unsigned int>& knots)
            
{
    Image4DType::Pointer outimage = Image4DType::New();
    outimage->SetRegions(fmri_img->GetRequestedRegion());
    outimage->Allocate();
    outimage->FillBuffer(0);

    /* Fmri Iterators */
    itk::ImageLinearConstIteratorWithIndex< Image4DType > 
                fmri_it(fmri_img, fmri_img->GetRequestedRegion());
    fmri_it.SetDirection(0);
    
    itk::ImageLinearConstIteratorWithIndex< Image4DType >
                fmri_stop(fmri_img, fmri_img->GetRequestedRegion());
    fmri_stop.SetDirection(3);
    fmri_stop.GoToBegin();
    ++fmri_stop;

    std::cerr << "Knots:" << std::endl;
    for(unsigned int ii = 0 ; ii < knots.size() ;ii++) {
        std::cerr << knots[ii] << std::endl;
    }
    
    for(fmri_it.GoToBegin(); fmri_it != fmri_stop ; fmri_it.NextLine()) {
        for( ; !fmri_it.IsAtEndOfLine(); ++fmri_it) {
            detrend_avg(fmri_img, fmri_it.GetIndex(), knots, outimage);
        }
    }

    outimage->CopyInformation(fmri_img);
    outimage->SetMetaDataDictionary(fmri_img->GetMetaDataDictionary());
    return outimage;
}


/* Uses knots at crossover points of HRF */
Image4DType::Pointer deSplineByStim(const Image4DType::Pointer fmri_img,
            std::vector<Activation>& stim, double dt, std::string base)
{
    boost::mpi::communicator world;
    const unsigned int rank = world.rank();
    itk::Image< DataType, 1>::Pointer canon = getCanonical(stim, dt, 0,
                fmri_img->GetRequestedRegion().GetSize()[3]*dt);
   
    //add first three
    std::vector<unsigned int> knots(1, 1);

    const unsigned int SKIP = 6;
    const double THRESH = .01;
    double prev = THRESH+1;
    double cur = THRESH+1;
    int zeroStart = 0;
    bool zeroZone = false;
    // Find points in the middle that are 0 crossings/0
    itk::Image<DataType, 1>::IndexType index, next, prevprev;
    for(index[0] = SKIP ; index[0] < canon->GetRequestedRegion().GetSize()[0]-SKIP ; index[0]++) {
        prev = cur;
        cur = canon->GetPixel(index);
        std::cerr << cur << std::endl;
        //zero area
        if(fabs(cur) < THRESH) {
            std::cerr  << "zero " << index[0];
            if(!zeroZone) {
                std::cerr  << " (start) ";
                zeroZone = true;
                zeroStart = index[0];
            }
            std::cerr << std::endl;
        //end of zero zone, take the middle
        } else if(fabs(prev) < THRESH) {
            std::cerr << "Adding " << index[0] << std::endl;
            knots.push_back((zeroStart+index[0]-1)/2);
            index[0] += SKIP;
            zeroZone = false;
        //neither this nor previous are 0, but they have different signs so cross
        } else if((cur < THRESH && prev > THRESH) || (cur > THRESH && prev < THRESH)) {
            std::cerr << "Crossing " << index[0] << std::endl;
            next[0] = index[0]+1;
            prevprev[0] = index[0]-2;
            //crossing in between these two, so for [A prev cur B] choose minumum:
            // min(A + prev + cur, prev + cur + B)
            if(fabs(canon->GetPixel(next) + prev + cur) < 
                        fabs(canon->GetPixel(prevprev) + prev + cur)) {
                std::cerr << "Adding " << index[0] << std::endl;
                knots.push_back(index[0]);
                index[0] += SKIP;
            } else {
                std::cerr << "Adding " << index[0] -1 << std::endl;
                knots.push_back(index[0] - 1);
                index[0] += SKIP-1;
            }
        }
    }

    if(fabs(prev) < THRESH) {
        std::cerr << "Adding " << index[0] << std::endl;
        knots.push_back((zeroStart+index[0]-1)/2);
        index[0] += SKIP;
        zeroZone = false;
    }

    //add final three
//    knots.push_back(canon->GetRequestedRegion().GetSize()[0]-2);
    if(knots.size() < 4) {
        std::cerr << "WARNING! Not enough low stimulus areas to intelligently "
                    << "place knots" << std::endl;
    }

    std::cerr << "Making Spline" << std::endl;
    Image4DType::Pointer spline = getspline(fmri_img, knots);
    
    if(rank == 0){
    std::string tmp = base;
    tmp.append("spline.nii.gz");
    printf("Writing %s", tmp.c_str());
    itk::ImageFileWriter< Image4DType >::Pointer writer = 
                itk::ImageFileWriter< Image4DType >::New();
    writer->SetInput(spline);
    writer->SetFileName(tmp);
    writer->Update();
    }

    /* Rescale (fmri - spline)/avg*/
    
    
    Image4DType::Pointer avg = extrude(Tmean(fmri_img), 
                fmri_img->GetRequestedRegion().GetSize()[3]);
    if(rank == 0){
    std::string tmp = base;
    tmp.append("fmri_img.nii.gz");
    printf("Writing %s\n", tmp.c_str());
    itk::ImageFileWriter< Image4DType >::Pointer writer = 
                itk::ImageFileWriter< Image4DType >::New();
    writer->SetInput(fmri_img);
    writer->SetFileName(tmp);
    writer->Update();
    }
    if(rank == 0){
    std::string tmp = base;
    tmp.append("avg.nii.gz");
    printf("Writing %s\n", tmp.c_str());
    itk::ImageFileWriter< Image4DType >::Pointer writer = 
                itk::ImageFileWriter< Image4DType >::New();
    writer->SetInput(avg);
    writer->SetFileName(tmp);
    writer->Update();
    }
    SubF4::Pointer sub = SubF4::New();   
    DivF::Pointer div = DivF::New();   
    sub->SetInput1(fmri_img);
    sub->SetInput2(spline);
    div->SetInput1(sub->GetOutput());
    div->SetInput2(avg);
    div->Update();

    div->GetOutput()->CopyInformation(fmri_img);
    div->GetOutput()->SetMetaDataDictionary(
                fmri_img->GetMetaDataDictionary() );
    return div->GetOutput();
}
