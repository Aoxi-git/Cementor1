/*************************************************************************
*  Copyright (C) 2004 by Janek Kozicki                                   *
*  cosurgi@berlios.de                                                    *
*                                                                        *
*  This program is free software; it is licensed under the terms of the  *
*  GNU General Public License v2 or later. See file LICENSE for details. *
*************************************************************************/

#include "GLDrawLatticeBeamState.hpp"
#include "LatticeBeamParameters.hpp"
#include <yade/yade-lib-opengl/OpenGLWrapper.hpp>

void GLDrawLatticeBeamState::go(const shared_ptr<PhysicalParameters>& pp)
{
	static Real maxTensileFactor = 0.0; // FIXME - thread unsafe
	static Real maxCompressFactor = 0.0; // FIXME - thread unsafe

	LatticeBeamParameters* beam = static_cast<LatticeBeamParameters*>(pp.get());
	Real strain                     = beam->strain();
	Real factor;
	if( strain > 0 )
		factor                  = strain / beam->criticalTensileStrain; // positive
	else
		factor 			= strain / beam->criticalCompressiveStrain; // negative

	// compute optimal red/blue colors	
	maxTensileFactor		= std::max(factor,maxTensileFactor);
	maxCompressFactor 		= std::min(factor,maxCompressFactor);
	if(factor > 0 && maxTensileFactor > 0)
	{
		factor                  /= maxTensileFactor;
		glColor3v(Vector3f(0.9,0.9,1.0) - factor * Vector3f(0.9,0.9,0.0));
	}
	else if (factor < 0 && maxCompressFactor < 0)
	{
		factor                  /= maxCompressFactor;
		glColor3v(Vector3f(1.0,0.9,0.9) - factor * Vector3f(0.0,0.9,0.9));
	}
	else
		glColor3(0.9,0.9,0.9);

	glTranslatev(beam->se3.position);

	glScale(beam->length,beam->length,beam->length);

	glDisable(GL_LIGHTING);
	glBegin(GL_LINES);
		glVertex3v(beam->direction*(-0.5));
		glVertex3v(beam->direction*0.5);
		glColor3(0.4,0.4,0.4);
		glVertex3(0,0,0);
		glVertex3v(beam->otherDirection*0.2);
	glEnd();
}

