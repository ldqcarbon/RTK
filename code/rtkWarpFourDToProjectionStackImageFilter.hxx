/*=========================================================================
 *
 *  Copyright RTK Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef __rtkWarpFourDToProjectionStackImageFilter_hxx
#define __rtkWarpFourDToProjectionStackImageFilter_hxx

#include "rtkWarpFourDToProjectionStackImageFilter.h"

namespace rtk
{

template< typename VolumeSeriesType, typename ProjectionStackType, typename TMVFImageSequence, typename TMVFImage>
WarpFourDToProjectionStackImageFilter< VolumeSeriesType, ProjectionStackType, TMVFImageSequence, TMVFImage>::WarpFourDToProjectionStackImageFilter()
{
  this->SetNumberOfRequiredInputs(3);
  m_MVFInterpolatorFilter = MVFInterpolatorType::New();
}

template< typename VolumeSeriesType, typename ProjectionStackType, typename TMVFImageSequence, typename TMVFImage>
void
WarpFourDToProjectionStackImageFilter< VolumeSeriesType, ProjectionStackType, TMVFImageSequence, TMVFImage>::SetDisplacementField(const TMVFImageSequence* DisplacementField)
{
  this->SetNthInput(2, const_cast<TMVFImageSequence*>(DisplacementField));
}

template< typename VolumeSeriesType, typename ProjectionStackType, typename TMVFImageSequence, typename TMVFImage>
typename TMVFImageSequence::ConstPointer
WarpFourDToProjectionStackImageFilter< VolumeSeriesType, ProjectionStackType, TMVFImageSequence, TMVFImage>::GetDisplacementField()
{
  return static_cast< const TMVFImageSequence * >
          ( this->itk::ProcessObject::GetInput(2) );
}

#ifdef RTK_USE_CUDA
template< typename VolumeSeriesType, typename ProjectionStackType, typename TMVFImageSequence, typename TMVFImage>
CudaWarpForwardProjectionImageFilter *
WarpFourDToProjectionStackImageFilter<VolumeSeriesType, ProjectionStackType, TMVFImageSequence, TMVFImage>::GetForwardProjectionFilter()
{
  return(m_ForwardProjectionFilter.GetPointer());
}
#endif

template< typename VolumeSeriesType, typename ProjectionStackType, typename TMVFImageSequence, typename TMVFImage>
void
WarpFourDToProjectionStackImageFilter< VolumeSeriesType, ProjectionStackType, TMVFImageSequence, TMVFImage>
::SetSignalFilename(const std::string _arg)
{
  itkDebugMacro("setting SignalFilename to " << _arg);
  if ( this->m_SignalFilename != _arg )
    {
    this->m_SignalFilename = _arg;
    this->Modified();

    std::ifstream is( _arg.c_str() );
    if( !is.is_open() )
      {
      itkGenericExceptionMacro(<< "Could not open signal file " << m_SignalFilename);
      }

    double value;
    while( !is.eof() )
      {
      is >> value;
      m_Signal.push_back(value);
      }
    }
  m_MVFInterpolatorFilter->SetSignalVector(m_Signal);
}

template< typename VolumeSeriesType, typename ProjectionStackType, typename TMVFImageSequence, typename TMVFImage>
void
WarpFourDToProjectionStackImageFilter< VolumeSeriesType, ProjectionStackType, TMVFImageSequence, TMVFImage>
::GenerateOutputInformation()
{
  m_MVFInterpolatorFilter->SetInput(this->GetDisplacementField());
  m_MVFInterpolatorFilter->SetFrame(0);

#ifdef RTK_USE_CUDA
  this->m_ForwardProjectionFilter = rtk::CudaWarpForwardProjectionImageFilter::New();
  GetForwardProjectionFilter()->SetDisplacementField(m_MVFInterpolatorFilter->GetOutput());
#else
  this->m_ForwardProjectionFilter = rtk::JosephForwardProjectionImageFilter<ProjectionStackType, ProjectionStackType>::New();
  itkWarningMacro("The warp Forward project image filter exists only in CUDA. Ignoring the displacement vector field and using CPU Joseph forward projection")
#endif

  Superclass::GenerateOutputInformation();
}

template< typename VolumeSeriesType, typename ProjectionStackType, typename TMVFImageSequence, typename TMVFImage>
void
WarpFourDToProjectionStackImageFilter< VolumeSeriesType, ProjectionStackType, TMVFImageSequence, TMVFImage>
::GenerateInputRequestedRegion()
{
  // Input 0 is the stack of projections we update
  typename ProjectionStackType::Pointer  inputPtr0 = const_cast< ProjectionStackType * >( this->GetInput(0) );
  if ( !inputPtr0 )
    {
    return;
    }
  inputPtr0->SetRequestedRegion( this->GetOutput()->GetRequestedRegion() );

  // Input 1 is the volume series
  typename VolumeSeriesType::Pointer inputPtr1 = static_cast< VolumeSeriesType * >
            ( this->itk::ProcessObject::GetInput(1) );
  inputPtr1->SetRequestedRegionToLargestPossibleRegion();

  // Input 2 is the sequence of DVFs
  typename TMVFImageSequence::Pointer inputPtr2 = static_cast< TMVFImageSequence * >
            ( this->itk::ProcessObject::GetInput(2) );
  inputPtr2->SetRequestedRegionToLargestPossibleRegion();
}

template< typename VolumeSeriesType, typename ProjectionStackType, typename TMVFImageSequence, typename TMVFImage>
void
WarpFourDToProjectionStackImageFilter< VolumeSeriesType, ProjectionStackType, TMVFImageSequence, TMVFImage>
::GenerateData()
{
  int ProjectionStackDimension = ProjectionStackType::ImageDimension;

  int NumberProjs = this->GetInputProjectionStack()->GetRequestedRegion().GetSize(ProjectionStackDimension-1);
  int FirstProj = this->GetInputProjectionStack()->GetRequestedRegion().GetIndex(ProjectionStackDimension-1);
  for(this->m_ProjectionNumber=FirstProj; this->m_ProjectionNumber<FirstProj+NumberProjs; this->m_ProjectionNumber++)
    {

    // After the first update, we need to use the output as input.
    if(this->m_ProjectionNumber>FirstProj)
      {
      typename ProjectionStackType::Pointer pimg = this->m_PasteFilter->GetOutput();
      pimg->DisconnectPipeline();
      this->m_PasteFilter->SetDestinationImage( pimg );
      }

    // Update the paste region
    this->m_PasteRegion.SetIndex(ProjectionStackDimension-1, this->m_ProjectionNumber);

    // Set the projection stack source
    this->m_ConstantProjectionStackSource->SetIndex(this->m_PasteRegion.GetIndex());

    // Set the Paste Filter. Since its output has been disconnected
    // we need to set its RequestedRegion manually (it will never
    // be updated by a downstream filter)
    this->m_PasteFilter->SetSourceRegion(this->m_PasteRegion);
    this->m_PasteFilter->SetDestinationIndex(this->m_PasteRegion.GetIndex());
    this->m_PasteFilter->GetOutput()->SetRequestedRegion(this->m_PasteFilter->GetDestinationImage()->GetLargestPossibleRegion());

    // Set the Interpolation filter
    this->m_InterpolationFilter->SetProjectionNumber(this->m_ProjectionNumber);

    // Set the MVF interpolator
    m_MVFInterpolatorFilter->SetFrame(this->m_ProjectionNumber);

    // Update the last filter
    this->m_PasteFilter->Update();
    }

  // Graft its output
  this->GraftOutput( this->m_PasteFilter->GetOutput() );

  // Release the data in internal filters
  this->m_MVFInterpolatorFilter->GetOutput()->ReleaseData();
}

}// end namespace


#endif
