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

#ifndef __rtkTotalVariationDenoisingBPDQImageFilter_h
#define __rtkTotalVariationDenoisingBPDQImageFilter_h

#include "rtkForwardDifferenceGradientImageFilter.h"
#include "rtkBackwardDifferenceDivergenceImageFilter.h"
#include "rtkMagnitudeThresholdImageFilter.h"

#include <itkCastImageFilter.h>
#include <itkSubtractImageFilter.h>
#include <itkMultiplyImageFilter.h>
#include <itkPeriodicBoundaryCondition.h>
#include <itkInPlaceImageFilter.h>

namespace rtk
{
/** \class TotalVariationDenoisingBPDQImageFilter
 * \brief Applies a total variation denoising, only along the dimensions specified, on an image.
 *
 * This filter finds the minimum of || f - f_0 ||_2^2 + gamma * TV(f)
 * using basis pursuit dequantization, where f is the current image, f_0 the
 * input image, and TV the total variation calculated with only the gradients
 * along the dimensions specified. This filter can be used, for example, to
 * perform 3D total variation denoising on a 4D dataset
 * (by calling SetDimensionsProcessed([true true true false]).
 * More information on the algorithm can be found at
 * http://wiki.epfl.ch/bpdq#download
 *
 * \dot
 * digraph TotalVariationDenoisingBPDQImageFilter
 * {
 *
 * subgraph clusterFirstIteration
 *   {
 *   label="First iteration"
 *
 *   FI_Input [label="Input"];
 *   FI_Input [shape=Mdiamond];
 *
 *   node [shape=box];
 *   FI_Multiply [ label="itk::MultiplyImageFilter (by beta)" URL="\ref itk::MultiplyImageFilter"];
 *   FI_Gradient [ label="rtk::ForwardDifferenceGradientImageFilter" URL="\ref rtk::ForwardDifferenceGradientImageFilter"];
 *   FI_MagnitudeThreshold [ label="rtk::MagnitudeThresholdImageFilter" URL="\ref rtk::MagnitudeThresholdImageFilter"];
 *   FI_OutOfMagnitudeTreshold [label="", fixedsize="false", width=0, height=0, shape=none];
 *
 *   FI_Input -> FI_Multiply;
 *   FI_Multiply -> FI_Gradient;
 *   FI_Gradient -> FI_MagnitudeThreshold;
 *   FI_MagnitudeThreshold -> FI_OutOfMagnitudeTreshold [style=dashed];
 *   }
 *
 * subgraph clusterAfterFirstIteration
 *   {
 *   label="After first iteration"
 *
 *   Input [label="Input"];
 *   Input [shape=Mdiamond];
 *   Output [label="Output"];
 *   Output [shape=Mdiamond];
 *
 *   node [shape=box];
 *   Divergence [ label="rtk::BackwardDifferenceDivergenceImageFilter" URL="\ref rtk::BackwardDifferenceDivergenceImageFilter"];
 *   Subtract [ label="itk::SubtractImageFilter" URL="\ref itk::SubtractImageFilter"];
 *   Multiply [ label="itk::MultiplyImageFilter (by beta)" URL="\ref itk::MultiplyImageFilter"];
 *   Gradient [ label="rtk::ForwardDifferenceGradientImageFilter" URL="\ref rtk::ForwardDifferenceGradientImageFilter"];
 *   SubtractGradient [ label="itk::SubtractImageFilter" URL="\ref itk::SubtractImageFilter"];
 *   MagnitudeThreshold [ label="rtk::MagnitudeThresholdImageFilter" URL="\ref rtk::MagnitudeThresholdImageFilter"];
 *   OutOfSubtract [label="", fixedsize="false", width=0, height=0, shape=none];
 *   OutOfMagnitudeTreshold [label="", fixedsize="false", width=0, height=0, shape=none];
 *   BeforeDivergence [label="", fixedsize="false", width=0, height=0, shape=none];
 *
 *   Input -> Subtract;
 *   Divergence -> Subtract;
 *   Subtract -> OutOfSubtract;
 *   OutOfSubtract -> Output;
 *   OutOfSubtract -> Multiply;
 *   Multiply -> Gradient;
 *   Gradient -> SubtractGradient;
 *   SubtractGradient -> MagnitudeThreshold;
 *   MagnitudeThreshold -> OutOfMagnitudeTreshold;
 *   OutOfMagnitudeTreshold -> BeforeDivergence [style=dashed, constraint=false];
 *   BeforeDivergence -> Divergence;
 *   BeforeDivergence -> SubtractGradient;
 *   }
 *
 * }
 * \enddot
 *
 * \author Cyril Mory
 *
 * \ingroup IntensityImageFilters
 */

template< typename TOutputImage, typename TGradientImage =
    itk::Image< itk::CovariantVector < typename TOutputImage::ValueType, TOutputImage::ImageDimension >, 
    TOutputImage::ImageDimension > >
class TotalVariationDenoisingBPDQImageFilter :
        public itk::InPlaceImageFilter< TOutputImage, TOutputImage >
{
public:

  /** Standard class typedefs. */
  typedef TotalVariationDenoisingBPDQImageFilter               Self;
  typedef itk::InPlaceImageFilter< TOutputImage, TOutputImage> Superclass;
  typedef itk::SmartPointer<Self>                              Pointer;
  typedef itk::SmartPointer<const Self>                        ConstPointer;

  /** Method for creation through the object factory. */
  itkNewMacro(Self)

  /** Run-time type information (and related methods). */
  itkTypeMacro(TotalVariationDenoisingBPDQImageFilter, ImageToImageFilter)

  /** Sub filter type definitions */
  typedef ForwardDifferenceGradientImageFilter
            <TOutputImage,
             typename TOutputImage::ValueType,
             typename TOutputImage::ValueType,
             TGradientImage>                                                      GradientFilterType;
  typedef itk::MultiplyImageFilter<TOutputImage>                                  MultiplyFilterType;
  typedef itk::SubtractImageFilter<TOutputImage>                                  SubtractImageFilterType;
  typedef itk::SubtractImageFilter<TGradientImage>                                SubtractGradientFilterType;
  typedef MagnitudeThresholdImageFilter<TGradientImage>                           MagnitudeThresholdFilterType;
  typedef BackwardDifferenceDivergenceImageFilter<TGradientImage, TOutputImage>   DivergenceFilterType;

  itkGetMacro(NumberOfIterations, int)
  itkSetMacro(NumberOfIterations, int)

  itkSetMacro(Gamma, double)
  itkGetMacro(Gamma, double)

  void SetDimensionsProcessed(bool* arg);

  /** In some cases, regularization must use periodic boundary condition */
  void SetBoundaryConditionToPeriodic();

protected:
  TotalVariationDenoisingBPDQImageFilter();
  ~TotalVariationDenoisingBPDQImageFilter(){}

  virtual void GenerateData();

  virtual void GenerateOutputInformation();

  /** Sub filter pointers */
  typename GradientFilterType::Pointer             m_GradientFilter;
  typename MultiplyFilterType::Pointer             m_MultiplyFilter;
  typename SubtractImageFilterType::Pointer        m_SubtractFilter;
  typename SubtractGradientFilterType::Pointer     m_SubtractGradientFilter;
  typename MagnitudeThresholdFilterType::Pointer   m_MagnitudeThresholdFilter;
  typename DivergenceFilterType::Pointer           m_DivergenceFilter;

  double m_Gamma;
  double m_Beta;
  int    m_NumberOfIterations;
  bool   m_DimensionsProcessed[TOutputImage::ImageDimension];

private:
  TotalVariationDenoisingBPDQImageFilter(const Self&); //purposely not implemented
  void operator=(const Self&); //purposely not implemented

  virtual void SetPipelineForFirstIteration();
  virtual void SetPipelineAfterFirstIteration();
};

} // end namespace itk

#ifndef ITK_MANUAL_INSTANTIATION
#include "rtkTotalVariationDenoisingBPDQImageFilter.hxx"
#endif

#endif //__rtkTotalVariationDenoisingBPDQImageFilter__
