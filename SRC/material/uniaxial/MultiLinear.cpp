/* ****************************************************************** **
**    OpenSees - Open System for Earthquake Engineering Simulation    **
**          Pacific Earthquake Engineering Research Center            **
**                                                                    **
**                                                                    **
** (C) Copyright 1999, The Regents of the University of California    **
** All Rights Reserved.                                               **
**                                                                    **
** Commercial use of this program without express permission of the   **
** University of California, Berkeley, is strictly prohibited.  See   **
** file 'COPYRIGHT'  in main directory for information on usage and   **
** redistribution,  and for a DISCLAIMER OF ALL WARRANTIES.           **
**                                                                    **
** Developed by:                                                      **
**   Frank McKenna (fmckenna@ce.berkeley.edu)                         **
**   Gregory L. Fenves (fenves@ce.berkeley.edu)                       **
**   Filip C. Filippou (filippou@ce.berkeley.edu)                     **
**                                                                    **
** ****************************************************************** */
                                                                        
// Written: fmk 
// Created: 05/12
// Revision: A
//
// Description: This file contains the class implementation for multilinear material. 
//
// What: "@(#) MultiLinear.C, revA"


#include <MultiLinear.h>
#include <Vector.h>
#include <Matrix.h>
#include <Channel.h>
#include <math.h>
#include <float.h>

#include <elementAPI.h>
#define OPS_Export 
OPS_Export void *
OPS_New_MultiLinear(void)
{
  // Pointer to a uniaxial material that will be returned
  UniaxialMaterial *theMaterial = 0;

  if (OPS_GetNumRemainingInputArgs() < 5) {
    opserr << "Invalid #args,  want: uniaxialMaterial MultiLinear tag? e1 s1 e2 s2 ... " << endln;
    return 0;
  }
    
  int iData[1];
  int numData = 1;
  if (OPS_GetIntInput(&numData, iData) != 0) {
    opserr << "WARNING invalid tag or soilType uniaxialMaterial MultiLinearMaterial" << endln;
    return 0;
  }

  numData = OPS_GetNumRemainingInputArgs();

  int numSlope = numData/2;
  double *dData = new double[numData];
  if (OPS_GetDoubleInput(&numData, dData) != 0) {
    opserr << "Invalid pyData data for material uniaxial MultiLinear " << iData[0] << endln;
    return 0;	
  }

  Vector e(numSlope);
  Vector s(numSlope);
  for (int i=0; i<numSlope; i++) {
    e(i) = dData[2*i];
    s(i) = dData[2*i+1];
  }

  // Parsing was successful, allocate the material
  theMaterial = new MultiLinear(iData[0], s, e);
  if (theMaterial == 0) {
    opserr << "WARNING could not create uniaxialMaterial of type MultiLinear\n";
    return 0;
  }

  return theMaterial;
}


MultiLinear::MultiLinear(int tag, const Vector &s, const Vector &e)
  :UniaxialMaterial(tag,MAT_TAG_MultiLinear),
   numSlope(0)
{

  numSlope = e.Size();
  data.resize(numSlope, 6);

  data(0,0) = -e(0);      // pos yeild strain
  data(0,1) = e(0);     // neg yeild strain
  data(0,2) = -s(0);      // pos yield stress
  data(0,3) = s(0);     // neg yeild stress
  data(0,4) = s(0)/e(0); // slope
  data(0,5) = e(0);      // dist - (0-1)/2

  for (int i=1; i<numSlope; i++) {
    data(i,0) = -e(i);
    data(i,1) = e(i);
    data(i,2) = -s(i);
    data(i,3) = s(i);
    data(i,4) = (s(i)-s(i-1)) /
	(e(i)-e(i-1));
    data(i,5) = e(i)-e(i-1);
  }    

  tStrain = 0.0;
  tStress = 0.0;
  tTangent = data(0,4);

  cStrain = 0.0;
  cStress = 0.0;
  cTangent = tTangent;

  e0 = 0.0;
  tSlope = 0;
  cSlope = 0;; 

  this->Print(opserr);
}


MultiLinear::MultiLinear()
:UniaxialMaterial(0,MAT_TAG_MultiLinear),
 numSlope(0)
{

}

MultiLinear::~MultiLinear()
{
  // does nothing
}

int 
MultiLinear::setTrialStrain(double strain, double strainRate)
{
  opserr << "setTrialStrain: " << tStrain << endln;

  if (fabs(tStrain - strain) < DBL_EPSILON)
    return 0;

  tStrain = strain;
  tSlope = 0;
  
  if (tStrain >= data(0,0) && tStrain <= data(0,1)) { // elastic
    tSlope = 0;
    tStress = data(0,2) + (tStrain-data(0,0))*data(0,4);
    tTangent = data(0,4);
  } else if (tStrain < data(0,0)) { // search neg of data
    tSlope = 1;
    while (tSlope < numSlope && tStrain < data(tSlope,0))
      tSlope++;
    if (tSlope == numSlope)
      tSlope = numSlope-1;
    tStress = data(tSlope,2) + (tStrain-data(tSlope,0))*data(tSlope,4);
    tTangent = data(tSlope,4);    
  } else { // serach pos side of data
    tSlope = 1;
    while (tSlope < numSlope && tStrain > data(tSlope,1))
      tSlope++;
    if (tSlope == numSlope)
      tSlope = numSlope-1;
    tStress = data(tSlope,3) + (tStrain-data(tSlope,1))*data(tSlope,4);
    tTangent = data(tSlope,4);    
  }
	     
  return 0;
}
    
double 
MultiLinear::getStrain(void)
{
  return tStrain;

}

double 
MultiLinear::getStress(void)
{
  return tStress;
}

double 
MultiLinear::getTangent(void)
{
  return tTangent;
}

int 
MultiLinear::commitState(void)
{
  opserr << "COMMIT\n";
  // if yielded we need to reset the values               
  if (tSlope != 0) { // yielded

    if (tStrain > data(0,1)) { // upper curve
      data(0,1) = tStrain;
      data(0,3) = tStress;
      data(0,0) = tStrain - 2*data(0,5);
      data(0,2) = tStress - 2*data(0,5)*data(0,4);

      double dSlopeStrain = tStrain-data(tSlope,1);
      double dSlopeStress = tStress-data(tSlope,3);

      for (int i=1; i<tSlope; i++) {
	data(i,1) = tStrain;
	data(i,3) = tStress;
	data(i,0) = data(i-1,0) - 2*data(i,5);
	data(i,2) = data(i-1,2) - 2*data(i,5)*data(i,4);
      }

      data(tSlope,0) = data(tSlope-1,0) - 2*data(tSlope,5) 
	+ data(tSlope,1) - data(tSlope-1,1);
      data(tSlope,2) = data(tSlope-1,2) 
	+ (data(tSlope,0)-data(tSlope-1,0))*data(tSlope,4);

      double dStrain = tStrain-cStrain;
      double dStress = tStress-cStress;
      for (int i=tSlope+1; i<numSlope; i++) {
	//	data(i,1) = data(i-1,1) + data(i,5);
	//data(i,3) = data(i-1,3) + 2*data(i,5)*data(i,4);
	//	data(i,1) += dStrain;
	//		data(i,3) += dStress;
	data(tSlope,0) = data(tSlope-1,0) - 2*data(tSlope,5) 
	  + data(tSlope,1) - data(tSlope-1,1);
	data(tSlope,2) = data(tSlope-1,2) 
	  + (data(tSlope,0)-data(tSlope-1,0))*data(tSlope,4);
      }

      /*      
      data(tSlope,0) = data(tSlope-1,0) - data(tSlope,5) 
	+ tStrain - data(tSlope-1,1);
      data(tSlope,2) = data(tSlope-1,2) 
	+ (data(tSlope,0)-data(tSlope-1,0))*data(tSlope,4);

      double dStrain = tStrain-cStrain;
      double dStress = tStress-cStress;
      for (int i=tSlope+1; i<numSlope; i++) {
	data(i,0) += dStrain;
	data(i,2) += dStress;
      }
      */

    } else {

      data(0,0) = tStrain;
      data(0,2) = tStress;
      data(0,1) = tStrain + 2*data(0,5);
      data(0,3) = tStress + 2*data(0,5)*data(0,4);

      double dSlopeStrain = tStrain-data(tSlope,1);
      double dSlopeStress = tStress-data(tSlope,3);

      for (int i=1; i<tSlope; i++) {
	data(i,0) = tStrain;
	data(i,2) = tStress;
	data(i,1) = data(i-1,1) + 2*data(i,5);
	data(i,3) = data(i-1,3) + 2*data(i,5)*data(i,4);
      }

      data(tSlope,1) = data(tSlope-1,1) + 2*data(tSlope,5) 
	+ data(tSlope,0) - data(tSlope-1,0);
      data(tSlope,3) = data(tSlope-1,3) 
	+ (data(tSlope,1)-data(tSlope-1,1))*data(tSlope,4);

      double dStrain = tStrain-cStrain;
      double dStress = tStress-cStress;
      for (int i=tSlope+1; i<numSlope; i++) {
	//	data(i,1) = data(i-1,1) + data(i,5);
	//data(i,3) = data(i-1,3) + 2*data(i,5)*data(i,4);
	//	data(i,1) += dStrain;
	//		data(i,3) += dStress;
	data(tSlope,1) = data(tSlope-1,1) + 2*data(tSlope,5) 
	  + data(tSlope,0) - data(tSlope-1,0);
	data(tSlope,3) = data(tSlope-1,3) 
	  + (data(tSlope,1)-data(tSlope-1,1))*data(tSlope,4);
      }

    }
  }
  	
  cStress=tStress;
  cStrain=tStrain;
  cTangent = tTangent;

  opserr << "MultiLinear:: commitState - end\n";
  this->Print(opserr);
    
  return 0;
}

int 
MultiLinear::revertToLastCommit(void)
{
  tStrain = cStrain;
  tTangent = cTangent;
  tStress = cStress;

  return 0;
}


int 
MultiLinear::revertToStart(void)
{
  tStrain = cStrain = 0.0;
  tTangent = cTangent = data(elasticPosSlope,2);
  tStress = cStress = 0.0;

  return 0;
}


UniaxialMaterial *
MultiLinear::getCopy(void)
{
  MultiLinear *theCopy =
    new MultiLinear();
  theCopy->data = this->data;
  theCopy->numSlope = this->numSlope;
  theCopy->tSlope = this->tSlope;
  theCopy->cSlope = this->cSlope;
  
  theCopy->tStress = this->tStress;
  theCopy->tStrain = this->tStrain;
  theCopy->tTangent = this->tTangent;
  theCopy->cStress = this->cStress;
  theCopy->cStrain = this->cStrain;
  theCopy->cTangent = this->cTangent;

  theCopy->Print(opserr);
  opserr << "END getCOPY\n";
  return theCopy;
}


int 
MultiLinear::sendSelf(int cTag, Channel &theChannel)
{
  int res = -1;
  return res;
}

int 
MultiLinear::recvSelf(int cTag, Channel &theChannel, 
				 FEM_ObjectBroker &theBroker)
{
  int res = -1;
  return res;
}

void 
MultiLinear::Print(OPS_Stream &s, int flag)
{
    s << "MultiLinear tag: " << this->getTag() << endln;
    s << "  stress: " << tStress << " tangent: " << tTangent << endln;
  opserr << "tSlope: " << tSlope << "numSlope: " << numSlope << endln;
  opserr << data;
}

