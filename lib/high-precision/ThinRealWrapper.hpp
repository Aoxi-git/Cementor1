/*************************************************************************
*  2019 Janek Kozicki                                                    *
*                                                                        *
*  This program is free software; it is licensed under the terms of the  *
*  GNU General Public License v2 or later. See file LICENSE for details. *
*************************************************************************/

// This file is mainly to workaround the fact that boost::python has a built-in converter for long double which is losing 3 digits of precision.
//      see: https://www.boost.org/doc/libs/1_71_0/boost/python/converter/builtin_converters.hpp
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

namespace yade {

template <typename T> struct RealPart {
	typedef T type;
};
template <typename T> struct RealPart<std::complex<T>> {
	typedef T type;
};

// TODO: rename this class, it supports Complex now.
template <typename WrappedReal>
#ifdef YADE_IGNORE_IEEE_INFINITY_NAN
class ThinRealWrapper
        : boost::ordered_field_operators1<ThinRealWrapper<WrappedReal>, boost::ordered_field_operators2<ThinRealWrapper<WrappedReal>, WrappedReal>> {
#else
class ThinRealWrapper : boost::partially_ordered1<
                                ThinRealWrapper<WrappedReal>,
                                boost::partially_ordered2<ThinRealWrapper<WrappedReal>, WrappedReal, boost::field_operators1<ThinRealWrapper<WrappedReal>>>> {
#endif
private:
	WrappedReal val;

	// detect types which are convertible to WrappedReal
	static constexpr bool IsComplex                                = boost::is_complex<WrappedReal>::value;
	using NonComplex                                               = typename RealPart<WrappedReal>::type;
	template <typename OtherType> using EnableIfConvertible        = std::enable_if_t<std::is_convertible<OtherType, WrappedReal>::value>;
	template <typename OtherType> using EnableIfNonComplex         = std::enable_if_t<(not boost::is_complex<OtherType>::value)>;
	template <typename OtherType> using EnableIfIsComplex          = std::enable_if_t<boost::is_complex<OtherType>::value>;
	template <typename OtherType> using EnableIfComplexConvertible = std::enable_if_t<IsComplex and (std::is_convertible<OtherType, NonComplex>::value)>;

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

	// move/copy constructor from OtherType which is_convertible o WrappedReal
	template <typename OtherType, typename = EnableIfConvertible<OtherType>>
	ThinRealWrapper(OtherType&& moveVal) BOOST_NOEXCEPT_IF(boost::has_nothrow_move<WrappedReal>::value)
	        : val(std::forward<OtherType>(moveVal))
	{
	}

	// move/copy assignment from OtherType which is_convertible o WrappedReal
	template <typename OtherType, typename = EnableIfConvertible<OtherType>>
	ThinRealWrapper& operator=(OtherType&& moveVal) BOOST_NOEXCEPT_IF(boost::has_nothrow_move<WrappedReal>::value)
	{
		val = std::forward<OtherType>(moveVal);
		return *this;
	}

	// conversion operator to other types
	template <typename OtherType, typename = EnableIfConvertible<OtherType>> explicit operator OtherType() const { return static_cast<OtherType>(val); }
	explicit operator const WrappedReal&() const { return val; }

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
	template <typename OtherType = WrappedReal, typename boost::disable_if<boost::is_complex<OtherType>, int>::type = 0>
	void check(const ThinRealWrapper& rhs) const
	{
		if (std::isnan(rhs.val) or std::isnan(val) or std::isinf(rhs.val) or std::isinf(val)) {
			throw std::runtime_error("cannot compare NaN, Inf numbers.");
		}
	}
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
#endif

	// Output/ Input
	friend inline std::ostream& operator<<(std::ostream& os, const ThinRealWrapper& v) { return os << v.val; }
	friend inline std::istream& operator>>(std::istream& is, ThinRealWrapper& v)
	{
		is >> v.val;
		return is;
	}


	// support Complex numbers - start
	using value_type = ThinRealWrapper<NonComplex>;
	// constructor from two ThinRealWrapper arguments
	template <typename OtherType, typename = EnableIfComplexConvertible<OtherType>>
	ThinRealWrapper(const ThinRealWrapper<OtherType>& v1, const ThinRealWrapper<OtherType>& v2)
	        BOOST_NOEXCEPT_IF(boost::has_nothrow_move<WrappedReal>::value)
	        : val(static_cast<OtherType>(v1), static_cast<OtherType>(v2))
	{
	}
	// move/copy constructor from two arguments
	template <typename OtherType, typename = EnableIfComplexConvertible<OtherType>>
	ThinRealWrapper(OtherType&& moveVal_1, OtherType&& moveVal_2) BOOST_NOEXCEPT_IF(boost::has_nothrow_move<WrappedReal>::value)
	        : val(std::forward<OtherType>(moveVal_1), std::forward<OtherType>(moveVal_2))
	{
	}
	template <typename OtherType = WrappedReal, typename = EnableIfIsComplex<OtherType>> NonComplex real() const { return val.real(); };
	template <typename OtherType = WrappedReal, typename = EnableIfIsComplex<OtherType>> NonComplex imag() const { return val.imag(); };
	template <typename OtherType, typename = EnableIfNonComplex<OtherType>> operator ThinRealWrapper<std::complex<OtherType>>() const
	{
		return ThinRealWrapper<std::complex<OtherType>>(val, static_cast<const OtherType&>(0));
	}
#ifndef YADE_IGNORE_IEEE_INFINITY_NAN
	template <typename OtherType = WrappedReal, typename boost::enable_if<boost::is_complex<OtherType>, int>::type = 0>
	void check(const ThinRealWrapper& rhs) const
	{
		if (std::isnan(rhs.val.real()) or std::isnan(val.real()) or std::isinf(rhs.val.real()) or std::isinf(val.real()) or std::isnan(rhs.val.imag())
		    or std::isnan(val.imag()) or std::isinf(rhs.val.imag()) or std::isinf(val.imag())) {
			throw std::runtime_error("cannot compare NaN, Inf numbers.");
		}
	}
#endif
	// support Complex numbers - end

};

}

#endif

