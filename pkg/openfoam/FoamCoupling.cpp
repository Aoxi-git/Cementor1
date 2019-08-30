// *  Deepak kn : deepak.kunhappan@3sr-grenoble.fr; deepak.kn1990@gmail.com

#ifdef YADE_MPI

#include <mpi.h>
#include "FoamCoupling.hpp"

#include<pkg/common/Facet.hpp>
#include<pkg/common/Box.hpp>
#include<pkg/common/Sphere.hpp>
#include<pkg/common/Grid.hpp>

#include <iostream>

namespace yade { // Cannot have #include directive inside.

CREATE_LOGGER(FoamCoupling);
YADE_PLUGIN((FoamCoupling));
YADE_PLUGIN((FluidDomainBbox));
CREATE_LOGGER(FluidDomainBbox);
YADE_PLUGIN((Bo1_FluidDomainBbox_Aabb));


void Bo1_FluidDomainBbox_Aabb::go(const shared_ptr<Shape>& cm, shared_ptr<Bound>& bv, const Se3r& , const Body* ){
	
	FluidDomainBbox* domain = static_cast<FluidDomainBbox*>(cm.get());
	if (!bv){bv = shared_ptr<Bound> (new Aabb); }
	Aabb* aabb = static_cast<Aabb*>(bv.get()); 
	
	aabb-> min = domain -> minBound; 
	aabb-> max = domain -> maxBound; 
	return ; 
}


/******serial version *****/ 

void FoamCoupling::getRank() {

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &commSize);

}

void FoamCoupling::setNumParticles(int np){

      getRank(); 
      numParticles = np;
      castNumParticle(numParticles); 
      initDone = true; 
  
}

void FoamCoupling::setIdList(const std::vector<int>& alist) {
  bodyList.clear(); bodyList.resize(alist.size()); 
  for (unsigned int i=0; i != bodyList.size(); ++i){
    bodyList[i] = alist[i];
  }

}


void FoamCoupling::insertBodyId(int bId){
	bodyList.push_back(bId);
  
}

bool FoamCoupling::eraseId(int bId){
	auto it = std::find(bodyList.begin(), bodyList.end(), bId);
	if (it != bodyList.end()){bodyList.erase(it); return true; }
	else {
		LOG_ERROR("Id not found in list of ids in coupling"); 
		return false; 
	}
}


int FoamCoupling::getNumBodies(){
	return bodyList.size(); 
}

std::vector<int> FoamCoupling::getIdList(){
	return bodyList; 
}

void FoamCoupling::castParticle() {
  
	int sz = bodyList.size(); 
	MPI_Bcast(&sz, 1, MPI_INT, rank, MPI_COMM_WORLD);
	procList.resize(sz); hydroForce.resize(sz*6); 
	particleData.resize(10*sz); 
	std::fill(procList.begin(), procList.end(), -1); 
	std::fill(hydroForce.begin(), hydroForce.end(), 1e-50); 

#ifdef YADE_OPENMP
#pragma omp parallel  for collapse (1)
#endif

for (unsigned int i=0; i <  bodyList.size(); ++i)
  {
    const Body* b = (*scene -> bodies)[bodyList[i]].get();
    if ( scene-> isPeriodic){
      Vector3r pos = scene->cell->wrapPt( b->state->pos);
      particleData[i*10] = pos[0];
      particleData[i*10+1] = pos[1];
      particleData[i*10+2] = pos[2];
    } else {

    particleData[i*10] = b->state->pos[0];
    particleData[i*10+1] = b->state->pos[1];
    particleData[i*10+2] = b->state->pos[2];
  }
    particleData[i*10+3] = b->state->vel[0];
    particleData[i*10+4] = b->state->vel[1];
    particleData[i*10+5] = b->state->vel[2];
    particleData[i*10+6] = b->state->angVel[0];
    particleData[i*10+7] = b->state->angVel[1];
    particleData[i*10+8] = b->state->angVel[2];
    shared_ptr<Sphere> s = YADE_PTR_DYN_CAST<Sphere>(b->shape);
    particleData[i*10+9] = s->radius;
  }
  MPI_Bcast(&particleData.front(), particleData.size(), MPI_DOUBLE, rank, MPI_COMM_WORLD);
  // clear array after bcast
  particleData.clear(); 

}

void FoamCoupling::castNumParticle(int value) {
	MPI_Bcast(&value, 1, MPI_INT, rank, MPI_COMM_WORLD);
}


void FoamCoupling::castTerminate() {
  int value = 10; 
  MPI_Bcast(&value, 1, MPI_INT, rank, MPI_COMM_WORLD);

}

void FoamCoupling::updateProcList()
{
  for (unsigned int i=0; i != bodyList.size(); ++i)
  {
     int dummy_val = -5;
     MPI_Allreduce(&dummy_val,&procList[i],1,MPI_INT, MPI_MAX, MPI_COMM_WORLD);
     if (procList[i] < 0 )  std::cout << "Particle not found in FOAM " << std::endl;
   }
   
   
}

void FoamCoupling::recvHydroForce() {
  for (unsigned int i=0; i!= procList.size(); ++i) {
    int recvFrom = procList[i];
    for (unsigned int j=0; j != 6; ++j) {
     MPI_Recv(&hydroForce[6*i+j],1,MPI_DOUBLE,recvFrom,sendTag,MPI_COMM_WORLD,&status); 
    }
     
}}

void FoamCoupling::setHydroForce() {

  // clear hydroforce before summation
  
 #ifdef YADE_OPENMP
 #pragma omp parallel for collapse(1)
 #endif
    for (unsigned int i=0; i < bodyList.size(); ++i) {
       const Vector3r& fx=Vector3r(hydroForce[6*i], hydroForce[6*i+1], hydroForce[6*i+2]);
       const Vector3r& tx=Vector3r(hydroForce[6*i+3], hydroForce[6*i+4], hydroForce[6*i+5]);
       scene->forces.addForce(bodyList[i], fx);
       scene->forces.addTorque(bodyList[i], tx);
  }

}

void FoamCoupling::sumHydroForce() {
  // clear the vector
  std::fill(hydroForce.begin(), hydroForce.end(), 0.0);
  Real dummy_val = 0.0;
  for (unsigned int i=0; i != bodyList.size(); ++i) {
   for (unsigned int j=0; j != 6; ++j){
     MPI_Allreduce(&dummy_val ,&hydroForce[6*i+j],1,MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
   }
  }
}


void FoamCoupling::resetProcList() {
     procList.clear(); 
}


void FoamCoupling::action() {

	if (!couplingModeParallel) {
		if ( exchangeData()) {
			runCoupling();
			exchangeDeltaT();
		}
		setHydroForce();  
	} else {
		if (exchangeData()) {
			runCouplingParallel(); 
			exchangeDeltaTParallel(); 
		}
		
	}
}

bool FoamCoupling::exchangeData(){

	return scene->iter%dataExchangeInterval==0;

}

void FoamCoupling::exchangeDeltaT() {

  // Recv foamdt  first and broadcast;
  MPI_Recv(&foamDeltaT,1,MPI_DOUBLE,1,sendTag,MPI_COMM_WORLD,&status);
  //bcast yadedt to others.
  Real  yadeDt = scene-> dt;
  MPI_Bcast(&yadeDt,1,MPI_DOUBLE, rank, MPI_COMM_WORLD);
  // calculate the interval . TODO: to include hydrodynamic time scale if inertial in openfoam
  // here -> hDeltaT = getViscousTimeScale();
  dataExchangeInterval = (long int) ((yadeDt < foamDeltaT) ? foamDeltaT/yadeDt : 1);

}

Real FoamCoupling::getViscousTimeScale() {

//  Real hDeltaT = 0.0;
//  Real dummy = 1e9;
//
//  MPI_Allreduce(&dummy, &hDeltaT, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
//
	return 0;
}

void FoamCoupling::runCoupling() {
	castParticle();
	updateProcList();
	if (isGaussianInterp){
		sumHydroForce();
	} else {
		recvHydroForce();
	}
}


/***********parallel version *******/

void FoamCoupling::getFluidDomainBbox() {
  
	
	/* get the bounding box of the grid from each fluid solver processes, this gird minmax is used to set the min/max of the body of shape FluidDomainBbox. 
	 All Yade processes have ranks from 0 to yadeCommSize - 1 in the  MPI_COMM_WORLD communicator, the fluid Ranks are then from yadeCommSize to size(M
	 PI_COMM_WORLD) -1, all yade ranks receive the min max of the fluid domains, and insert it to their body containers. The fluid subdomain bodies have subdomain=0, they are actually owned 
	 by the master process (rank=0) in the yade communicator. */ 
	
	//get local comm size and local rank. 
	
	MPI_Comm_size(scene->mpiComm, &localCommSize); 
	MPI_Comm_rank(scene->mpiComm, &localRank); 
	
	//world comm size and world rank 
	
	MPI_Comm_size(MPI_COMM_WORLD, &worldCommSize); 
	MPI_Comm_rank(MPI_COMM_WORLD, &worldRank); 
	
	commSzdff = abs(localCommSize-worldCommSize); 
	
	if (worldRank-localCommSize < 0 ) {stride = localCommSize; } else { stride = 0; }
	
	std::cout << "stride val = " << std::endl; 
	
	//std::vector<int> fluidRanks(szdff, -1); 
	
	// vector to hold the minMax buff 
	std::vector<std::vector<double> > minMaxBuff; 
	
	//alloc memory 
	for (int i=0; i != commSzdff; ++i){
		std::vector<double> buff(6, 1e-50); 
		minMaxBuff.push_back(std::move(buff)); 
	  
	}

	//recv the grid minmax from fluid solver. 
	
	for (int rnk=0; rnk != commSzdff; ++rnk){
		MPI_Status status; 
		std::vector<double>& buff = minMaxBuff[rnk]; 
		MPI_Recv(&buff.front(), 6, MPI_DOUBLE, rnk+stride , TAG_GRID_BBOX, MPI_COMM_WORLD, &status); 
		
	}
	
	//create fluidDomainBbox bodies and get their ids. 
	for (int fd = 0; fd != commSzdff; ++fd){
		shared_ptr<Body>  flBody(shared_ptr<Body> (new Body()));
		shared_ptr<FluidDomainBbox> flBodyshape(shared_ptr<FluidDomainBbox>  (new FluidDomainBbox())); 
		std::vector<double>& buff = minMaxBuff[fd];
		flBodyshape->setMinMax(buff); 
		flBodyshape->domainRank = stride+fd; 
		flBodyshape->hasIntersection = false; 
		flBody->shape = flBodyshape;  // casting?!  
		fluidDomains.push_back(scene->bodies->insert(flBody)); 
		
	}
	
	std::cout << "recvd grid min max, rank = " <<  localRank  << std::endl; 
	commSizeSet = true; 
	minMaxBuff.clear(); // dealloc the recvd minMaxBuff; 
	  
}


void FoamCoupling::buildSharedIdsMap(){
	/*Builds the list of ids interacting with a fluid subdomain and stores those body ids that has intersections with several fluid domains. 
	 sharedIdsMapIndx = a vector of std::pair<Body::id_t, std::map<fluidDomainId, indexOfthebodyinthefluidDomain aka index in flbdy-> bIds> > */
	std::cout << "In build shared Ids Map rank =  " <<  std::endl; 
	// const shared_ptr<Subdomain>& subd = YADE_PTR_CAST<Subdomain>((*scene->bodies)[scene->thisSubdomainId]->shape); //not needed as we have localIds list. 
	for (int bodyId : localIds){
		std::map<int, int> testMap; 
		const auto& bIntrs = (*scene->bodies)[bodyId]->intrs; 
		for (const auto& itIntr : bIntrs){
			const shared_ptr<Interaction>& intr = itIntr.second; 
			Body::id_t otherId; 
			if (bodyId  == intr->getId1()){otherId = intr->getId2(); } else {otherId = intr->getId1(); } 
			if (ifFluidDomain(otherId)){
				const shared_ptr<FluidDomainBbox>& flbox = YADE_PTR_CAST<FluidDomainBbox> ((*scene->bodies)[otherId]->shape); 
				flbox->bIds.push_back(bodyId); 
				flbox->hasIntersection = true; 
				int indx = flbox->bIds.size()-1; 
				testMap.insert(std::make_pair(flbox->domainRank,indx)); // get the fluiddomainbbox rank and index in flbody->bIds, this will be used in the verifyTracking function 
				
			}
		}
		if (testMap.size() > 1) {	// this body has intersections with more than one fluid domains, hence this is a shared id . 
			sharedIdsMapIndx.push_back(std::make_pair(bodyId, testMap)); 
		}
	}
  
}


//unused. 
void FoamCoupling::buildSharedIds() {
	/*It is possible to have one yade body to have interactions with several  fluid subdomains, (we just have the bounding box of the fluid domain, the fluid domain is a regular polygon with several faces). 
	 Building a list of those ids which are have several interactions helps to identify those fliud procs from whom to receive the hydrodynamic force and tracking. This is used in the function 
	verifyParticleDetection. */
	
	const shared_ptr<Body>& subdBody = (*scene->bodies)[scene->thisSubdomainId]; 
	const shared_ptr<Subdomain>& subD = YADE_PTR_CAST<Subdomain>(subdBody->shape); 
	for (int bodyId = 0; bodyId != static_cast<int>(subD->ids.size()); ++bodyId){
		std::vector<Body::id_t> fluidIds; 
		const shared_ptr<Body>& testBody = (*scene->bodies)[subD->ids[bodyId]]; 
		for (const auto& bIntrs : testBody->intrs){
			const shared_ptr<Interaction>& intr = bIntrs.second; 
			Body::id_t otherId; 
			if (testBody->id == intr->getId1()){otherId = intr->getId2();  } else { otherId = intr->getId1(); }
			if (ifFluidDomain(otherId)){ fluidIds.push_back(otherId); }
		}
		if (fluidIds.size() > 1){sharedIds.push_back(std::make_pair(subD->ids[bodyId], fluidIds)); }  // this body has interaction with  more than one fluid  grids. 
	}
}

//unused. 
int FoamCoupling::ifSharedId(const Body::id_t& testId){
	/*function to check if given body id is a shared id. */
	if (! sharedIds.size()) {return false; } // this subdomain does not have any shared ids. 
	int res = -1; 
	for (unsigned indx =0; indx != sharedIds.size(); ++indx ){
		if (testId == sharedIds[indx].first) {
			res = indx; 
		}
	}
	
	return res; 
	
}

int FoamCoupling::ifSharedIdMap(const Body::id_t& testId){
	int indx = 0; 
	for (const auto& it : sharedIdsMapIndx){
		if (it.first == testId) {return indx; }
		++indx; 
	}
	
	return -1; 
}

bool FoamCoupling::ifFluidDomain(const Body::id_t&  testId ){
	/* function to check if the body is fluidDomainBox*/ 
	
	const auto& iter = std::find(fluidDomains.begin(), fluidDomains.end(), testId);  
	if (iter != fluidDomains.end()){return true; } else {return false;}
  
}

// void FoamCoupling::findIntersections(){
// 	
// 	/*find bodies intersecting with the fluid domain bbox, get their ids and ranks of the intersecting fluid processes.  */
// 		
// 	for (unsigned f = 0; f != fluidDomains.size(); ++f){
// 		shared_ptr<Body>& fdomain = (*scene->bodies)[fluidDomains[f]]; 
// 		if (fdomain){
// 			for (const auto& fIntrs : fdomain->intrs){
// 				Body::id_t otherId; 
// 				const shared_ptr<Interaction>& intr = fIntrs.second;
// 				if (fdomain->id == intr->getId1()){otherId = intr->getId2(); } else { otherId = intr->getId1(); }
// 				const shared_ptr<Body>& testBody = (*scene->bodies)[otherId]; 
// 				if (testBody) {
// 					if (testBody->subdomain==scene->subdomain){
// 						if (!ifDomainBodies(testBody)){
// 							const shared_ptr<FluidDomainBbox>& flBox = YADE_PTR_CAST<FluidDomainBbox>(fdomain->shape); 
// 							flBox->bIds.push_back(testBody->id); 
// 							flBox->hasIntersection = true; 
// 						}
// 					}
// 				} 
// 			}
// 		
// 	}
// 	}
// }

bool FoamCoupling::ifDomainBodies(const shared_ptr<Body>& b) {
	// check if body is subdomain, wall, facet, or other fluidDomainBbox 
	
	shared_ptr<Box> boxShape = YADE_PTR_DYN_CAST<Box> (b->shape); 
	shared_ptr<FluidDomainBbox> fluidShape = YADE_PTR_DYN_CAST<FluidDomainBbox> (b->shape); 
	shared_ptr<Facet> facetShape = YADE_PTR_DYN_CAST<Facet>(b->shape); 
	
	if (b->getIsSubdomain()){return true; }
	else if (boxShape) {return true; }
	else if (facetShape) {return true; }
	else {return false; }
	 
}

void FoamCoupling::sendIntersectionToFluidProcs(){
	
	// notify the fluid procs about intersection based on number of intersecting bodies. 
	// vector of sendRecvRanks, with each vector element containing the number of bodies, if no bodies, send negative val. 

	sendRecvRanks.resize(fluidDomains.size()); 
	
	for (unsigned f=0;  f != fluidDomains.size(); ++f){
		const shared_ptr<Body>& fdomain = (*scene->bodies)[fluidDomains[f]]; 
		if (fdomain){
			const shared_ptr<FluidDomainBbox>& fluidBox = YADE_PTR_CAST<FluidDomainBbox>(fdomain->shape); 
			if (fluidBox->hasIntersection){
				sendRecvRanks[f] = fluidBox->bIds.size(); 
				
			} else {sendRecvRanks[f] = -1; }
		} 
		else {sendRecvRanks[f] = -1; }
	}
	// 
	int buffSz = fluidDomains.size(); 
	
	//MPI_Send ..
	
	for (int rnk = 0; rnk != commSzdff; ++ rnk){
		MPI_Send(&sendRecvRanks.front(), buffSz, MPI_INT, rnk+stride, TAG_SZ_BUFF, MPI_COMM_WORLD); 
		
	}
	
}

void FoamCoupling::sendBodyData(){
	/* send the particle data to the associated fluid procs. prtData -> pos, vel, angvel, raidus (for sphere), if fiber -> ori  */ 
	std::cout <<  "sending body data " << std::endl;  
	bool isPeriodic = scene->isPeriodic; 
	for (int f = 0; f != static_cast<int>(fluidDomains.size()); ++f){
		const shared_ptr<Body>& flbody = (*scene->bodies)[fluidDomains[f]];
		if (flbody){
		const shared_ptr<FluidDomainBbox> flbox = YADE_PTR_CAST<FluidDomainBbox>(flbody->shape); 
			if (flbox->hasIntersection){
				std::vector<double> prtData(10*flbox->bIds.size(), 1e-50);
				for (unsigned int i=0; i != flbox->bIds.size(); ++i){
					const shared_ptr<Body>& b = (*scene->bodies)[flbox->bIds[i]]; 
					if (isPeriodic){
						const Vector3r& pos = scene->cell->wrapPt(b->state->pos); 
						prtData[10*i] = pos[0]; 
						prtData[10*i+1] = pos[1]; 
						prtData[10*i+2] = pos[2]; 
						
					} else {
						prtData[10*i] = b->state->pos[0]; 
						prtData[10*i+1] = b->state->pos[1];
						prtData[10*i+2] = b->state->pos[2]; 
					}
					prtData[10*i+3] = b->state->vel[0]; 
					prtData[10*i+4] = b->state->vel[1]; 
					prtData[10*i+5] = b->state->vel[2]; 
					prtData[10*i+6] = b->state->angVel[0]; 
					prtData[10*i+7] = b->state->angVel[1]; 
					prtData[10*i+8] = b->state->angVel[2]; 
					
					const shared_ptr<Sphere>& sph = YADE_PTR_CAST<Sphere>(b->shape); 
					prtData[10*i+9] = sph->radius; 
				}
				
				int sz = prtData.size();  
				MPI_Send(&prtData.front(),sz, MPI_DOUBLE, flbox->domainRank, TAG_PRT_DATA, MPI_COMM_WORLD ); 
			}
		}
	}
}


void FoamCoupling::verifyParticleDetection() {
  
	/* check if the sent particles are located on the fluid procs, verify all particles (in fluid coupling) owned by the yade process has been accounted for. Some particles may intersect the 
	 fluid domains bounding box but may not be actually inside the fluid mesh. 
	 Method : Everty fluid proc sents a vector of it's search result. if found res = 1, else res =0, for each particle. 
	 each yade rank receives this vector from intersecting fluid ranks, looks through the vector to find the fails. 
	 if fail is found : see if this id is a sharedid. if not this particle has been 'lost'. if shared id :  check the vector of verifyTracking of the intersecting fluid domain till found in 
	 at least one intersecting fluid box. if not particle has been lost. */ 
	
	//std::map<int, std::vector<int> > verifyTracking;  //vector containing domainRank, vector of "found/misses" for each body, miss = 0, found = 1 (we cannot use bool in MPI  ).  
	std::vector<std::pair<int, std::vector<int>> > verifyTracking; 
	for (unsigned int  f=0; f != fluidDomains.size(); ++f) {
		const shared_ptr<Body>& flbdy  = (*scene->bodies)[fluidDomains[f]]; 
		if (flbdy){
			const shared_ptr<FluidDomainBbox>& flbox = YADE_PTR_CAST<FluidDomainBbox>(flbdy->shape);
			std::vector<int> vt(flbox->bIds.size(), -1); 
			verifyTracking.push_back(std::make_pair(flbox->domainRank, std::move(vt))); 
		}
	}
	
	// recv the vec. 
	for (auto& it : verifyTracking){
		std::vector<int>& vt = it.second; 
		int rnk = it.first; 
		MPI_Status status; 
		int buffSz = vt.size(); 
		MPI_Recv(&vt.front(), buffSz, MPI_INT, rnk , TAG_SEARCH_RES, MPI_COMM_WORLD, &status); 
		
	}
	
	
	//check for misses 
	
	std::map<Body::id_t, bool> unFoundSharedIds; 
	//std::vector<std::pair<int, std::map<int,int>> > unFoundSharedIds; 
	
	for (const auto& vt : verifyTracking){
		const int& flBdyIndx = abs(vt.first-stride); 
		const shared_ptr<FluidDomainBbox>& flbody = YADE_PTR_CAST<FluidDomainBbox>((*scene->bodies)[fluidDomains[flBdyIndx]]->shape); 
		int bIndx = 0; 
		for (const auto& val : vt.second){
			if (val < 0) {
				// this body was not found in the fluid domain. 
				const Body::id_t&  testId = (*scene->bodies)[flbody->bIds[bIndx]]->id;
				// check if this body is a sharedId  from sharedIdsMap. 
				int sharedIndx = ifSharedIdMap(testId); 
				if(sharedIndx < 0) {
					const Vector3r& pos = (*scene->bodies)[testId]->state->pos; 
					LOG_ERROR("Particle ID  = " << testId << " pos = " << pos[0] << " " << pos[1] << " " << pos[2] <<  " was not found in fluid domain");
				}
			}
			++bIndx; 
		}
	} 
	
	// check if the 'sharedIds' has been located in any of the fluid procs. 
	
	for (const auto& idPair : sharedIdsMapIndx){
		const auto& bodyId = idPair.first; 
		const int& mpSz = idPair.second.size(); 
		int count = 0;  bool found = false; 
		for (const auto& fdIndx : idPair.second){
			for (unsigned int j =0; j != verifyTracking.size(); ++j){
				if (fdIndx.first == abs(verifyTracking[j].first-stride)){
					const std::vector<int>& vtVec = verifyTracking[j].second; 
					if (vtVec[fdIndx.second] > 0) {found = true; }
				}
			}
			++count; 
		}
		if ( (count == mpSz) && (!found) ) {
			const Vector3r& pos = (*scene->bodies)[bodyId]->state->pos; 
			LOG_ERROR("Particle ID  = " << bodyId << " pos = " << pos[0] << " " << pos[1] << " " << pos[2] <<  " was not found in fluid domain");
		}
	}
	
}

void FoamCoupling::getParticleForce(){
	
	//clear previous hForce vec. 
	hForce.clear(); 
	
	for (const auto& fdId : fluidDomains){
		const shared_ptr<Body>& flbdy  = (*scene->bodies)[fdId]; 
		if (flbdy){
			const shared_ptr<FluidDomainBbox>& flbox  = YADE_PTR_CAST<FluidDomainBbox>(flbdy->shape); 
			std::vector<double> forceVec(6*flbox->bIds.size(), 0.0); 
			hForce.push_back(std::make_pair(flbox->domainRank, std::move(forceVec))); 
		}
	}
	for (auto& recvForce : hForce){
		 std::vector<double>& tmpForce = recvForce.second; 
		 int recvRank = recvForce.first; 
		 int buffSz = tmpForce.size(); 
		 MPI_Status status; 
		 /* fluid procs having no particles will send 0 force, torque */
		 MPI_Recv(&tmpForce.front(),buffSz, MPI_DOUBLE, recvRank, TAG_FORCE, MPI_COMM_WORLD, &status); 
	}
	

}

void FoamCoupling::resetCommunications(){
	// clear the vector ids held fluidDomainBbox->bIds 
	for (unsigned f = 0; f != fluidDomains.size(); ++f) {
		const shared_ptr<Body>& fdomain = (*scene->bodies)[fluidDomains[f]]; 
		if (fdomain){
			const shared_ptr<FluidDomainBbox>& fluidBox = YADE_PTR_CAST<FluidDomainBbox>(fdomain->shape); 
			fluidBox->bIds.clear(); 
		}
	}
	
	sharedIdsMapIndx.clear(); 
	//hForce.clear(); 
}


void FoamCoupling::setParticleForceParallel(){
 	// add the force  
	
	for (const auto& rf : hForce){
		int indx = abs(rf.first-commSzdff); 
		const std::vector<double>& forceVec = rf.second; 
		const shared_ptr<FluidDomainBbox>& flbox = YADE_PTR_CAST<FluidDomainBbox>((*scene->bodies)[fluidDomains[indx]]->shape);
		for (unsigned int i=0; i != flbox->bIds.size(); ++i){
			Vector3r fx; fx[0] = forceVec[6*i]; fx[1] = forceVec[6*i+1]; fx[2] = forceVec[6*i+2]; 
			Vector3r tx; tx[0] = forceVec[6*i+3]; tx[1] = forceVec[6*i+4]; tx[2] = forceVec[6*i+5]; 
			scene->forces.addForce(flbox->bIds[i], fx); 
			scene->forces.addTorque(flbox->bIds[i], tx); 
		}
	}
	
}

void FoamCoupling::exchangeDeltaTParallel() {

	// Recv foamdt  first and broadcast;
	
	if (localRank == yadeMaster){
		MPI_Status status; 
		int fluidMaster = stride;
		MPI_Recv(&foamDeltaT,1,MPI_DOUBLE,fluidMaster,TAG_FLUID_DT,MPI_COMM_WORLD,&status);
	}
	
	//bcast  the fluidDt to all yade_procs. 
	// Real  yadeDt = scene-> dt;
	MPI_Bcast(&foamDeltaT,1,MPI_DOUBLE, yadeMaster, scene->mpiComm);
	
	//do a MPI_Allreduce (min) and get the minDt of all the yade procs.. 
	Real myDt = scene->dt; Real yadeDt;
	MPI_Allreduce(&myDt,&yadeDt,1, MPI_DOUBLE,MPI_MIN,scene->mpiComm);  
	
	
	// send the minDt to fluid proc (master .. ) 
	if (localRank == yadeMaster) {
		int fluidMaster = stride; 
		MPI_Send(&yadeDt, 1, MPI_DOUBLE, fluidMaster, TAG_YADE_DT, MPI_COMM_WORLD);  
	}
	  
	// calculate the interval . TODO: to include hydrodynamic time scale if inertial in openfoam
	// here -> hDeltaT = getViscousTimeScale();
	dataExchangeInterval = (long int) ((yadeDt < foamDeltaT) ? foamDeltaT/yadeDt : 1);  

}


void FoamCoupling::runCouplingParallel(){
	if (!commSizeSet){
		getFluidDomainBbox(); // recieve the bbox of the fluid mesh,  move this from here. 
	}
	  	
	if (localRank > 0) {
		buildSharedIdsMap(); 
		sendIntersectionToFluidProcs(); 
		sendBodyData(); 
		verifyParticleDetection(); 
		getParticleForce(); 
		resetCommunications(); 
		setParticleForceParallel();   
	}
}


void FoamCoupling::buildLocalIds(){
	
	if (bodyList.size() == 0) { LOG_ERROR("Ids for coupling has no been set, FAIL!"); return;   } 
	const shared_ptr<Subdomain> subD =  YADE_PTR_CAST<Subdomain>((*scene->bodies)[scene->thisSubdomainId]->shape); 
	if (! subD) {LOG_ERROR("subdomain not found"); return; }  
	for (const auto& testId : bodyList) {
		std::vector<Body::id_t>::iterator iter = std::find(subD->ids.begin(), subD->ids.end(), testId); 
		if (iter != subD->ids.end()){
			localIds.push_back(*iter); 
		}
	}
	
}

void FoamCoupling::killMPI() { 
	castTerminate(); 
	MPI_Finalize();

}


<<<<<<< 5e2d19c4894e3b4a39f1e0ddba7f9c18766a55b2
}

=======
>>>>>>> some fixes and additions : get dt from fluid procs, build local list of ids from global input of ids list
#endif
