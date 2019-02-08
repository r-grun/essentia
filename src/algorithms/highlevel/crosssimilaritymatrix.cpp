/*
 * Copyright (C) 2006-2016  Music Technology Group - Universitat Pompeu Fabra
 *
 * This file is part of Essentia
 *
 * Essentia is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation (FSF), either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the Affero GNU General Public License
 * version 3 along with this program.  If not, see http://www.gnu.org/licenses/
 */
#include "crosssimilaritymatrix.h"
#include "essentia/utils/tnt/tnt2vector.h"
#include "essentiamath.h"
#include <vector>
#include <iostream>
#include <string>
#include <algorithm>
#include <functional>

using namespace essentia;

std::vector<Real> globalAverageChroma(std::vector<std::vector<Real> >& inputFeature);
int optimalTranspositionIndex(std::vector<std::vector<Real> >& chromaA, std::vector<std::vector<Real> >& chromaB, int nshifts);
std::vector<std::vector<Real> > toTimeEmbedding(std::vector<std::vector<Real> >& inputArray, int m, int tau);
std::vector<std::vector<Real> > chromaBinarySimMatrix(std::vector<std::vector<Real> >& chromaA, std::vector<std::vector<Real> >& chromaB, int nshifts, Real matchCoef, Real mismatchCoef);


namespace essentia {
namespace standard {

const char* CrossSimilarityMatrix::name = "CrossSimilarityMatrix";
const char* CrossSimilarityMatrix::category = "Music Similarity";
const char* CrossSimilarityMatrix::description = DOC("This algorithm computes a binary cross similarity matrix from two chromagam feature vectors of a query and reference song.\n\n"
"Use HPCP algorithm for computing the chromagram and the default parameters of this algorithm for best results.\n\n"
"In addition, the algorithm also provides an option to use another binary similarity computation method using optimal transposition index (OTI) of chroma features as mentioned in [3].\n\n"
"Use default parameter values for best results.\n\n"
"The input chromagram should be in the shape (x, numbins), where 'x' is number of frames and 'numbins' stands for number of bins in the chromagram. An exception isthrown otherwise.\n\n"
"An exception is also thrown if either one of the input audio feature arrays are empty or if the cross similarity matrix is empty.\n\n"
"References:\n"
"[1] Serra, J., Gómez, E., & Herrera, P. (2008). Transposing chroma representations to a common key, IEEE Conference on The Use of Symbols to Represent Music and Multimedia Objects.\n\n"
"[2] Serra, J., Serra, X., & Andrzejak, R. G. (2009). Cross recurrence quantification for cover song identification.New Journal of Physics.\n\n"
"[3] Serra, Joan, et al. Chroma binary similarity and local alignment applied to cover song identification. IEEE Transactions on Audio, Speech, and Language Processing 16.6 (2008).\n");


void CrossSimilarityMatrix::configure() {
  // configure parameters
  _tau = parameter("tau").toInt();
  _embedDimension = parameter("embedDimension").toInt();
  _kappa = parameter("kappa").toReal();
  _noti = parameter("noti").toInt();
  _oti = parameter("oti").toBool();
  _toBlocked = parameter("toBlocked").toBool();
  _otiBinary = parameter("otiBinary").toBool();
  _optimiseThreshold = parameter("optimiseThreshold").toBool();
  _mathcCoef = 1; // for chroma binary sim-matrix based on OTI similarity as in [3]. 
  _mismatchCoef = 0; // for chroma binary sim-matrix based on OTI similarity as in [3]. 
}

void CrossSimilarityMatrix::compute() {
  // get inputs and output
  std::vector<std::vector<Real> > queryFeature = _queryFeature.get();
  std::vector<std::vector<Real> > referenceFeature = _referenceFeature.get();
  std::vector<std::vector<Real> >& csm = _csm.get();

  if (queryFeature.empty())
    throw EssentiaException("CrossSimilarityMatrix: input queryFeature array is empty.");

  if (referenceFeature.empty())
    throw EssentiaException("CrossSimilarityMatrix: input referenceFeature array is empty.");

  // check whether to use oti-based binary similarity 
  if (_otiBinary == true) {
    // check whether to stack the chroma features
    if (_toBlocked == true) {
      std::vector<std::vector<Real> >  timeEmbedA = toTimeEmbedding(queryFeature, _embedDimension, _tau);
      std::vector<std::vector<Real> >  timeEmbedB = toTimeEmbedding(referenceFeature, _embedDimension, _tau);
      csm = chromaBinarySimMatrix(queryFeature, referenceFeature, _noti, _mathcCoef, _mismatchCoef);;
    }
    else {
      csm = chromaBinarySimMatrix(queryFeature, referenceFeature, _noti, _mathcCoef, _mismatchCoef);
    }
  }
  // Use default cross similarity computation method based on euclidean distances
  else {

    std::vector<std::vector<Real> > pdistances;
    // check whether to transpose by oti
    if (_oti == true) {
      int otiIdx = optimalTranspositionIndex(queryFeature, referenceFeature, _noti);
      std::rotate(referenceFeature.begin(), referenceFeature.end() - otiIdx, referenceFeature.end());
    }

    // construct time embedding from input chroma features
    std::vector<std::vector<Real> >  timeEmbedA = toTimeEmbedding(queryFeature, _embedDimension, _tau);
    std::vector<std::vector<Real> >  timeEmbedB = toTimeEmbedding(referenceFeature, _embedDimension, _tau);

    // pairwise euclidean distance
      pdistances = pairwiseDistance(timeEmbedA, timeEmbedB);
    if (pdistances.empty())
      throw EssentiaException("CrossSimilarityMatrix: empty array found inside euclidean cross similarity matrix.");

    // transposing the array of pairwsie distance
    std::vector<std::vector<Real> > tpDistances = transpose(pdistances);

    size_t xRows = pdistances.size();
    size_t xCols = pdistances[0].size();
    size_t yRows = tpDistances.size();
    size_t yCols = tpDistances[0].size();

    std::vector<std::vector<Real> > similarityX;
    if (_optimiseThreshold == true) {
      // optimise the threshold computation on axis X by iniatilizing it to a matrix of ones
      similarityX.assign(xRows, std::vector<Real>(xCols, 1));
    }
    else if (_optimiseThreshold == false) {
      similarityX.assign(xRows, std::vector<Real>(xCols, 0));
      // construct thresholded similarity matrix on axis X
      for (size_t k=0; k<xRows; k++) {
        for (size_t l=0; l<xCols; l++) {
          similarityX[k][l] = percentile(pdistances[k], _kappa*100) - pdistances[k][l];
        }
      }
      // binarise the array with heavisideStepFunction
      heavisideStepFunction(similarityX);
    }
    // should not arrive here
    else throw EssentiaException("CrossSimilarityMatrix: Invalid type for parameter 'optimiseThreshold', expects Boolean type");

    std::vector<std::vector<Real> > similarityY(yRows, std::vector<Real>(yCols, 0));

    // construct thresholded similarity matrix on axis Y
    for (size_t u=0; u<yRows; u++) {
      for (size_t v=0; v<yCols; v++) {
        similarityY[u][v] = percentile(tpDistances[u], _kappa*100) - tpDistances[u][v];
      }
    }

    // here we binarise and transpose the similarityY array same time in order to avoid redundant looping
    std::vector<std::vector<Real> > tSimilarityY(yCols, std::vector<Real>(yRows));
    for (size_t i=0; i<yRows; i++) {
      for (size_t j=0; j<yCols; j++) {
        if (similarityY[i][j] < 0) {
          tSimilarityY[j][i] = 0;
        }
        else if (similarityY[i][j] >= 0) {
          tSimilarityY[j][i] = 1;
        }
      }
    }
    // finally we construct out cross similarity matrix by multiplying similarityX and similarityY
    // [TODO]: replace TNT array matmult with Boost matrix in future for faster computation.
    TNT::Array2D<Real> simX = vecvecToArray2D(similarityX);
    TNT::Array2D<Real> simY = vecvecToArray2D(tSimilarityY);
    TNT::Array2D<Real> csmOut = TNT::operator*(simX, simY);
    csm = array2DToVecvec(csmOut);
  }
}

} // namespace standard
} // namespace essentia

#include "algorithmfactory.h"

namespace essentia {
namespace streaming {

const char* CrossSimilarityMatrix::name = standard::CrossSimilarityMatrix::name;
const char* CrossSimilarityMatrix::description = standard::CrossSimilarityMatrix::description;

void CrossSimilarityMatrix::configure() {
  // configure parameters
  _referenceFeature = parameter("referenceFeature").toVectorVectorReal();
  _tau = parameter("tau").toInt();
  _embedDimension = parameter("embedDimension").toInt();
  _kappa = parameter("kappa").toReal();
  _noti = parameter("noti").toInt();
  _oti = parameter("oti").toBool();
  _otiBinary = parameter("otiBinary").toBool();
  _mathcCoef = 1; // for chroma binary sim-matrix based on OTI similarity as in [3]. 
  _mismatchCoef = 0; // for chroma binary sim-matrix based on OTI similarity as in [3]. 
  _minFramesSize = _embedDimension + 1;

  input("queryFeature").setAcquireSize(_minFramesSize);
  input("queryFeature").setReleaseSize(_tau);

  output("csm").setAcquireSize(1);
  output("csm").setReleaseSize(1);
}

AlgorithmStatus CrossSimilarityMatrix::process() {
 
  EXEC_DEBUG("process()");
  AlgorithmStatus status = acquireData();
  EXEC_DEBUG("data acquired (in: " << _queryFeature.acquireSize()
             << " - out: " << _csm.acquireSize() << ")");

  if (status != OK) {
    if (!shouldStop()) return status;

    // if shouldStop is true, that means there is no more audio coming, so we need
    // to take what's left to fill in half-frames, instead of waiting for more
    // data to come in (which would have done by returning from this function)

    int available = input("queryFeature").available();
    if (available == 0) return NO_INPUT;

    input("queryFeature").setAcquireSize(available);
    input("queryFeature").setReleaseSize(available);

    return process();
  }

  const std::vector<std::vector<Real> >& inputQueryFrames = _queryFeature.tokens();
  std::vector<std::vector<Real> > inputFramesCopy = inputQueryFrames; 
  // std::vector<std::vector<std::vector<Real> > >& csmOutput = _csm.tokens();
  std::vector<TNT::Array2D<Real> >& csmOutput = _csm.tokens();
  std::vector<std::vector<Real> > outputSimMatrix;

  if (input("queryFeature").acquireSize() < _minFramesSize) {
    for (size_t i=0; i<(_minFramesSize - input("queryFeature").acquireSize()); i++) {
      inputFramesCopy.push_back(inputQueryFrames[i]);
    }
  }

  // check whether to transpose by oti
  if (_oti == true) {
    int otiIdx = optimalTranspositionIndex(inputFramesCopy, _referenceFeature, _noti);
    std::rotate(_referenceFeature.begin(), _referenceFeature.end() - otiIdx, _referenceFeature.end());
  }
  
  std::vector<std::vector<Real> > queryTimeEmbed = toTimeEmbedding(inputFramesCopy, _embedDimension, _tau);
  std::vector<std::vector<Real> > referenceTimeEmbed = toTimeEmbedding(_referenceFeature, _embedDimension, _tau);

  // check whether to use oti-based binary similarity as mentioned in [3]
  if (_otiBinary == true) {
    outputSimMatrix = chromaBinarySimMatrix(queryTimeEmbed, referenceTimeEmbed, _noti, _mathcCoef, _mismatchCoef);
    csmOutput[0] = vecvecToArray2D(outputSimMatrix);
    releaseData();
  }

  // otherwise we compute similarity matrix as mentioned in [2]
  else if (_otiBinary == false) {
    // here we compute the pairwsie euclidean distances between query and reference song time embedding and finally tranpose the resulting matrix.
    std::vector<std::vector<Real> > pdistances = pairwiseDistance(queryTimeEmbed, referenceTimeEmbed);
    std::vector<std::vector<Real> > tpDistances = transpose(pdistances);

    size_t yRows = tpDistances.size();
    size_t yCols = tpDistances[0].size();

    // optimise the threshold computation on axis X by iniatilizing it to a matrix of ones
    std::vector<std::vector<Real> > similarityX(yCols, std::vector<Real>(yRows, 1));
    std::vector<std::vector<Real> > similarityY(yRows, std::vector<Real>(yCols, 0));

    // construct thresholded similarity matrix on axis Y
    std::vector<std::vector<Real> > tSimilarityY(yCols, std::vector<Real>(yRows));
    for (size_t u=0; u<yRows; u++) {
      for (size_t v=0; v<yCols; v++) {
        similarityY[u][v] = percentile(tpDistances[u], _kappa*100) - tpDistances[u][v];
        // here we binarise and transpose the similarityY array same time in order to avoid redundant looping
        if (similarityY[u][v] < 0) {
          tSimilarityY[v][u] = 0.;
        }
        else if (similarityY[u][v] >= 0) {
          tSimilarityY[v][u] = 1.;
        }
      }
    }

    TNT::Array2D<Real> simX = vecvecToArray2D(similarityX);
    TNT::Array2D<Real> simY = vecvecToArray2D(tSimilarityY);
    TNT::Array2D<Real> csmOut = TNT::operator*(simX, simY);
    csmOutput[0] = csmOut;
    releaseData();
  }
  // should not arrive here
  else EXEC_DEBUG("CrossSimilarityMatrix: wrong type parameter 'otiBinary'. Expect boolean type");
  
  return OK;
}


/*
// construct time delayed embedding from streaming input
std::vector<std::vector<Real> > CrossSimilarityMatrix::streamingFrames2TimeEmbedding(std::vector<std::vector<Real> > inputFrames, int m, int tau) {
  // check if previous query frame vector is empty
  if (_prevQueryFrame.empty()) {
   return toTimeEmbedding(inputFrames, _embedDimension, _tau);
  }
  
  std::vector<std::vector<Real> > currentFeature;
  // preallocate memory
  currentFeature.reserve(_prevQueryFrame.size() + inputFrames.size());
  // concat previous query stream frames with the current query stream frames
  currentFeature.insert(currentFeature.end(), _prevQueryFrame.begin(), _prevQueryFrame.end());
  currentFeature.insert(currentFeature.end(), inputFrames.begin(), inputFrames.end());

  std::vector<std::vector<Real> > outputVec;
  outputVec.reserve(_prevQueryFrame.size());

  for (size_t idx=0; idx<_prevQueryFrame.size(); idx+=tau) {
    outputVec.push_back(toTimeEmbedding(currentFeature, m, tau)[0]); // TODO: adapt code for tau!=1 conditions
    currentFeature.erase(currentFeature.begin() + idx);
  }
  if (currentFeature.empty()) EXEC_DEBUG("Couldn't find minimum amount of streams in the input");

  _prevQueryFrame.clear();
  _prevQueryFrame = currentFeature;
  return outputVec;
}
*/


} // namespace streaming
} // namespace essentia


// computes global averaged chroma hpcp as described in [1]
std::vector<Real> globalAverageChroma(std::vector<std::vector<Real> >& inputFeature) {

  size_t numbins = inputFeature[0].size();
  std::vector<Real> globalChroma(numbins);

  Real tSum;
  for (size_t j=0; j<numbins; j++) {
    tSum = 0;
    for (size_t i=0; i<inputFeature.size(); i++) {
      tSum += inputFeature[i][j];
    }
    globalChroma[j] = tSum;
  }
  // divide the sum array by the max element to normalise it to 0-1 range
  essentia::normalize(globalChroma);
  return globalChroma;
}

// Compute the optimal transposition index for transposing reference song feature to the musical key of query song feature as described in [1].
int optimalTranspositionIndex(std::vector<std::vector<Real> >& chromaA, std::vector<std::vector<Real> >& chromaB, int nshifts) {
    
  std::vector<Real> globalChromaA = globalAverageChroma(chromaA);
  std::vector<Real> globalChromaB = globalAverageChroma(chromaB);
  std::vector<Real> valueAtShifts;
  std::vector<Real> chromaBcopy = globalChromaB;
  for(int i=0; i<=nshifts; i++) {
    // circular rotate the input globalchroma by an index 'i'
    std::rotate(chromaBcopy.begin(), chromaBcopy.end() - i, chromaBcopy.end());
    // compute the dot product of the query global chroma and the shifted global chroma of reference song and append to an array
    valueAtShifts.push_back(essentia::dotProduct(globalChromaA, chromaBcopy));
    chromaBcopy = globalChromaB;
  }
  // compute the optimal index by finding the index of maximum element in the array of value at various shifts
  return essentia::argmax(valueAtShifts);
}


// Construct a stacked chroma embedding from an input chroma audio feature vector 
// [TODO]: In future use beat-synchronised stacked embeddings
std::vector<std::vector<Real> > toTimeEmbedding(std::vector<std::vector<Real> >& inputArray, int m, int tau) {

  if (m == 1) {
    return inputArray;
  }
  else {
    int stopIdx;
    int increment = m*tau;
    int frameSize = inputArray.size() - increment;
    int yDim = inputArray[0].size() * m;
    std::vector<std::vector<Real> > timeEmbedding(frameSize, std::vector<Real>(yDim, 0));
    std::vector<Real> tempRow;

    for (int i=0; i<frameSize; i+=tau) {
      stopIdx = i + increment;
      for (int startTime=i; startTime<stopIdx; startTime+=tau) {
        if (startTime == i) {
          tempRow = inputArray[startTime];
        }
        else {
          tempRow.insert(tempRow.end(), inputArray[startTime].begin(), inputArray[startTime].end());
        }
      timeEmbedding[i] = tempRow;
      }
    }
    return timeEmbedding;
  }
}

// Computes a binary similarity matrix from two chroma vector inputs using OTI as mentioned in [3]
std::vector<std::vector<Real> > chromaBinarySimMatrix(std::vector<std::vector<Real> >& chromaA, std::vector<std::vector<Real> >& chromaB, int nshifts, Real matchCoef, Real mismatchCoef) {

  int otiIndex;
  std::vector<Real> valueAtShifts;
  std::vector<Real> chromaBcopy;

  std::vector<std::vector<Real> > simMatrix(chromaA.size(), std::vector<Real>(chromaB.size(), 0));

  for (size_t i=0; i<chromaA.size(); i++) {
    for (size_t j=0; j<chromaB.size(); j++) {
      // compute OTI-based similarity for each frame of chromaA and chromaB
      for(int k=0; k<=nshifts; k++) {
        chromaBcopy = chromaB[j];
        std::rotate(chromaBcopy.begin(), chromaBcopy.end() - k, chromaBcopy.end());
        valueAtShifts.push_back(essentia::dotProduct(chromaA[i], chromaBcopy));
        chromaBcopy = chromaB[j];
      }
      otiIndex = essentia::argmax(valueAtShifts);
      valueAtShifts.clear();
      // assign matchCoef to similarity matrix if the OTI is 0 or 1 semitone
      if (otiIndex == 0 || otiIndex == 1) {
        simMatrix[i][j] = matchCoef;
      }
      else {
        simMatrix[i][j] = mismatchCoef;
      } 
    }
  }
  return simMatrix;
};
