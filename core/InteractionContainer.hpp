// 2004 © Olivier Galizzi <olivier.galizzi@imag.fr>
// 2004 © Janek Kozicki <cosurgi@berlios.de>
// 2010 © Václav Šmilauer <eudoxos@arcig.cz>

#pragma once

#include <lib/serialization/Serializable.hpp>

#ifdef YADE_OPENMP
#include <omp.h>
#endif

#include <core/BodyContainer.hpp>
#include <core/Interaction.hpp>

namespace yade { // Cannot have #include directive inside.

/* This InteractionContainer implementation has reference to the body container and
stores interactions in 2 places:

* Internally in a std::vector; that allows for const-time linear traversal.
  Each interaction internally holds back-reference to the position in this container in Interaction::linIx.
* Inside Body::intrs (in the body with min(id1,id2)).

Both must be kep in sync, which is handled by insert & erase methods.

It was originally written by 2008 © Sergei Dorofeenko <sega@users.berlios.de>,
later devirtualized and put here.

Alternative implementations of InteractionContainer should implement the same API. Due to performance
reasons, no base class with virtual methods defining such API programatically is defined (it could
be possible to create class template for this, though).

Future (?):

* the linear vector might be removed; in favor of linear traversal of bodies by their subdomains,
  then traversing the map in each body. If the previous point would come to realization, half of the
  interactions would have to be skipped explicitly in such a case.

*/
class InteractionContainer : public Serializable {
private:
	typedef vector<shared_ptr<Interaction>> ContainerT;
	// linear array of container interactions
	ContainerT linIntrs;
	// same array that can be sorted with updateSortedIntrs()
	ContainerT sortedIntrs;
	// allow interaction loop to directly access the above vectors
	friend class InteractionLoop;
	// pointer to body container, since each body holds (some) interactions
	// this must always point to scene->bodies->body
	const BodyContainer::ContainerT* bodies;
	// always in sync with intrs.size(), to avoid that function call
	size_t                  currSize;
	shared_ptr<Interaction> empty;
	// used only during serialization/deserialization
	vector<shared_ptr<Interaction>> interaction;

public:
	// flag for notifying the collider that persistent data should be invalidated
	bool dirty;
	// required by the class factory... :-|
	InteractionContainer()
	        : currSize(0)
	        , dirty(false)
	        , serializeSorted(false)
	        , iterColliderLastRun(-1)
	{
		bodies = NULL;
	}
	void clear();
	// iterators
	typedef ContainerT::iterator       iterator;
	typedef ContainerT::const_iterator const_iterator;
	iterator                           begin() { return linIntrs.begin(); }
	iterator                           end() { return linIntrs.end(); }
	const_iterator                     begin() const { return linIntrs.begin(); }
	const_iterator                     end() const { return linIntrs.end(); }
	// insertion/deletion
	bool insert(Body::id_t id1, Body::id_t id2);
	bool insert(const shared_ptr<Interaction>& i);
	//3rd parameter is used to remove I from linIntrs (in conditionalyEraseNonReal()) when body b1 has been removed
	bool                           erase(Body::id_t id1, Body::id_t id2, int linPos = -1);
	bool                           insertInteractionMPI(shared_ptr<Interaction>&);
	const shared_ptr<Interaction>& find(Body::id_t id1, Body::id_t id2);
	inline bool                    found(const Body::id_t& id1, const Body::id_t& id2)
	{
		assert(bodies);
		if (id2 >= (Body::id_t)bodies->size() or (id1 == id2)) {
			return false;
		} else {
			if (id1 > id2) {
				return (*bodies)[id2]->intrs.count(id1);
			} else {
				return (*bodies)[id1]->intrs.count(id2);
			}
		}
	}
	// index access
	shared_ptr<Interaction>&       operator[](size_t id) { return linIntrs[id]; }
	const shared_ptr<Interaction>& operator[](size_t id) const { return linIntrs[id]; }
	size_t                         size() { return currSize; }
	// simulation API

	//! Erase all non-real (in term of Interaction::isReal()) interactions
	void eraseNonReal();

	// mutual exclusion to avoid crashes in the rendering loop
	std::mutex drawloopmutex;
	// sort interactions before serializations; useful if comparing XML files from different runs (false by default)
	bool serializeSorted;
	// iteration number when the collider was last run; set by the collider, if it wants interactions that were not encoutered in that step to be deleted by InteractionLoop (such as SpatialQuickSortCollider). Other colliders (such as InsertionSortCollider) set it it -1, which is the default
	long iterColliderLastRun;
	//! Ask for erasing the interaction given (from the constitutive law); this resets the interaction (to the initial=potential state) and collider should traverse potential interactions to decide whether to delete them completely or keep them potential
	void requestErase(Body::id_t id1, Body::id_t id2);
	void requestErase(const shared_ptr<Interaction>& I);
	void requestErase(Interaction* I);

	/*! Traverse all interactions and erase them if they are not real and the (T*)->shouldBeErased(id1,id2) return true, or if body(id1) has been deleted
			Class using this interface (which is presumably a collider) must define the
				bool shouldBeErased(Body::id_t, Body::id_t) const
		*/
	template <class T> size_t conditionalyEraseNonReal(const T& t, Scene* rb)
	{
// beware iterators here, since erase is invalidating them. We need to iterate carefully, and keep in mind that erasing one interaction is moving the last one to the current position.
// For the parallel flavor we build the list to be erased in parallel, then it is erased sequentially. Still significant speedup since checking bounds is the most expensive part.
#ifdef YADE_OPENMP
		if (omp_get_max_threads() <= 1) {
#endif
			size_t initSize = currSize;
			for (size_t linPos = 0; linPos < currSize;) {
				const shared_ptr<Interaction>& i = linIntrs[linPos];
				if (!i->isReal() && t.shouldBeErased(i->getId1(), i->getId2(), rb)) erase(i->getId1(), i->getId2(), linPos);
				else
					linPos++;
			}
			return initSize - currSize;
#ifdef YADE_OPENMP
		} else {
			unsigned nThreads = omp_get_max_threads();
			assert(nThreads > 0);
			std::vector<std::vector<Vector3i>> toErase;
			toErase.resize(nThreads, std::vector<Vector3i>());
			for (unsigned kk = 0; kk < nThreads; kk++)
				toErase[kk].reserve(1000); //A smarter value than 1000?
			size_t initSize = currSize;
#pragma omp parallel for schedule(static) num_threads(nThreads)
			for (size_t linPos = 0; linPos < currSize; linPos++) {
				const shared_ptr<Interaction>& i = linIntrs[linPos];
				if (!i->isReal() && t.shouldBeErased(i->getId1(), i->getId2(), rb))
					toErase[omp_get_thread_num()].push_back(Vector3i(i->getId1(), i->getId2(), linPos));
			}
			for (int kk = nThreads - 1; kk >= 0; kk--)
				for (int ii(toErase[kk].size() - 1); ii >= 0; ii--)
					erase(toErase[kk][ii][0], toErase[kk][ii][1], toErase[kk][ii][2]);
			return initSize - currSize;
		}
#endif
	}

	void        updateSortedIntrs();
	static bool compareTwoInteractions(shared_ptr<Interaction> inter1, shared_ptr<Interaction> inter2)
	{
		Body::id_t min1, max1, min2, max2;
		if (inter1->id1 < inter1->id2) {
			min1 = inter1->id1;
			max1 = inter1->id2;
		} else {
			min1 = inter1->id2;
			max1 = inter1->id1;
		}
		if (inter2->id1 < inter2->id2) {
			min2 = inter2->id1;
			max2 = inter2->id2;
		} else {
			min2 = inter2->id2;
			max2 = inter2->id1;
		}
		if (min1 < min2) return true;
		else if (min1 > min2)
			return false;
		else
			return max1 < max2; //min1==min2
	}

	// we must call Scene's ctor (and from Scene::postLoad), since we depend on the existing BodyContainer at that point.
	void postLoad__calledFromScene(const shared_ptr<BodyContainer>&);
	void preLoad(InteractionContainer&);
	void preSave(InteractionContainer&);
	void postSave(InteractionContainer&);


	REGISTER_ATTRIBUTES(Serializable, (interaction)(serializeSorted)(dirty));
	REGISTER_CLASS_AND_BASE(InteractionContainer, Serializable);
	DECLARE_LOGGER;
};
REGISTER_SERIALIZABLE(InteractionContainer);

} // namespace yade
