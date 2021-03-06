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

#ifndef __rtkCudaWarpForwardProjectionImageFilter_hxx
#define __rtkCudaWarpForwardProjectionImageFilter_hxx

#include "rtkCudaWarpForwardProjectionImageFilter.h"
#include "rtkCudaUtilities.hcu"
#include "rtkCudaWarpForwardProjectionImageFilter.hcu"
#include "rtkHomogeneousMatrix.h"

#include <itkImageRegionConstIterator.h>
#include <itkImageRegionIteratorWithIndex.h>
#include <itkLinearInterpolateImageFunction.h>
#include <itkMacro.h>
#include <itkImageRegionIterator.h>
#include "rtkMacro.h"
#include "itkCudaUtil.h"

namespace rtk
{

CudaWarpForwardProjectionImageFilter
::CudaWarpForwardProjectionImageFilter()
{
  this->SetNumberOfRequiredInputs(2);
}

void
CudaWarpForwardProjectionImageFilter
::SetInputProjectionStack(const InputImageType* ProjectionStack)
{
  this->SetInput(0, const_cast<InputImageType*>(ProjectionStack));
}

void
CudaWarpForwardProjectionImageFilter
::SetInputVolume(const InputImageType* Volume)
{
  this->SetInput(1, const_cast<InputImageType*>(Volume));
}

void
CudaWarpForwardProjectionImageFilter
::SetDisplacementField(const DVFType* MVF)
{
  this->SetInput("DisplacementField", const_cast<DVFType*>(MVF));
}

CudaWarpForwardProjectionImageFilter::InputImageType::Pointer
CudaWarpForwardProjectionImageFilter
::GetInputProjectionStack()
{
  return static_cast< InputImageType * >
          ( this->itk::ProcessObject::GetInput(0) );
}

CudaWarpForwardProjectionImageFilter::InputImageType::Pointer
CudaWarpForwardProjectionImageFilter
::GetInputVolume()
{
  return static_cast< InputImageType * >
          ( this->itk::ProcessObject::GetInput(1) );
}

CudaWarpForwardProjectionImageFilter::DVFType::Pointer
CudaWarpForwardProjectionImageFilter
::GetDisplacementField()
{
  return static_cast< DVFType * >
          ( this->itk::ProcessObject::GetInput("DisplacementField") );
}

void
CudaWarpForwardProjectionImageFilter
::GenerateInputRequestedRegion()
{
  Superclass::GenerateInputRequestedRegion();
  
  // Since we do not know where the DVF points, the 
  // whole input volume is required. 
  // However, the volume's requested region 
  // computed by Superclass::GenerateInputRequestedRegion()
  // is the requested region for the DVF
  this->GetInputProjectionStack()->SetRequestedRegion(this->GetOutput()->GetRequestedRegion());
  this->GetDisplacementField()->SetRequestedRegion(this->GetInputVolume()->GetRequestedRegion());
  this->GetInputVolume()->SetRequestedRegionToLargestPossibleRegion();
  
  // The requested region for the input projection 
  // shoudl be set correctly by 
  // Superclass::GenerateInputRequestedRegion()
}


void
CudaWarpForwardProjectionImageFilter
::GPUGenerateData()
{
  itk::Matrix<double, 4, 4> matrixIdxInputVol;
  itk::Matrix<double, 4, 4> indexInputToPPInputMatrix;
  itk::Matrix<double, 4, 4> indexInputToIndexDVFMatrix;
  itk::Matrix<double, 4, 4> PPInputToIndexInputMatrix;  
  matrixIdxInputVol.SetIdentity();
  for(unsigned int i=0; i<3; i++)
    {
    matrixIdxInputVol[i][3] = this->GetInputVolume()->GetBufferedRegion().GetIndex()[i]; // Should 0.5 be added here ?
    }
  
  const Superclass::GeometryType::Pointer geometry = this->GetGeometry();
  const unsigned int Dimension = InputImageType::ImageDimension;
  const unsigned int iFirstProj = this->GetInputProjectionStack()->GetRequestedRegion().GetIndex(Dimension-1);
  const unsigned int nProj = this->GetInputProjectionStack()->GetRequestedRegion().GetSize(Dimension-1);
  const unsigned int nPixelsPerProj = this->GetOutput()->GetBufferedRegion().GetSize(0) *
    this->GetOutput()->GetBufferedRegion().GetSize(1);

  itk::Vector<double, 4> source_position;

  // Setting BoxMin and BoxMax
  // SR: we are using cuda textures where the pixel definition is not center but corner.
  // Therefore, we set the box limits from index to index+size instead of, for ITK,
  // index-0.5 to index+size-0.5.
  float boxMin[3];
  float boxMax[3];
  for(unsigned int i=0; i<3; i++)
    {
    boxMin[i] = this->GetInputVolume()->GetBufferedRegion().GetIndex()[i]+0.5;
    boxMax[i] = boxMin[i] + this->GetInputVolume()->GetBufferedRegion().GetSize()[i]-1.0;
    }

  // Getting Spacing
  float spacing[3];
  for(unsigned int i=0; i<3; i++)
    {
    spacing[i] = this->GetInputVolume()->GetSpacing()[i];
    }

  // Cuda convenient format for dimensions
  int projectionSize[2];
  projectionSize[0] = this->GetOutput()->GetBufferedRegion().GetSize()[0];
  projectionSize[1] = this->GetOutput()->GetBufferedRegion().GetSize()[1];

  int volumeSize[3];
  volumeSize[0] = this->GetInputVolume()->GetBufferedRegion().GetSize()[0];
  volumeSize[1] = this->GetInputVolume()->GetBufferedRegion().GetSize()[1];
  volumeSize[2] = this->GetInputVolume()->GetBufferedRegion().GetSize()[2];
  
  int inputDVFSize[3];
  inputDVFSize[0] = this->GetDisplacementField()->GetBufferedRegion().GetSize()[0];
  inputDVFSize[1] = this->GetDisplacementField()->GetBufferedRegion().GetSize()[1];
  inputDVFSize[2] = this->GetDisplacementField()->GetBufferedRegion().GetSize()[2];
  
  // Split the DVF into three images (one per component)
  InputImageType::Pointer xCompDVF = InputImageType::New();
  InputImageType::Pointer yCompDVF = InputImageType::New();
  InputImageType::Pointer zCompDVF = InputImageType::New();
  InputImageType::RegionType largest = this->GetDisplacementField()->GetLargestPossibleRegion();
  xCompDVF->SetRegions(largest);
  yCompDVF->SetRegions(largest);
  zCompDVF->SetRegions(largest);
  xCompDVF->Allocate();
  yCompDVF->Allocate();
  zCompDVF->Allocate();
  itk::ImageRegionIterator<InputImageType>      itxComp(xCompDVF, largest);
  itk::ImageRegionIterator<InputImageType>      ityComp(yCompDVF, largest);
  itk::ImageRegionIterator<InputImageType>      itzComp(zCompDVF, largest);
  itk::ImageRegionConstIterator<DVFType>   itDVF(this->GetDisplacementField(), largest);
  while(!itDVF.IsAtEnd())
    {
      itxComp.Set(itDVF.Get()[0]);
      ityComp.Set(itDVF.Get()[1]);
      itzComp.Set(itDVF.Get()[2]);
      ++itxComp;
      ++ityComp;
      ++itzComp;
      ++itDVF;
    }

  float *pin = *(float**)( this->GetInputProjectionStack()->GetCudaDataManager()->GetGPUBufferPointer() );
  float *pout = *(float**)( this->GetOutput()->GetCudaDataManager()->GetGPUBufferPointer() );
  float *pvol = *(float**)( this->GetInputVolume()->GetCudaDataManager()->GetGPUBufferPointer() );
  float *pinxDVF = *(float**)( xCompDVF->GetCudaDataManager()->GetGPUBufferPointer() );
  float *pinyDVF = *(float**)( yCompDVF->GetCudaDataManager()->GetGPUBufferPointer() );
  float *pinzDVF = *(float**)( zCompDVF->GetCudaDataManager()->GetGPUBufferPointer() );

  // Transform matrices that we will need during the warping process
  indexInputToPPInputMatrix = rtk::GetIndexToPhysicalPointMatrix( this->GetInputVolume().GetPointer() ).GetVnlMatrix()
                              * matrixIdxInputVol.GetVnlMatrix();

  indexInputToIndexDVFMatrix = rtk::GetPhysicalPointToIndexMatrix( this->GetDisplacementField().GetPointer() ).GetVnlMatrix()
                              * rtk::GetIndexToPhysicalPointMatrix( this->GetInputVolume().GetPointer() ).GetVnlMatrix()
                              * matrixIdxInputVol.GetVnlMatrix();

  PPInputToIndexInputMatrix = rtk::GetPhysicalPointToIndexMatrix( this->GetInputVolume().GetPointer() ).GetVnlMatrix();

  // Convert the matrices to arrays of floats (skipping the last line, as we don't care)
  float fIndexInputToIndexDVFMatrix[12];
  float fPPInputToIndexInputMatrix[12];
  float fIndexInputToPPInputMatrix[12];
  for (int j = 0; j < 12; j++)
    {
    fIndexInputToIndexDVFMatrix[j] = (float) indexInputToIndexDVFMatrix[j/4][j%4];
    fPPInputToIndexInputMatrix[j] = (float) PPInputToIndexInputMatrix[j/4][j%4];
    fIndexInputToPPInputMatrix[j] = (float) indexInputToPPInputMatrix[j/4][j%4];
    }

  // Go over each projection
  for(unsigned int iProj = iFirstProj; iProj < iFirstProj + nProj; iProj++)
    {
    // Account for system rotations
    Superclass::GeometryType::ThreeDHomogeneousMatrixType volPPToIndex;
    volPPToIndex = GetPhysicalPointToIndexMatrix( this->GetInputVolume().GetPointer() );

    // Compute matrix to translate the pixel indices on the volume and the detector
    // if the Requested region has non-zero index
    Superclass::GeometryType::ThreeDHomogeneousMatrixType projIndexTranslation, volIndexTranslation;
    projIndexTranslation.SetIdentity();
    volIndexTranslation.SetIdentity();
    for(unsigned int i=0; i<3; i++)
      {
      projIndexTranslation[i][3] = this->GetOutput()->GetRequestedRegion().GetIndex(i);
      volIndexTranslation[i][3] = -this->GetInputVolume()->GetBufferedRegion().GetIndex(i);

//       if (m_UseCudaTexture)
//         {
        // Adding 0.5 offset to change from the centered pixel convention (ITK)
        // to the corner pixel convention (CUDA).
        volPPToIndex[i][3] += 0.5;
//         }
      }

    // Compute matrix to transform projection index to volume index
    Superclass::GeometryType::ThreeDHomogeneousMatrixType d_matrix;
    d_matrix =
      volIndexTranslation.GetVnlMatrix() *
      volPPToIndex.GetVnlMatrix() *
      geometry->GetProjectionCoordinatesToFixedSystemMatrix(iProj).GetVnlMatrix() *
      rtk::GetIndexToPhysicalPointMatrix( this->GetInput() ).GetVnlMatrix() *
      projIndexTranslation.GetVnlMatrix();
    float matrix[4][4];
    for (int j=0; j<4; j++)
      for (int k=0; k<4; k++)
        matrix[j][k] = (float)d_matrix[j][k];

    // Set source position in volume indices
    source_position = volPPToIndex * geometry->GetSourcePosition(iProj);

    int projectionOffset = iProj - this->GetOutput()->GetBufferedRegion().GetIndex(2);

    CUDA_warp_forward_project(projectionSize,
                        volumeSize,
                        inputDVFSize,
                        (float*)&(matrix[0][0]),
                        pin + nPixelsPerProj * projectionOffset,
                        pout + nPixelsPerProj * projectionOffset,
                        pvol,
                        1,
                        (double*)&(source_position[0]),
                        boxMin,
                        boxMax,
                        spacing,
                        pinxDVF,
                        pinyDVF,
                        pinzDVF,
                        fIndexInputToIndexDVFMatrix,
                        fPPInputToIndexInputMatrix,
                        fIndexInputToPPInputMatrix
                        );
    }
}

} // end namespace rtk

#endif
