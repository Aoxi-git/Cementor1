/*************************************************************************
*  2021 jerome.duriez@inrae.fr                                           *
*  This program is free software, see file LICENSE for details.          *
*************************************************************************/

#ifdef YADE_LS_DEM
#include <pkg/levelSet/OtherClassesForLSContact.hpp>
#include <pkg/levelSet/ShopLS.hpp>

namespace yade {
YADE_PLUGIN((Bo1_LevelSet_Aabb));
CREATE_LOGGER(Bo1_LevelSet_Aabb);

void Bo1_LevelSet_Aabb::go(const shared_ptr<Shape>& cm, shared_ptr<Bound>& bv, const Se3r& se3, const Body*)
{ //TODO: use Eigen Aligned Box.extend to avoid the 2*6 if below ?
	// NB: see BoundDispatcher::processBody() (called by BoundDispatcher::action(), called by InsertionSortCollider::action()) in pkg/common/Dispatching.cpp for the attributes used upon calling
	if (!bv) { bv = shared_ptr<Bound>(new Aabb); }
	Aabb*     aabb    = static_cast<Aabb*>(bv.get()); // no need to bother deleting that raw pointer: e.g. https://stackoverflow.com/q/53908753/9864634
	LevelSet* lsShape = static_cast<LevelSet*>(cm.get());
	Real      inf     = std::numeric_limits<Real>::infinity();
	// We compute the bounds from LevelSet->corners serving as an Aabb in local frame, and considering the transformation from that local frame
	// NB: it is useless to try to make something much similar to Bo1_Box_Aabb::go(), in case se3.position of our level set body would not be in the middle of the lsShape->corners box, contrary to Box bodies and their extents and "halfSize" in Bo1_Box_Aabb::go()

	if (!lsShape->corners.size()) { // kind of "first iteration"
		Vector3i nGP(lsShape->lsGrid->nGP);
		Vector3r gp;
		int      nGPx(nGP[0]), nGPy(nGP[1]), nGPz(nGP[2]);
		Real     xGP, yGP, zGP;
		Real     xMin(inf), xMax(-inf), yMin(inf), yMax(-inf), zMin(inf), zMax(-inf); // extrema values for the "inside" gridpoints
		// identifying the extrema coordinates within the surface using a nGridPoints^3 loop. Would be much better to do 3*nGridpoints^2 loops, if possible, searching for find_if(firstIterator to lsValAlongZAxis,lastIterator to lsValAlongZAxis, within surface) which is easy for the zAxis = lsGrid->gridPoint[xInd][yInd], but how accessing an yAxis ~ lsGrid->gridPoint[xInd][:][zInd] which does not exist in C++ ?
		for (int xInd = 0; xInd < nGPx; xInd++) {
			for (int yInd = 0; yInd < nGPy; yInd++) {
				for (int zInd = 0; zInd < nGPz; zInd++) {
					gp  = lsShape->lsGrid->gridPoint(xInd, yInd, zInd);
					xGP = gp[0];
					yGP = gp[1];
					zGP = gp[2];
					if (lsShape->distField[xInd][yInd][zInd] <= 0) {
						if (xGP < xMin) xMin = xGP;
						if (xGP > xMax) xMax = xGP;
						if (yGP < yMin) yMin = yGP;
						if (yGP > yMax) yMax = yGP;
						if (zGP < zMin) zMin = zGP;
						if (zGP > zMax) zMax = zGP;
					}
				}
			}
		}
		if ((xMin == xMax) or (yMin == yMax) or (zMin == zMax))
			LOG_WARN("One flat LevelSet body, as detected by shape.corners computation, was that expected ? (is the grid too coarse ?)");
		// right now, our *Min, *Max define a downwards-rounded Aabb (smaller than true surface), let's make it upwards-rounded below:
		Real g(lsShape->lsGrid->spacing);
		for (int iInd = 0; iInd < 2; iInd++) {
			for (int jInd = 0; jInd < 2; jInd++) {
				for (int kInd = 0; kInd < 2; kInd++)
					lsShape->corners.push_back(
					        Vector3r(iInd == 0 ? xMin - g : xMax + g, jInd == 0 ? yMin - g : yMax + g, kInd == 0 ? zMin - g : zMax + g));
			}
		}
	}
	if (lsShape->corners.size() != 8) LOG_ERROR("We have a LevelSet-shaped body with some shape.corners computed but not 8 of them !");
	std::array<Vector3r, 8> cornersCurrent; // current positions of corners (in global frame)
	for (int corner = 0; corner < 8; corner++)
		cornersCurrent[corner] = ShopLS::rigidMapping(lsShape->corners[corner], Vector3r::Zero(), se3.position, se3.orientation);
	// NB: corners should average to the origin. This is kind of tested through LevelSet.center in levelSetBody() Py function

	Real xCorner, yCorner, zCorner;                                           // the ones of cornersCurrent
	Real xMin(inf), xMax(-inf), yMin(inf), yMax(-inf), zMin(inf), zMax(-inf); // extrema values for the cornersCurrent
	for (int corner = 0; corner < 8; corner++) {
		xCorner = cornersCurrent[corner][0];
		yCorner = cornersCurrent[corner][1];
		zCorner = cornersCurrent[corner][2];
		if (xCorner < xMin) xMin = xCorner;
		if (xCorner > xMax) xMax = xCorner;
		if (yCorner < yMin) yMin = yCorner;
		if (yCorner > yMax) yMax = yCorner;
		if (zCorner < zMin) zMin = zCorner;
		if (zCorner > zMax) zMax = zCorner;
	}
	aabb->min = Vector3r(xMin, yMin, zMin);
	aabb->max = Vector3r(xMax, yMax, zMax);
}

} // namespace yade
#endif //YADE_LS_DEM
