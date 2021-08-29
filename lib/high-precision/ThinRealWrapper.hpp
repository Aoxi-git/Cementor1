/*************************************************************************
*  2019 Janek Kozicki                                                    *
*                                                                        *
*  This program is free software; it is licensed under the terms of the  *
*  GNU General Public License v2 or later. See file LICENSE for details. *
*************************************************************************/

// The purpose of this class is twofold
// 1. To catch all improper uses od Real type via compiler errors - this is possible thanks to careful design of all conversion operators.
//    It makes it much simpler to reach the real target: boost::multiprecision::float128, because the compiler errors generated by ThinRealWrapper
//    are much more informative than respective compiler errors due the same mistakes generated by boost::multiprecision::float128.
//
// 2. Second equally important is to workaround boost precision error, so that the tests py/tests/testMath.py would work exactly the same as
//    for all other Real types. It is better to fix long double, than to add a special workaround in tests which would allow loss of last 3 digits of precision :)
//    see: https://www.boost.org/doc/libs/1_71_0/boost/python/converter/builtin_converters.hpp
//
// It will be useful later in general if we will need some special traits taken care of for Real type.

#ifndef YADE_THIN_REAL_WRAPPER_HPP
#define YADE_THIN_REAL_WRAPPER_HPP

#include <boost/config.hpp>
#include <boost/move/traits.hpp>
#include <boost/operators.hpp>
#include <boost/type_traits/conditional.hpp>
#include <boost/type_traits/has_nothrow_assign.hpp>
#include <boost/type_traits/has_nothrow_constructor.hpp>
#include <boost/type_traits/has_nothrow_copy.hpp>
#include <boost/type_traits/is_complex.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

// The implementation is based on https://www.boost.org/doc/libs/1_71_0/libs/utility/operators.htm
// so that all operators =,+,-,*,/,>,<,==,!= etc on Real are predefined using boost.
// It is tested with https://www.boost.org/doc/libs/1_72_0/libs/math/doc/html/math_toolkit/real_concepts.html
//  #include <boost/concept/assert.hpp>
//  #include <boost/math/concepts/real_type_concept.hpp>
//  int main() { BOOST_CONCEPT_ASSERT((boost::math::concepts::RealTypeConcept<Real>)); }
//
// see also: /usr/include/boost/math/bindings/e_float.hpp
//           /usr/include/mpreal.h
//           https://www.boost.org/doc/libs/1_72_0/libs/math/test/e_float_concept_check.cpp

// it is possible to #define YADE_IGNORE_IEEE_INFINITY_NAN  ← about that see https://www.boost.org/doc/libs/1_71_0/libs/utility/operators.htm#ordering


// RealHP<…> note:
//   If RealHP<N> is supported then ThinRealWrapper needs explicit conversion operators to all other higher precision types, so provide them here.
//   These include headers are necessary to make it work.
//   If other types (e.g. boost quad_double) appear, they will have to be added here.
#ifndef YADE_DISABLE_REAL_MULTI_HP
#ifdef YADE_MPFR
#include <boost/multiprecision/mpfr.hpp>
#else
#include <boost/multiprecision/cpp_bin_float.hpp>
#endif
// clang currently does not support float128   https://github.com/boostorg/math/issues/181
// another similar include is in RealHP.hpp, all other checks if we have float128 should be #ifdef BOOST_MP_FLOAT128_HPP or yade::math::isFloat128<T>
#ifndef __clang__
#include <boost/multiprecision/float128.hpp>
#endif
#endif

namespace yade {
namespace math {
	template <typename WrappedReal>
#ifdef YADE_IGNORE_IEEE_INFINITY_NAN
	class ThinRealWrapper
	        : boost::ordered_field_operators1<ThinRealWrapper<WrappedReal>, boost::ordered_field_operators2<ThinRealWrapper<WrappedReal>, WrappedReal>> {
#else
	class ThinRealWrapper
	        : boost::partially_ordered1<
	                  ThinRealWrapper<WrappedReal>,
	                  boost::partially_ordered2<ThinRealWrapper<WrappedReal>, WrappedReal, boost::field_operators1<ThinRealWrapper<WrappedReal>>>> {
#endif
	private:
		WrappedReal val;

		// detect types which are convertible to WrappedReal
		template <typename OtherType> using EnableIfConvertible = std::enable_if_t<std::is_convertible<OtherType, WrappedReal>::value>;

		static_assert(boost::is_complex<WrappedReal>::value == false, "WrappedReal cannot be complex");

	public:
		// default constructor
		ThinRealWrapper() BOOST_NOEXCEPT_IF(boost::has_nothrow_default_constructor<WrappedReal>::value) = default;
		// copy constructor
		ThinRealWrapper(const ThinRealWrapper& initVal) BOOST_NOEXCEPT_IF(boost::has_nothrow_copy_constructor<WrappedReal>::value) = default;
		// copy assignment operator
		ThinRealWrapper& operator=(const ThinRealWrapper& rhs) BOOST_NOEXCEPT_IF(boost::has_nothrow_assign<WrappedReal>::value) = default;
		// move constructor
		ThinRealWrapper(ThinRealWrapper&& moveVal) BOOST_NOEXCEPT_IF(boost::has_nothrow_move<WrappedReal>::value) = default;
		// move assignment operator
		ThinRealWrapper& operator=(ThinRealWrapper&& moveVal) BOOST_NOEXCEPT_IF(boost::has_nothrow_move<WrappedReal>::value) = default;
		// destructor
		~ThinRealWrapper() noexcept = default;

		// NOTE: copy and assignment constructors are implemened below as templated move/copy constructors.

		// move/copy constructor from OtherType which is_convertible to WrappedReal
		template <typename OtherType, typename = EnableIfConvertible<OtherType>>
		constexpr ThinRealWrapper(OtherType&& moveVal) BOOST_NOEXCEPT_IF(boost::has_nothrow_move<WrappedReal>::value)
		        : val(static_cast<WrappedReal>(std::forward<OtherType>(moveVal)))
		{
		}

		// move/copy assignment from OtherType which is_convertible to WrappedReal
		template <typename OtherType, typename = EnableIfConvertible<OtherType>>
		ThinRealWrapper& operator=(OtherType&& moveVal) BOOST_NOEXCEPT_IF(boost::has_nothrow_move<WrappedReal>::value)
		{
			val = std::forward<OtherType>(moveVal);
			return *this;
		}

		// conversion operator to other types
		template <typename OtherType, typename = EnableIfConvertible<OtherType>> explicit operator OtherType() const
		{
			return static_cast<OtherType>(val);
		}
// If RealHP<N> is supported then ThinRealWrapper needs explicit conversion operators to all other higher precision types, so provide them here.
#ifndef YADE_DISABLE_REAL_MULTI_HP
#ifdef YADE_MPFR
		template <unsigned int Dec> using HPBackend = boost::multiprecision::mpfr_float_backend<Dec>;
#else
		template <unsigned int Dec> using HPBackend = boost::multiprecision::cpp_bin_float<Dec>;
#endif
		template <unsigned int Num> explicit operator ::boost::multiprecision::number<HPBackend<Num>, boost::multiprecision::et_off>() const
		{
			return static_cast<::boost::multiprecision::number<HPBackend<Num>, boost::multiprecision::et_off>>(val);
		}
#ifdef BOOST_MP_FLOAT128_HPP
		explicit operator ::boost::multiprecision::float128() const { return static_cast<::boost::multiprecision::float128>(val); }
#endif
		template <unsigned int Num>
		ThinRealWrapper(const ::boost::multiprecision::number<HPBackend<Num>, boost::multiprecision::et_off>& otherVal)
		        : val(static_cast<WrappedReal>(otherVal))
		{
		}
#ifdef BOOST_MP_FLOAT128_HPP
		ThinRealWrapper(const ::boost::multiprecision::float128& otherVal)
		        : val(static_cast<WrappedReal>(otherVal))
		{
		}
#endif
#endif
		explicit operator const WrappedReal&() const { return val; }
		explicit operator WrappedReal&() { return val; }

		// https://en.cppreference.com/w/cpp/language/cast_operator
		explicit operator WrappedReal*() { return &val; };
		explicit operator const WrappedReal*() const { return &val; };

		// field operators
		ThinRealWrapper& operator+=(const ThinRealWrapper& x)
		{
			val += x.val;
			return *this;
		}
		ThinRealWrapper& operator-=(const ThinRealWrapper& x)
		{
			val -= x.val;
			return *this;
		}
		ThinRealWrapper& operator*=(const ThinRealWrapper& x)
		{
			val *= x.val;
			return *this;
		}
		ThinRealWrapper& operator/=(const ThinRealWrapper& x)
		{
			val /= x.val;
			return *this;
		}
		const ThinRealWrapper  operator-() const { return -val; }
		const ThinRealWrapper& operator+() const { return *this; }

		// ordering operators
		bool operator<(const ThinRealWrapper& rhs) const { return val < rhs.val; }
#ifdef YADE_IGNORE_IEEE_INFINITY_NAN
		bool operator==(const ThinRealWrapper& rhs) const { return val == rhs.val; }
#else
		bool operator==(const ThinRealWrapper& rhs) const
		{
			check(rhs);
			return val == rhs.val;
		}
		bool operator!=(const ThinRealWrapper& rhs) const
		{
			check(rhs);
			return val != rhs.val;
		}

		template <typename OtherType, typename = EnableIfConvertible<OtherType>> friend inline bool operator==(OtherType rhs, const ThinRealWrapper& th)
		{
			th.check(rhs);
			return th.val == rhs;
		}
		template <typename OtherType, typename = EnableIfConvertible<OtherType>> friend inline bool operator!=(OtherType rhs, const ThinRealWrapper& th)
		{
			th.check(rhs);
			return th.val != rhs;
		}
#ifdef YADE_WRAPPER_THROW_ON_NAN_INF_REAL
		// this can be useful sometimes for debugging when calculations go all wrong.
		void check(const ThinRealWrapper& rhs) const
		{
			// boost::partially_ordered takes into account that some numbers cannot be compared with each other
			if (std::isnan(rhs.val) or std::isnan(val) or std::isinf(rhs.val) or std::isinf(val)) {
				throw std::runtime_error("cannot compare NaN, Inf numbers.");
			}
		}
#else
		// this one is optimized away
		void check(const ThinRealWrapper&) const {};
#endif
#endif

		// Output/ Input
		friend inline std::ostream& operator<<(std::ostream& os, const ThinRealWrapper& v) { return os << v.val; }
		friend inline std::istream& operator>>(std::istream& is, ThinRealWrapper& v)
		{
			is >> v.val;
			return is;
		}
	};

	static_assert(
	        boost::is_complex<ThinRealWrapper<UnderlyingReal>>::value == false,
	        "ThinRealWrapper<UnderlyingReal> is not recognized by boost::is_complex, please report a bug.");

} // namespace math
} // namespace yade

#endif
