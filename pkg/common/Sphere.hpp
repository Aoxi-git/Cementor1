#pragma once
#include<core/Shape.hpp>

// FIXME - move these headers into single place. Current duplicates: customConverters, _testCppPy
#if YADE_REAL_BIT>=128
#include <boost/cstdfloat.hpp>
#include <boost/math/cstdfloat/cstdfloat_types.hpp>
#include <boost/multiprecision/float128.hpp>
#endif

#ifdef YADE_MPFR
#include <boost/multiprecision/mpfr.hpp>
#endif

// FIXED: add namespace yade, see https://gitlab.com/yade-dev/trunk/issues/57 and (old site, fixed bug) https://bugs.launchpad.net/yade/+bug/528509

namespace yade {

class Sphere: public Shape{
	public:
		Sphere(Real _radius): radius(_radius){ createIndex(); }
		virtual ~Sphere () {};
	// clang-format off
	YADE_CLASS_BASE_DOC_ATTRS_CTOR(Sphere,Shape,"Geometry of spherical particle.",
		((Real,radius,NaN,,"Radius [m]"))
		((boost::float128_t,test128,NaN,,"Test 128"))
		,
		createIndex(); /*ctor*/
	);
	// clang-format on
	REGISTER_CLASS_INDEX(Sphere,Shape);
};

REGISTER_SERIALIZABLE(Sphere);

}; // namespace yade

