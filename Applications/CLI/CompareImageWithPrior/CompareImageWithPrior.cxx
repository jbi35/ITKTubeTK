/*=========================================================================

Library:   TubeTK

Copyright 2010 Kitware Inc. 28 Corporate Drive,
Clifton Park, NY, 12065, USA.

All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

=========================================================================*/

#if defined(_MSC_VER)
#pragma warning ( disable : 4786 )
#endif

#ifdef __BORLANDC__
#define ITK_LEAN_AND_MEAN
#endif

// It is important to use OrientedImages
#include "itkOrientedImage.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkImageRegionConstIteratorWithIndex.h"
#include "itkAddImageFilter.h"
#include "itkSubtractImageFilter.h"
#include "itkSquareImageFilter.h"
#include "itkSqrtImageFilter.h"
#include "itkDivideByConstantImageFilter.h"
#include "itkMultiplyByConstantImageFilter.h"
#include "itkMinimumMaximumImageCalculator.h"
#include "itkRecursiveGaussianImageFilter.h"
#include "vnl/vnl_math.h"

// The following three should be used in every CLI application
#include "tubeCLIFilterWatcher.h"
#include "tubeCLIProgressReporter.h"
#include "itkTimeProbesCollectorBase.h"

// Local Includes for helper functions
#include "tubeSubImageGenerator.h"
#include "tubeJointHistogramGenerator.h"
#include "tubeZScoreCalculator.h"

// Must do a forward declaraction of DoIt before including
// tubeCLIHelperFunctions
template< class pixelT, unsigned int dimensionT >
int DoIt( int argc, char * argv[] );

// Must include CLP before including tubeCLIHleperFunctions
#include "CompareImageWithPriorCLP.h"

// Includes tube::ParseArgsAndCallDoIt function
#include "tubeCLIHelperFunctions.h"

// Your code should be within the DoIt function...
template< class pixelT, unsigned int dimensionT >
int DoIt( int argc, char * argv[] )
{
  PARSE_ARGS;

  // The timeCollector is used to perform basic profiling of the components
  //   of your algorithm.
  itk::TimeProbesCollectorBase timeCollector;

  // CLIProgressReporter is used to communicate progress with the Slicer GUI
  tube::CLIProgressReporter    progressReporter( "CompareImageWithPrior",
                                                 CLPProcessInformation );
  progressReporter.Start();

  // typedefs for inputs
  typedef float                                              PixelType;
  typedef itk::OrientedImage< PixelType,  dimensionT >       ImageType;
  typedef itk::ImageFileReader< ImageType >                  ReaderType;
  typedef itk::ImageFileWriter< ImageType  >                 WriterType;

  // typedefs for internal storage
  typedef std::vector<int>                                   VectorType;
  typedef typename tube::JointHistogramGenerator<PixelType,dimensionT>
    ::JointHistogramType                                     HistogramType;
  typedef bool                                               BoolPixelType;
  typedef itk::OrientedImage< BoolPixelType, dimensionT >    SelectionMaskType;
  typedef itk::ImageFileReader< HistogramType >              HistReaderType;
  typedef itk::ImageFileWriter< HistogramType>               HistWriterType;
  typedef itk::ImageFileReader< SelectionMaskType >          SelectionMaskReaderType;

  // typedefs for iterators
  typedef itk::ImageRegionConstIteratorWithIndex< ImageType> FullItrType;
  typedef itk::ImageRegionConstIterator<HistogramType>       HistIteratorType;
  typedef itk::ImageRegionIterator<SelectionMaskType>        SelectionMaskIteratorType;

  typedef itk::MinimumMaximumImageCalculator<ImageType>      CalculatorType;
  typedef itk::RecursiveGaussianImageFilter<HistogramType,HistogramType>
                                                             BlurType;

  // Setup the readers to load the input data (image + prior)
  timeCollector.Start( "Load data" );
  typename ReaderType::Pointer reader = ReaderType::New();
  typename ReaderType::Pointer priorReader = ReaderType::New();
  reader->SetFileName( inputVolume.c_str() );
  priorReader->SetFileName( priorVolume.c_str() );

  // Load the input image with exception handling
  try
    {
    reader->Update();
    }
  catch( itk::ExceptionObject & err )
    {
    std::cerr << "ExceptionObject caught while reading the input image!"
              << std::endl;
    std::cerr << err << std::endl;
    return EXIT_FAILURE;
    }

  // Load the input prior with exception handling
  try
    {
    priorReader->Update();
    }
  catch( itk::ExceptionObject & err )
    {
    std::cerr << "ExceptionObject caught while reading the input prior!"
              << std::endl;
    std::cerr << err << std::endl;
    return EXIT_FAILURE;
    }
  timeCollector.Stop( "Load data" );
  double progress = 0.1;
  progressReporter.Report( progress );

  // Store the input images and prepare the target regions
  typename ImageType::Pointer curImage = reader->GetOutput();
  typename ImageType::Pointer curPrior = priorReader->GetOutput();
  typename ImageType::RegionType region;
  typename ImageType::RegionType fullRegion;
  typename ImageType::SizeType size;
  typename ImageType::IndexType start;
  VectorType roiSize( dimensionT );

  // Setup the target regions
  size = curImage->GetLargestPossibleRegion().GetSize();
  start = curImage->GetLargestPossibleRegion().GetIndex();
  for( unsigned int i = 0; i < dimensionT; ++i )
    {
    size[i] -= regionRadius;
    start[i] += regionRadius / 2;
    roiSize[i] = regionRadius;
    }
  region.SetSize( size );
  region.SetIndex( start );

  // Allocate the output image and fill with 0s
  typename ImageType::Pointer outImage = ImageType::New();
  outImage->CopyInformation( curImage );
  outImage->SetRegions( curImage->GetLargestPossibleRegion() );
  outImage->Allocate();
  outImage->FillBuffer( 0 );

  // Allocate the selection mask
  typename SelectionMaskType::Pointer mask; 

  // Setup the full image iterator and the selection Mask iterator
  FullItrType imageItr( curImage, region );
  SelectionMaskIteratorType maskItr;
  typename HistogramType::PixelType samples = 0;
  
  if( selectionMask != std::string( "nil" ) )
    {

    typename SelectionMaskReaderType::Pointer maskReader = 
      SelectionMaskReaderType::New();
    maskReader->SetFileName( selectionMask );
      
    // Load the selection mask with exception handling
    try
      {
      maskReader->Update();
      }
    catch( itk::ExceptionObject & err )
      {
      std::cerr << "ExceptionObject caught while reading the selection mask!"
                << std::endl;
      std::cerr << err << std::endl;
      return EXIT_FAILURE;
      }

    // Place the data from the mask reader into the mask
    mask = maskReader->GetOutput();

    // Get the count from the read mask
    maskItr = SelectionMaskIteratorType( mask, region );
    maskItr.GoToBegin();
    while( !maskItr.IsAtEnd() )
      {
      if( maskItr.Get() )
        {
        samples++;
        }
      ++maskItr;
      }
    }
  else // Create a blank selection mask
    {
    mask = SelectionMaskType::New();
    mask->CopyInformation( curImage );
    mask->SetRegions( curImage->GetLargestPossibleRegion() );
    mask->Allocate();
    mask->FillBuffer( false );

    // The first iteration through the image is to get the number of samples 
    // that we will use in the other iterations. This allows for the 
    // super-granular progress reporting that we all know and love. We also 
    // use this to create the image of booleans used to clean up the future 
    // iterations a bit.
    maskItr = SelectionMaskIteratorType( mask, region );
    imageItr.GoToBegin();
    maskItr.GoToBegin();

    while( !imageItr.IsAtEnd() )
      {
      bool doCalculation = true;
      bool useThreshold;
      bool useSkip;
      if( threshold != 0 )
        {
        useThreshold = true;
        useSkip = false;
        }
      else if( skipSize != -1 )
        {
        useThreshold = false;
        useSkip = true;
        }
      else
        {
        useThreshold = false;
        useSkip = false;
        }
      
      if( useThreshold && imageItr.Get() > threshold )
        {
        doCalculation = false;
        }
      
      typename ImageType::IndexType curIndex;
      std::vector<int> roiCenter;
      if( doCalculation )
        {
        curIndex = imageItr.GetIndex();
        roiCenter = std::vector<int>( dimensionT );
        for( unsigned int i = 0; i < dimensionT; ++i )
          {
          if( useSkip && ( curIndex[i] - start[i] ) % skipSize != 0 )
            {
            doCalculation = false;
            break;
            }
          roiCenter[i] = curIndex[i];
          }
        }
      
      if( doCalculation )
        {
        ++samples;
        maskItr.Set( true );
        }
      ++imageItr;
      ++maskItr;
      }

    }
  
  // Caculate Image and Mask's Max and Min
  typename ImageType::PixelType imageMin;
  typename ImageType::PixelType imageMax;
  typename ImageType::PixelType maskMin;
  typename ImageType::PixelType maskMax;
  typename CalculatorType::Pointer calculator;
  calculator = CalculatorType::New();
  calculator->SetImage( curImage );
  calculator->Compute();
  imageMin = calculator->GetMinimum();
  imageMax = calculator->GetMaximum();
  calculator = CalculatorType::New();
  calculator->SetImage( curPrior );
  calculator->Compute();
  maskMin = calculator->GetMinimum();
  maskMax = calculator->GetMaximum();

  // Get some memory ready for our joint histograms
  typename HistogramType::Pointer hist;
  typename HistogramType::Pointer meanHist;
  typename HistogramType::Pointer stdevHist;

  // variable for managing the granularity of progress reporting. The loop uses
  // progress += proportion/samples to appropriately gauge progress.
  double proportion;

  // If we've specified mean and stdev inputs, load them and don't do the
  // computation.
  if( mean != std::string( "nil" ) && stdev != std::string( "nil" ) )
    {
    timeCollector.Start( "Load Mean and Stdev" );

    // Setup reader for mean histogram
    typename HistReaderType::Pointer histReader;
    histReader = HistReaderType::New();
    histReader->SetFileName( mean );

    // Read mean histogram with exception handling
    try
      {
      histReader->Update();
      }
    catch( itk::ExceptionObject & err )
      {
      std::cerr << "ExceptionObject caught while reading the mean histogram!"
                << std::endl;
      std::cerr << err << std::endl;
      return EXIT_FAILURE;
      }
    meanHist = histReader->GetOutput();

    // Setup reader for standard deviation histogram
    histReader = HistReaderType::New();
    histReader->SetFileName( stdev );

    // Read standard deviation histogram with exception handling
    try
      {
      histReader->Update();
      }
    catch( itk::ExceptionObject & err )
      {
      std::cerr << "ExceptionObject caught while reading the stdev histogram!"
                << std::endl;
      std::cerr << err << std::endl;
      return EXIT_FAILURE;
      }
    stdevHist = histReader->GetOutput();

    proportion = 0.75;

    timeCollector.Stop( "Load Mean and Stdev" );
    }
  // If no mean and stdev inputs are specified we'll calculate them
  else
    {
    // Calculate the mean and standard deviation histograms
    timeCollector.Start( "Get Mean and Stdev" );
    proportion = 0.40;
    double propFrac = proportion/3.0;
    
    tube::ZScoreCalculator<PixelType,dimensionT> zCalc;
    zCalc.SetInputVolume( curImage );
    zCalc.SetInputPrior( curPrior );
    zCalc.SetSelectionMask( mask );
    zCalc.SetNumberOfBins( histogramSize );
    zCalc.SetInputMin( imageMin );
    zCalc.SetInputMax( imageMax );
    zCalc.SetMaskMin( maskMin );
    zCalc.SetMaskMax( maskMax );
    zCalc.SetROISize( roiSize );
    zCalc.SetRegion( region );
    zCalc.SetMeanHistogram( meanHist );
    zCalc.SetStdevHistogram( stdevHist );
    zCalc.CalculateMeanAndStdev( progressReporter, progress, 
                                 propFrac, samples );
    progress += propFrac;
    zCalc.Update( progressReporter, progress, 
                  propFrac, samples );
    progress += propFrac;
    zCalc.CalculateRobustMeanAndStdev( progressReporter, 
                                       progress, 
                                       propFrac, samples, 
                                       robustPercentage,
                                       robustZScore );
    meanHist = zCalc.GetMeanHistogram();
    stdevHist = zCalc.GetStdevHistogram();

    timeCollector.Stop( "Get Mean and Stdev" );
    proportion += propFrac;
    proportion = 0.35;
    }

  // Write the mean and standard deviation histograms to disk
  timeCollector.Start( "Write Mean and Stdev" );

  // Setup the mean histogram writer
  typename HistWriterType::Pointer histWriter = HistWriterType::New();
  histWriter->SetFileName( meanOutput.c_str() );
  histWriter->SetInput( meanHist );

  // Write the mean histogram to disk, with exception handling
  try
    {
    histWriter->Update();
    }
  catch( itk::ExceptionObject & err )
    {
    std::cerr << "ExceptionObject caught while writing the mean histogram!"
              << std::endl;
    std::cerr << err << std::endl;
    }

  // Setup the standard deviation histogram writer
  typename HistWriterType::Pointer stdWriter = HistWriterType::New();
  stdWriter->SetFileName( stdevOutput.c_str() );
  stdWriter->SetInput( stdevHist );

  // Write the stdev histogram to disk, with exception handling
  try
    {
    stdWriter->Update();
    }
  catch( itk::ExceptionObject & err )
    {
    std::cerr << "ExceptionObject caught while writing the stdev histogram!"
              << std::endl;
    std::cerr << err << std::endl;
    }
  timeCollector.Stop( "Write Mean and Stdev" );

  // Blur the histograms
  typename BlurType::Pointer blur = BlurType::New();
  blur->SetInput( meanHist );
  blur->SetSigma( 1 );
  blur->Update();
  meanHist = blur->GetOutput();

  blur->SetInput( stdevHist );
  blur->SetSigma( 1 );
  blur->Update();
  stdevHist = blur->GetOutput();

  // Report progress after the mean and stdev write
  progress += 0.05;
  progressReporter.Report( progress );

  // Iterate through the image for a third and final time to calculate the Z
  // scores in the manner specified.
  timeCollector.Start( "Calculate Z Scores" );

  tube::ZScoreCalculator<PixelType,dimensionT> zCalc;
  zCalc.SetInputVolume( curImage );
  zCalc.SetInputPrior( curPrior );
  zCalc.SetSelectionMask( mask );
  zCalc.SetNumberOfBins( histogramSize );
  zCalc.SetInputMin( imageMin );
  zCalc.SetInputMax( imageMax );
  zCalc.SetMaskMin( maskMin );
  zCalc.SetMaskMax( maskMax );
  zCalc.SetROISize( roiSize );
  zCalc.SetRegion( region );
  zCalc.SetMeanHistogram( meanHist );
  zCalc.SetStdevHistogram( stdevHist );
  zCalc.Update( progressReporter, progress, proportion, samples );
  outImage = zCalc.GetOutputVolume();

  timeCollector.Stop( "Calculate Z Scores" );

  // Prepare to write the output image of scores to disk
  timeCollector.Start( "Save data" );
  typename WriterType::Pointer writer = WriterType::New();
  writer->SetFileName( outputVolume.c_str() );
  writer->SetInput( outImage );

  // Write the output image to disk with exception handling
  try
    {
    writer->Update();
    }
  catch( itk::ExceptionObject & err )
    {
    std::cerr << "Exception caught: " << err << std::endl;
    return EXIT_FAILURE;
    }
  timeCollector.Stop( "Save data" );
  progress = 1.0;
  progressReporter.Report( progress );

  // Report the time each process step took
  timeCollector.Report();

  // Return with a good value
  return EXIT_SUCCESS;
}

// Main
int main( int argc, char **argv )
{
  PARSE_ARGS;

  // You may need to update this line if, in the project's .xml CLI file,
  //   you change the variable name for the inputVolume.
  return tube::ParseArgsAndCallDoIt( inputVolume, argc, argv );
}
