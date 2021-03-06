#include "tools.h"

#include <climits>
#include <vector>
#include <cmath>
#include <itkComplexToModulusImageFilter.h>
#include <itkComplexToPhaseImageFilter.h>
#include <itkFFTRealToComplexConjugateImageFilter.h>
#include <itkConvolutionImageFilter.h>
#include <itkResampleImageFilter.h>
#include <itkImageLinearIteratorWithIndex.h>

#include <itkAddImageFilter.h>
#include <itkSubtractImageFilter.h>
#include <itkSquareImageFilter.h>
#include <gsl/gsl_randist.h>

typedef itk::AddImageFilter< Image3DType > AddF3;
typedef itk::SubtractImageFilter< Image3DType > SubF3;
typedef itk::SquareImageFilter< Image3DType, Image3DType > SqrF3;

/* Calculates the percent difference between input1, and input2,
 * using input1 as the reference, using orientation
 */
Image4DType::Pointer pctDiff(const Image4DType::Pointer input1,
            const Image4DType::Pointer input2)
{
    itk::ImageLinearConstIteratorWithIndex<Image4DType> iter1(
                input1, input1->GetRequestedRegion());
    iter1.SetDirection(3);
    iter1.GoToBegin();
    
    Image4DType::Pointer out = Image4DType::New();
    out->SetRegions(input1->GetRequestedRegion());
    out->Allocate();
    out->FillBuffer(1);
    
    itk::ImageLinearIteratorWithIndex<Image4DType> itero(
                out, out->GetRequestedRegion());
    itero.SetDirection(3);
    itero.GoToBegin();

    Image4DType::SpacingType space = input2->GetSpacing();
    space[3] = input1->GetSpacing()[3];
    input2->SetSpacing(space);

    while(!iter1.IsAtEnd()) {
        while(!iter1.IsAtEndOfLine()) {
            Image4DType::IndexType index = iter1.GetIndex();
            if(input2->GetRequestedRegion().IsInside(index)) {
//                printf("%li %li %li %li\n", iter1.GetIndex()[0], iter1.GetIndex()[1],
//                            iter1.GetIndex()[2], iter1.GetIndex()[3]);
                double diff = (input2->GetPixel(index)-iter1.Get());
                if(diff == 0) itero.Set(0);
                else itero.Set(diff/fmax(abs(input2->GetPixel(index)), abs(iter1.Get())));
            }
            ++iter1; ++itero;
        }
        iter1.NextLine();
        itero.NextLine();
    }
    return out;
}

/* Calculates the percent difference between input1, and input2,
 * using input1 as the reference, using orientation
 */
Image4DType::Pointer pctDiffOrient(const Image4DType::Pointer input1,
            const Image4DType::Pointer input2)
{
    itk::ImageLinearConstIteratorWithIndex<Image4DType> iter1(
                input1, input1->GetRequestedRegion());
    iter1.SetDirection(3);
    iter1.GoToBegin();
    
    Image4DType::Pointer out = Image4DType::New();
    out->SetRegions(input1->GetRequestedRegion());
    out->Allocate();
    out->FillBuffer(1);
    
    itk::ImageLinearIteratorWithIndex<Image4DType> itero(
                out, out->GetRequestedRegion());
    itero.SetDirection(3);
    itero.GoToBegin();

    Image4DType::SpacingType space = input2->GetSpacing();
    space[3] = input1->GetSpacing()[3];
    input2->SetSpacing(space);

    while(!iter1.IsAtEnd()) {
        while(!iter1.IsAtEndOfLine()) {
            Image4DType::PointType point;
            input1->TransformIndexToPhysicalPoint(iter1.GetIndex(), point);
            Image4DType::IndexType index;
            input2->TransformPhysicalPointToIndex(point, index);
            if(input2->GetRequestedRegion().IsInside(index)) {
//                printf("%li %li %li %li\n", iter1.GetIndex()[0], iter1.GetIndex()[1],
//                            iter1.GetIndex()[2], iter1.GetIndex()[3]);
                double diff = (input2->GetPixel(index)-iter1.Get());
                if(diff == 0) itero.Set(0);
                else itero.Set(diff/fmax(abs(input2->GetPixel(index)), abs(iter1.Get())));
            }
            ++iter1; ++itero;
        }
        iter1.NextLine();
        itero.NextLine();
    }
    return out;
}

/* Calculates the mutual information for the two images input1/input2
 * mutual informaiton is performed between samples in the time dimension
 * bins1/2 - the number of bins to use when approximating the joint
 *            distribution (usually 10)
*/
Image3DType::Pointer mutual_info(uint32_t bins1, uint32_t bins2,
            const Image4DType::Pointer input1, const Image4DType::Pointer input2)
{
    /* Create Iterators and Output */
    if(input1->GetRequestedRegion().GetSize()[3] != 
                input2->GetRequestedRegion().GetSize()[3]) 
        throw -1;
    
    itk::ImageLinearConstIteratorWithIndex<Image4DType> it1(
                input1, input1->GetRequestedRegion());
    it1.SetDirection(3);
    it1.GoToBegin();
    
    itk::ImageLinearConstIteratorWithIndex<Image4DType> it2(
                input2, input2->GetRequestedRegion());
    it2.SetDirection(3);
    it2.GoToBegin();

    Image4DType::IndexType index4;
    Image3DType::Pointer out = Image3DType::New();
    Image4DType::SizeType size4 = input1->GetRequestedRegion().GetSize();
    {
        Image3DType::SizeType size3 = {{size4[0], size4[1], size4[2]}};
        out->SetRegions(size3);
        out->Allocate();
        out->FillBuffer(0);
        copyInformation<Image4DType, Image3DType>(input1, out);
    }

    uint32_t tlen = input1->GetRequestedRegion().GetSize()[3];

    /* Create the Imperical Distribution Matrix/Arrays */
    //allocate
    double** data = new double*[bins1];
    double* tmp = new double[bins1*bins2];
    for(uint32_t i = 0 ; i < bins1; i++) {
        data[i] = &tmp[i*bins2];
    }

    double* marginal1 = new double[bins1];
    double* marginal2 = new double[bins2];

    while(!it1.IsAtEnd()) {
        //zero them out
        for(uint32_t i = 0 ; i < bins1; i++) {
            for(uint32_t j = 0 ; j < bins2; j++) {
                data[i][j] = 0;
            }
        }

        for(uint32_t i = 0 ; i < bins1; i++) 
            marginal1[i] = 0;
        for(uint32_t i = 0 ; i < bins2; i++) 
            marginal2[i] = 0;

        /* Find the min and max for each dataset */
        double min1 = it1.Get();
        double max1 = it1.Get();
        double min2 = it2.Get();
        double max2 = it2.Get();
        
        while(!it1.IsAtEndOfLine()) {
            if(!(min1 <= it1.Get()))
                min1 = it1.Get();
            if(!(max1 >= it1.Get()))
                max1 = it1.Get();
            if(!(min2 <= it2.Get()))
                min2 = it2.Get();
            if(!(max2 >= it2.Get()))
                max2 = it2.Get();
            ++it1; ++it2;
        }

        if(max1 - min1 > INT_MAX || max2 - min2 > INT_MAX || 
            max1 == min1 || max2 == min2) {
            it1.NextLine();
            it2.NextLine();
            continue;
        }
        max1 += (max1-min1)*1e-9;
        max2 += (max2-min2)*1e-9;
        
        it1.GoToBeginOfLine();
        it2.GoToBeginOfLine();
        /* Put Each Element in a bin */
        int bin1, bin2;
        while(!it1.IsAtEndOfLine()) {
            bin1 = (it1.Get()-min1)*bins1/(max1-min1);
            bin2 = (it2.Get()-min2)*bins2/(max2-min2);
            data[(int)bin1][(int)bin2] += 1./tlen;
            marginal1[bin1] += 1./tlen;
            marginal2[bin2] += 1./tlen;
            ++it1; ++it2;
        }

        /* Calculate Mutual Information */
        double mi = 0;
        for(uint32_t y = 0 ; y < bins1; y++) { 
            for(uint32_t x = 0 ; x < bins2; x++) {
                if(data[y][x])
                    mi += data[y][x]*log2(data[y][x]/(marginal1[y]*marginal2[x]));
            }
        }
        mi -= (bins1*bins2)/(2.*tlen);

        index4 = it1.GetIndex();
        Image3DType::IndexType index3 = {{index4[0], index4[1], index4[2]}};
        out->SetPixel(index3, mi);

        it1.NextLine();
        it2.NextLine();
    }
    return out;
}
    
/* Takes the difference at each time point, squares it
 * then calculates the mean of that, Mean-Squared-Error
 */
Image3DType::Pointer mse(const Image4DType::Pointer input1,
            const Image4DType::Pointer input2)
{
    if(input1->GetRequestedRegion().GetSize() != 
                input2->GetRequestedRegion().GetSize()) {
        return NULL;
    }
    
    itk::ImageLinearConstIteratorWithIndex<Image4DType> iter1(
                input1, input1->GetRequestedRegion());
    iter1.SetDirection(3);
    iter1.GoToBegin();
    
    itk::ImageLinearConstIteratorWithIndex<Image4DType> iter2(
                input2, input2->GetRequestedRegion());
    iter2.SetDirection(3);
    iter2.GoToBegin();

    Image4DType::IndexType index4;
    Image3DType::Pointer out = Image3DType::New();
    Image4DType::SizeType size4 = input1->GetRequestedRegion().GetSize();
    {
        Image3DType::SizeType size3 = {{size4[0], size4[1], size4[2]}};
        out->SetRegions(size3);
        out->Allocate();
    }

    while(!iter1.IsAtEnd() && !iter2.IsAtEnd()) {
        index4 = iter1.GetIndex();
        double mean = 0;
        while(!iter1.IsAtEndOfLine() && !iter2.IsAtEndOfLine()) {
            mean += pow(iter1.Get() - iter2.Get(), 2);
            ++iter1; ++iter2;
        }
        index4 = iter1.GetIndex();
        Image3DType::IndexType index3 = {{index4[0], index4[1], index4[2]}};
        out->SetPixel(index3, mean/size4[3]);
        iter1.NextLine();
        iter2.NextLine();
    }
    return out;
}

/* Reads a file with <time> <level> pairs, and puts the time and level
 * into a struct which is then packed into a vector of these pairs
 */
std::vector<Activation> read_activations(std::string filename, std::string extra)
{       
    filename.append(extra);
    FILE* fin = fopen(filename.c_str(), "r");
    std::vector< Activation > output;
    if(!fin) {
        fprintf(stderr, "read_activations: \"%s\" is invalid\n", filename.c_str());
        return output;
    }
    
    char* input = NULL;
    size_t size = 0;
    char* curr = NULL;
    double prev = 1/0.;
    Activation parsed;
    printf("Parsing activations\n");
    while(getline(&input, &size, fin) && !feof(fin)) {
        parsed.time = strtod(input, &curr);
        parsed.level = strtod(curr, NULL);
        
        if(!(prev == parsed.level)) {
            output.push_back(parsed);
        }
        prev = parsed.level;
        free(input);
        input = NULL;
    }
    fclose(fin);
    return output;
}

/* Write a vector to std out, not no endline applied */
void outputVector(std::ostream& out, indii::ml::aux::vector mat) 
{
  unsigned int i;
  for (i = 0; i < mat.size(); i++) {
      out << std::setw(15) << mat(i);
  }
}

/* Write a matrix to std out, no final endline applied */
void outputMatrix(std::ostream& out, indii::ml::aux::matrix mat) 
{
  unsigned int i, j;
  for (j = 0; j < mat.size2(); j++) {
    for (i = 0; i < mat.size1(); i++) {
      out << std::setw(15) << mat(i,j);
    }
    out << std::endl;
  }
}

/* Takes an unsigned int and rounds up to the neared
 * power of 2. Used for FFT */
unsigned int round_power_2(unsigned int in) 
{
    unsigned int count = 0;
    while(in != 0) {
        count++;
        in >>= 1;
    }

    //set the size of the temporary var and the buffer
    return(1 << count);
}

/* Takes an index in a 4D image and puts all the time points in
 * the same 3D voxel position in a 1D image, used for FFT */
void copyTimeLine(Image4DType::Pointer src, 
            itk::Image<DataType,1>::Pointer dest,
            Image4DType::IndexType pos) 
{
    itk::ImageLinearIteratorWithIndex< itk::Image< DataType, 1 > >
                it(dest, dest->GetRequestedRegion());
    it.SetDirection(0);
    it.GoToBegin();

    while(!it.IsAtEnd()) {
        if(it.GetIndex()[0] > (int)src->GetRequestedRegion().GetSize()[3])
            break;
        pos[3] = it.GetIndex()[0];
        it.Set(src->GetPixel(pos));
        ++it;
    }
}

/* Takes an index in a 4D image and puts all the time points in
 * a 1D image into the same 3D voxel position in a 3D image, opposite of above */
void copyTimeLine(itk::Image<DataType, 1>::Pointer src, Image4DType::Pointer dest,
            Image4DType::IndexType pos) 
{
    itk::ImageLinearIteratorWithIndex< itk::Image< DataType, 1 > >
                it(src, src->GetRequestedRegion());
    it.SetDirection(0);
    it.GoToBegin();

    while(!it.IsAtEnd()) {
        if(it.GetIndex()[0] > (int)dest->GetRequestedRegion().GetSize()[3]) 
            break;
        pos[3] = it.GetIndex()[0];
        dest->SetPixel(pos, it.Get());
        ++it;
    }
}

/* Calculates a canonical HRF 1D image for the given parameters, 
 * (stimulus, TR, start time, stop time)
 */
double hrfParam[] = {6, 16, 1, 1, 6, 0, 32};
itk::Image<DataType, 1>::Pointer getCanonical(std::vector<Activation> stim, double TR,
            double start, double stop)
{
    typedef itk::ConvolutionImageFilter< itk::Image<DataType, 1> > ConvT;
    
    /*Setup Stimulus Image*/
    double dt = .05;
    itk::Image<DataType, 1>::Pointer inputs = itk::Image<DataType, 1>::New();
    double timeFOV = stop - start + 2*hrfParam[6];
    itk::Image<DataType, 1>::SizeType isize = {{timeFOV/dt}};
    inputs->SetRegions(isize);
    inputs->Allocate();

    double level = 0;
    unsigned int stimpos = 0;
    itk::ImageLinearIteratorWithIndex< itk::Image<DataType, 1> > it1
                (inputs, inputs->GetRequestedRegion());
    it1.GoToBegin();
    for(int ii = (start-hrfParam[6])/dt ; !it1.IsAtEndOfLine() ; ii++) {
        while(stimpos < stim.size() && stim[stimpos].time < ii*dt) {
            level = stim[stimpos].level;
            stimpos++;
        }
        it1.Set(level);
        ++it1;
    }
    {
        itk::ImageFileWriter< itk::Image<DataType, 1> >::Pointer writer = 
                    itk::ImageFileWriter< itk::Image<DataType, 1> >::New();
        writer->SetInput(inputs);
        writer->SetFileName("impulse.nii.gz");
        writer->Update();
    }

    /* Setup HRF Image */
    itk::Image<DataType, 1>::Pointer hrf = itk::Image<DataType, 1>::New();
    itk::Image<DataType, 1>::SizeType hsize = {{(int)hrfParam[6]/dt+1}};
    hrf->SetRegions(hsize);
    hrf->Allocate();
    itk::ImageLinearIteratorWithIndex< itk::Image<DataType, 1> > it2
                (hrf, hrf->GetRequestedRegion());
    it2.GoToBegin();

    for(unsigned int ii = 0; !it2.IsAtEndOfLine() ; ii++) {
        it2.Set(gsl_ran_gamma_pdf(ii, hrfParam[0]/hrfParam[2], hrfParam[2]/dt) -
                    gsl_ran_gamma_pdf(ii,hrfParam[1]/hrfParam[3], hrfParam[3]/dt)/hrfParam[4]);
        ++it2;
    }
    {
        itk::ImageFileWriter< itk::Image<DataType, 1> >::Pointer writer = 
                    itk::ImageFileWriter< itk::Image<DataType, 1> >::New();
        writer->SetInput(hrf);
        writer->SetFileName("hrf.nii.gz");
        writer->Update();
    }
    
    /* Perform hte Convolution */
    ConvT::Pointer conv = ConvT::New();
    conv->NormalizeOn();
    conv->SetImageKernelInput(hrf);
    conv->SetInput(inputs);
    conv->Update();
    conv->GetOutput()->SetSpacing(&dt);
    double tmp = -hrfParam[6];
    conv->GetOutput()->SetOrigin(&tmp);
    {
        itk::ImageFileWriter< itk::Image<DataType, 1> >::Pointer writer = 
                    itk::ImageFileWriter< itk::Image<DataType, 1> >::New();
        writer->SetInput(conv->GetOutput());
        writer->SetFileName("convolved.nii.gz");
        writer->Update();
    }
   
    //downsample
    itk::Image<DataType, 1>::SizeType outsize = {{(int)((stop - start)/TR)}};
    typedef itk::ResampleImageFilter< itk::Image<DataType, 1>, 
                itk::Image<DataType, 1>, DataType > ResampT;

    ResampT::Pointer resamp = ResampT::New();
    resamp->SetInput(conv->GetOutput());
    resamp->SetSize(outsize);
    resamp->SetOutputSpacing(&TR);
    resamp->Update();
    {
        itk::ImageFileWriter< itk::Image<DataType, 1> >::Pointer writer = 
                    itk::ImageFileWriter< itk::Image<DataType, 1> >::New();
        writer->SetInput(resamp->GetOutput());
        writer->SetFileName("resamp.nii.gz");
        writer->Update();
    }
    return resamp->GetOutput();
}

/* Takes the FFT of an input image (in the time dimension)
 * and saves the result in the output. Note this is only a
 * FFT over time, not over space
 */
Image4DType::Pointer fft_image(Image4DType::Pointer inimg)
{
    typedef itk::Image< std::complex<DataType>, 1> ComplexT;
    typedef Image4DType Real4DT;
    typedef itk::OrientedImage< DataType, 1> Real1DTBase;
    typedef itk::Image< DataType, 1> Real1DT;
    typedef itk::ComplexToModulusImageFilter< ComplexT, Real1DT > ModT;
    typedef itk::ComplexToPhaseImageFilter< ComplexT, Real1DT > PhasT;
    typedef itk::FFTRealToComplexConjugateImageFilter< DataType, 1 > FFT1DT;
    typedef itk::CastImageFilter< Real1DT, Real1DTBase> castF;

    //Set up sizes and indices to grab a single time vector
    //copy it into another image
    Real4DT::SizeType inSize = inimg->GetRequestedRegion().GetSize();
    Real4DT::SizeType timeSizeIn = {{1, 1, 1, inSize[3]}};
    Real4DT::IndexType index = {{0, 0, 0, 0}};
    Real1DT::SizeType lineSize = {{1}};
    Real4DT::RegionType regionSel;


    //Round up the nearest power of 2 for image length
    //and create a temporary image for FFT's
    lineSize[0] = round_power_2(inSize[3])*4;
    Real4DT::SizeType timeSizeOut = {{1, 1, 1, lineSize[0]}};
    Real1DT::Pointer working = Real1DT::New();
    working->SetRegions(lineSize);
    working->Allocate();
    castF::Pointer cast = castF::New();
    cast->SetInput(working);
    
    //Setup output image
    Real4DT::Pointer out = Real4DT::New();
    out->SetRegions(timeSizeOut);
    out->Allocate();

    //Create the 1-D filters
    FFT1DT::Pointer fft = FFT1DT::New();
    fft->SetInput(cast->GetOutput());
    
    ModT::Pointer modulus = ModT::New();
    modulus->SetInput(fft->GetOutput());
    PhasT::Pointer phase = PhasT::New();
    phase->SetInput(fft->GetOutput());

    for(index[0] = 0 ; index[0] < (int)inSize[0] ; index[0]++) {
        for(index[1] = 0 ; index[1] < (int)inSize[1] ; index[1]++) {
            for(index[2] = 0 ; index[2] < (int)inSize[2] ; index[2]++) {
                regionSel.SetIndex(index);
                regionSel.SetSize(timeSizeIn);
                copyTimeLine(inimg, working, index);
                
                modulus->Update();
                phase->Update();

                copyTimeLine(modulus->GetOutput(), out, index);
            }
        }
    }

    return out;
};

/* 
 * Calculates the Variance of the measurements over the time dimension 
 * same as the fslmaths tool of the same name
 */
Image3DType::Pointer Tvar(const Image4DType::Pointer fmri_img)
{
    Image3DType::Pointer mean = Tmean(fmri_img);
    itk::ImageLinearConstIteratorWithIndex<Image4DType> iter(
                fmri_img, fmri_img->GetRequestedRegion());
    iter.SetDirection(3);
    iter.GoToBegin();
    Image4DType::IndexType index4;
    Image3DType::Pointer out = Image3DType::New();
    Image4DType::SizeType size4 = fmri_img->GetRequestedRegion().GetSize();
    {
        Image3DType::SizeType size3 = {{size4[0], size4[1], size4[2]}};
        out->SetRegions(size3);
        out->Allocate();
    }

    while(!iter.IsAtEnd()) {
        index4 = iter.GetIndex();
        Image3DType::IndexType index3 = {{index4[0], index4[1], index4[2]}};
        double average = 0;
        while(!iter.IsAtEndOfLine()) {
            average += pow(iter.Get()-mean->GetPixel(index3),2);
            ++iter;
        }
        out->SetPixel(index3, average/size4[3]);
        iter.NextLine();
    }
    return out;
}

/* 
 * Calculates the mean of the measurements over the time dimension 
 * same as the fslmaths tool of the same name
 */
Image3DType::Pointer Tmean(const Image4DType::Pointer fmri_img)
{
    itk::ImageLinearConstIteratorWithIndex<Image4DType> iter(
                fmri_img, fmri_img->GetRequestedRegion());
    iter.SetDirection(3);
    iter.GoToBegin();
    Image4DType::IndexType index4;
    Image3DType::Pointer out = Image3DType::New();
    Image4DType::SizeType size4 = fmri_img->GetRequestedRegion().GetSize();
    {
        Image3DType::SizeType size3 = {{size4[0], size4[1], size4[2]}};
        out->SetRegions(size3);
        out->Allocate();
    }

    while(!iter.IsAtEnd()) {
        index4 = iter.GetIndex();
        double average = 0;
        while(!iter.IsAtEndOfLine()) {
            average += iter.Get();
            ++iter;
        }
        index4 = iter.GetIndex();
        Image3DType::IndexType index3 = {{index4[0], index4[1], index4[2]}};
        out->SetPixel(index3, average/size4[3]);
        iter.NextLine();
    }
    return out;
}

/* Takes a 3D image and copies every voxel for len elements in the 4th dimension */
Image4DType::Pointer extrude(const Image3DType::Pointer input, unsigned int len)
{
    itk::ImageLinearConstIteratorWithIndex<Image3DType> iter(
                input, input->GetRequestedRegion());
    iter.SetDirection(0);
    iter.GoToBegin();
    Image3DType::SizeType size3 = input->GetRequestedRegion().GetSize();

    Image4DType::Pointer out = Image4DType::New();
    {
        Image4DType::SizeType size4 = {{size3[0], size3[1], size3[2], len}};
        out->SetRegions(size4);
        out->Allocate();
    }

    while(!iter.IsAtEnd()) {
        while(!iter.IsAtEndOfLine()) {
            Image3DType::IndexType index3 = iter.GetIndex();
            for(unsigned int i = 0 ; i < len ; i++) {
                Image4DType::IndexType index4 = {{index3[0], index3[1], index3[2], i}};
                out->SetPixel(index4, iter.Get());
            }
            ++iter;
        }
        iter.NextLine();
    }
    return out;
}

//RMS for a non-zero mean signal is 
//sqrt(mu^2+sigma^2)
Image3DType::Pointer get_rms(Image4DType::Pointer in)
{ 
    Image3DType::Pointer out = Image3DType::New();
    Image3DType::SizeType outsize;
    Image3DType::DirectionType outdir;
    Image3DType::PointType outorigin;
    Image3DType::SpacingType outspacing;
    for(int i = 0 ; i < 3 ; i++) {
        outsize[i] = in->GetRequestedRegion().GetSize()[i];
        outorigin[i] = in->GetOrigin()[i];
        outspacing[i] = in->GetSpacing()[i];
        
        for(int j = 0 ; j < 3 ; j++) 
            outdir(i, j) = in->GetDirection()(i,j);
    }
    out->SetRegions(outsize);
    out->SetDirection(outdir);
    out->SetOrigin(outorigin);
    out->SetSpacing(outspacing);
    out->Allocate();
    
    for(size_t xx = 0 ; xx < in->GetRequestedRegion().GetSize()[0] ; xx++) {
        for(size_t yy = 0 ; yy < in->GetRequestedRegion().GetSize()[1] ; yy++) {
            for(size_t zz = 0 ; zz < in->GetRequestedRegion().GetSize()[2] ; zz++) {
                double mean = 0;
                double var = 0;
                
                //calc mean
                for(size_t tt = 0 ; tt < in->GetRequestedRegion().GetSize()[3] ; tt++) {
                    Image4DType::IndexType index = {{xx, yy, zz, tt}};
                    mean += in->GetPixel(index);
                }
                mean /= in->GetRequestedRegion().GetSize()[3];
                
                //calc cov
                for(size_t tt = 0 ; tt < in->GetRequestedRegion().GetSize()[3] ; tt++) {
                    Image4DType::IndexType index = {{xx, yy, zz, tt}};
                    var += pow(in->GetPixel(index)-mean, 2);
                }
                var /= in->GetRequestedRegion().GetSize()[3];
                
                //set output pixel as rms
                {
                    Image3DType::IndexType index = {{xx, yy, zz}};
                    out->SetPixel(index, sqrt(mean*mean + var));
                }
            }
        }
    }

    return out;
}

/* Calculates the median absolute deviation in the 4th dimension, similar to Tvar above */
Image3DType::Pointer median_absolute_deviation(const Image4DType::Pointer fmri_img)
{
    Image4DType::SizeType size4 = fmri_img->GetRequestedRegion().GetSize();
    Image3DType::Pointer output = Image3DType::New();
    Image3DType::SizeType size3;
    Image3DType::IndexType index3;
    for(uint32_t i = 0 ; i < 3 ; i++)
        size3[i] = size4[i];
    output->SetRegions(size3);
    output->Allocate();

    /* Go to index and start at time 0 at that voxel*/
    itk::ImageLinearConstIteratorWithIndex< Image4DType > 
                fmri_it(fmri_img, fmri_img->GetRequestedRegion());
    fmri_it.SetDirection(3);
    
    double median;
    std::vector<double> points(fmri_img->GetRequestedRegion().GetSize()[3]);
    std::vector<double>::iterator vit;
    while(!fmri_it.IsAtEnd()) {
        vit = points.begin();
        //generate a vector of point lists, get median
        while(!fmri_it.IsAtEndOfLine()) {
            *vit = fmri_it.Get();
            vit++; ++fmri_it;
        }
        sort(points.begin(), points.end());
        if(points.size()%2 == 0)
            median = (points[points.size()/2] + points[(points.size()+1)/2])/2.;
        else
            median = points[points.size()/2];

        //calculate the absolute deviations, median
        vit = points.begin();
        while(vit != points.end()) {
            *vit = fabs(*vit - median);
            vit++;
        }
        sort(points.begin(), points.end());
        if(points.size()%2 == 0)
            median = .7413*(points[points.size()/2] + points[(points.size()+1)/2]);
        else
            median = 1.4826*points[points.size()/2];
        
        for(uint32_t i = 0 ; i < 3; i++) {
            index3[i] = fmri_it.GetIndex()[i];
        }
        output->SetPixel(index3, median);
        fmri_it.NextLine();
    }
    copyInformation<Image4DType, Image3DType>(fmri_img, output);
    return output;
}

