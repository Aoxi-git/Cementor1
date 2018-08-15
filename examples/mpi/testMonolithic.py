#run  'python testMonolithic.py'
#     'python testMonolithic.py N M'
# for reference time to be compared to testMPIxN.py
import sys
import time
from os.path import expanduser
sys.path.append(expanduser('~')+'/yade/bin') # path where you have yadeimport.py
# yadeimport.py is generated by `ln yade-versionNo yadeimport.py`, where yade-versionNo is the yade executable
print expanduser('~')+'/yade/bin'
#from mpi4py import MPI

'''
This script is meant to be executed non-parallel, for comparison with the MPI flavor

'''

NSTEPS=100 #turn it >0 to see time iterations, else only initilization
VERBOSE_OUTPUT=False


#tags for mpi messages
_SCENE_=11
_SUBDOMAINSIZE_=12
_INTERSECTION_=13
#_STATE_SHAPE_
_ID_STATE_SHAPE_=14
_FORCES_=15

#for coloring processes outputs differently
class bcolors:#https://stackoverflow.com/questions/287871/print-in-terminal-with-colors
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

#print bcolors.WARNING + "Warning: No active frommets remain. Continue?"+ bcolors.ENDC

def wprint(m):
	if not VERBOSE_OUTPUT: return
	print bcolors.HEADER+"master:" +m+bcolors.ENDC
	
def mprint(m): #this one will print regardless of VERBOSE_OUTPUT
	print bcolors.HEADER+"master:" +m+bcolors.ENDC



#based on "O" some of what follows looks as usual to yade users
from yadeimport import *
from yadeimport import O,sphere,box
#O=yade.Omega()

#RULES:
	#- intersections[0] has 0-bodies (to which we need to send force)
	#- intersections[thisDomain] has ids of the other domains overlapping the current ones
	#- intersections[otherDomain] has ids of bodies in _current_ domain which are overlapping with other domain (for which we need to send updated pos/vel)

#def initMPI(subD):
	##set intersections and list of 
	
	#thisSubdomain=O._sceneObj.subdomain
	#print "worker 1 got subdomain ",thisSubdomain
	
	#sub1=[b.id for b in O.bodies if b.subdomain==thisSubdomain]
	#print "worker 1= sub1=",sub1
	##retrieve the subdomain and unbound external bodies (intersecting ones revided later)
	#domainBody1=None
	#for b in O.bodies:
		#if b.subdomain != thisSubdomain:
			#b.bounded=False
			#b.bound=None
		#if isinstance(b.shape,yade.Subdomain) and b.shape.subDomainIndex==thisSubdomain: domainBody1=b
			
		##print b.shape, yade.Subdomain, b.subdomain, thisSubdomain, domainBody1
	#if domainBody1==None: print "SUBDOMAIN NOT FOUND FOR RANK=",rank
	#subD = domainBody1.shape
	#subD.intersections=[[]]*MAX_SUBDOMAIN
	


if 1: #this is the master process
	##### DEFINE A SCENE #########
	newton.gravity=(0,-10,0) #else nothing would move
	O.dynDt=0
	timeStepper.dead=True
	O.dt=0.002 #very important, we don't want subdomains to use many different timesteps...

	if len(sys.argv)>1: #we then assume N,M are provided as 1st and 2nd cmd line arguments
		N=int(sys.argv[0]); M=int(sys.argv[1])
	else:
		N=900; M=100; #(columns, rows)
	for i in range(N): #NxM spheres
		for j in range(M):
			id=yade.Omega().bodies.append(sphere((i+j/3000.,j,0),0.5))
	WALL_ID=O.bodies.append(box(center=(0,-0.5,0),extents=(10000,0,10000),fixed=True))
	
	collider.boundDispatcher.functors=collider.boundDispatcher.functors+[yade.Bo1_Subdomain_Aabb()]
	collider.verletDist = 0.3 
	
	#BEGIN Garbage (should go to some init(), usually done in collider.__call__() but in the mpi case we want to collider.boundDispatcher.__call__() before collider.__call__()
	collider.boundDispatcher.sweepDist=collider.verletDist;
	collider.boundDispatcher.minSweepDistFactor=collider.minSweepDistFactor;
	collider.boundDispatcher.targetInterv=collider.targetInterv;
	collider.boundDispatcher.updatingDispFactor=collider.updatingDispFactor;
	#collider.fastestBodyMaxDist=0.0000000001
	#END Garbage
	
	##### RUN MPI #########
	O.step()
	start=time.time()
	cppRun=False
	if not cppRun:
		for iter in range(NSTEPS): O.step()
		#for iter in range(NSTEPS): O.run(1,1)
	else:
		O.run(NSTEPS,1)
	totTime = (time.time()-start)
	
	mass=0
	for b in O.bodies: mass+=b.state.mass
	mprint ( "TOTAL FORCE ON FLOOR="+str(O.forces.f(WALL_ID)[1])+" vs. total weight "+str(mass*10) )
	mprint ( "monolithic (cppRun="+str(cppRun)+"): done "+str(len(O.bodies)-1)+" bodies in "+str(totTime)+ " sec, at "+str(N*M*NSTEPS/totTime)+" body*iter/second")
	mprint ( "num. collision detection "+str(collider.numAction))
    
