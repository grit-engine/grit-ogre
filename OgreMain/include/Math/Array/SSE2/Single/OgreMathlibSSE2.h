/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2013 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#ifndef __MathlibSSE2_H__
#define __MathlibSSE2_H__

#if __OGRE_HAVE_SSE

#ifndef __Mathlib_H__
	#error "Don't include this file directly. include Math/Array/OgreMathlib.h"
#endif

#include "OgrePrerequisites.h"

namespace Ogre
{
	class ArrayRadian
	{
		ArrayReal mRad;

	public:
		explicit ArrayRadian ( ArrayReal r ) : mRad( r ) {}
		//ArrayRadian ( const ArrayDegree& d );
		ArrayRadian& operator = ( const ArrayReal &f )		{ mRad = f; return *this; }
		ArrayRadian& operator = ( const ArrayRadian &r )	{ mRad = r.mRad; return *this; }
		//ArrayRadian& operator = ( const ArrayDegree& d );

		//ArrayReal valueDegrees() const; // see bottom of this file
		ArrayReal valueRadians() const						{ return mRad; }

        inline const ArrayRadian& operator + () const;
		inline ArrayRadian operator + ( const ArrayRadian& r ) const;
		//inline ArrayRadian operator + ( const ArrayDegree& d ) const;
		inline ArrayRadian& operator += ( const ArrayRadian& r );
		//inline ArrayRadian& operator += ( const ArrayDegree& d );
		inline ArrayRadian operator - () const;
		inline ArrayRadian operator - ( const ArrayRadian& r ) const;
		//inline ArrayRadian operator - ( const ArrayDegree& d ) const;
		inline ArrayRadian& operator -= ( const ArrayRadian& r );
		//inline ArrayRadian& operator -= ( const ArrayDegree& d );
		inline ArrayRadian operator * ( ArrayReal f ) const;
        inline ArrayRadian operator * ( const ArrayRadian& f ) const;
		inline ArrayRadian& operator *= ( ArrayReal f );
		inline ArrayRadian operator / ( ArrayReal f ) const;
		inline ArrayRadian& operator /= ( ArrayReal f );

		inline ArrayReal operator <  ( const ArrayRadian& r ) const;
		inline ArrayReal operator <= ( const ArrayRadian& r ) const;
		inline ArrayReal operator == ( const ArrayRadian& r ) const;
		inline ArrayReal operator != ( const ArrayRadian& r ) const;
		inline ArrayReal operator >= ( const ArrayRadian& r ) const;
		inline ArrayReal operator >  ( const ArrayRadian& r ) const;
	};

	class _OgreExport MathlibSSE2
	{
	public:
		static const ArrayReal HALF;		//0.5f, 0.5f, 0.5f, 0.5f
		static const ArrayReal ONE;			//1.0f, 1.0f, 1.0f, 1.0f
		static const ArrayReal THREE;		//3.0f, 3.0f, 3.0f, 3.0f
		static const ArrayReal NEG_ONE;		//-1.0f, -1.0f, -1.0f, -1.0f
		static const ArrayReal PI;			//PI, PI, PI, PI
		static const ArrayReal TWO_PI;		//2*PI, 2*PI, 2*PI, 2*PI
		static const ArrayReal ONE_DIV_2PI;	//1 / 2PI, 1 / 2PI, 1 / 2PI, 1 / 2PI
		static const ArrayReal fEpsilon;	//1e-6f, 1e-6f, 1e-6f, 1e-6f
		static const ArrayReal fSqEpsilon;	//1e-12f, 1e-12f, 1e-12f, 1e-12f
		static const ArrayReal OneMinusEpsilon;//1 - 1e-6f, 1 - 1e-6f, 1 - 1e-6f, 1 - 1e-6f
		static const ArrayReal fDeg2Rad;	//Math::fDeg2Rad, Math::fDeg2Rad, Math::fDeg2Rad, Math::fDeg2Rad
		static const ArrayReal fRad2Deg;	//Math::fRad2Deg, Math::fRad2Deg, Math::fRad2Deg, Math::fRad2Deg
		static const ArrayReal FLOAT_MIN;	//FLT_MIN, FLT_MIN, FLT_MIN, FLT_MIN
		static const ArrayReal SIGN_MASK;	//0x80000000, 0x80000000, 0x80000000, 0x80000000

		/** Returns the absolute values of each 4 floats
			@param
				4 floating point values
			@return
				abs( a )
		*/
		static inline ArrayReal Abs4( ArrayReal a )
		{
			return _mm_andnot_ps( _mm_set1_ps( -0.0f ), a );
		}

		/** Branchless conditional move for 4 floating point values
			@remarks
				Will NOT work if any of the arguments contains Infinite
				or NaNs or non-floating point values. If an exact binary
				copy is needed, @see CmovRobust
			@param
				4 floating point values. Can't be NaN or Inf
			@param
				4 floating point values. Can't be NaN or Inf
			@param
				4 values containing either 0 or 0xffffffff
				Any other value, the result is undefined
			@return
				r[i] = mask[i] != 0 ? arg1[i] : arg2[i]
				Another way to say it:
					if( maskCondition[i] == true )
						r[i] = arg1[i];
					else
						arg2[i];
		*/
		static inline ArrayReal Cmov4( ArrayReal arg1, ArrayReal arg2, ArrayReal mask )
		{
			assert( _mm_movemask_ps( _mm_cmpeq_ps( arg1, arg1 ) ) == 0x0f &&
					_mm_movemask_ps( _mm_cmpeq_ps( arg2, arg2 ) ) == 0x0f &&
					"Passing NaN values to CMov4" );
#ifndef  NDEBUG
			ArrayReal newNan1 = _mm_mul_ps( arg1, _mm_setzero_ps() ); //+-Inf * 0 = nan
			ArrayReal newNan2 = _mm_mul_ps( arg2, _mm_setzero_ps() ); //+-Inf * 0 = nan
			assert( _mm_movemask_ps( _mm_cmpeq_ps( newNan1, newNan1 ) ) == 0x0f &&
					_mm_movemask_ps( _mm_cmpeq_ps( newNan2, newNan2 ) ) == 0x0f &&
					"Passing +/- Infinity values to CMov4" );
#endif

			ArrayReal t = _mm_sub_ps( arg1, arg2 );				// t = arg1 - arg2
			return _mm_add_ps( arg2, _mm_and_ps( t, mask ) );	// r = arg2 + (t & mask)
		}

		/** Robust, branchless conditional move for a 128-bit value.
			@remarks
				If you're looking to copy 4 floating point values that do
				not contain Inf or Nans, @see Cmov4 which is faster.
				This is because switching between registers flagged as
				floating point to integer and back has a latency delay

				For more information refer to Chapter 3.5.2.3
				Bypass between Execution Domains, Intel� 64 and IA-32
				Architectures Optimization Reference Manual Order
				Number: 248966-026 April (and also Table 2-12)
			@param
				A value containing 128 bits
			@param
				A value containing 128 bits
			@param
				Mask, each bit is evaluated
			@return
			    For each bit:
				r[i] = mask[i] != 0 ? arg1[i] : arg2[i]
				Another way to say it:
					if( maskCondition[i] == true )
						r[i] = arg1[i];
					else
						arg2[i];
		*/
		#
		static inline __m128 CmovRobust( __m128 arg1, __m128 arg2, __m128 mask )
		{
			return _mm_or_ps( _mm_and_ps( arg1, mask ), _mm_andnot_ps( mask, arg2 ) );
		}
		static inline __m128d CmovRobust( __m128d arg1, __m128d arg2, __m128d mask )
		{
			return _mm_or_pd( _mm_and_pd( arg1, mask ), _mm_andnot_pd( mask, arg2 ) );
		}
		static inline __m128i CmovRobust( __m128i arg1, __m128i arg2, __m128i mask )
		{
			return _mm_or_si128( _mm_and_si128( arg1, mask ), _mm_andnot_si128( mask, arg2 ) );
		}

		/** Returns the result of "a & b"
		@return
			r[i] = a[i] & b[i];
		*/
		static inline __m128i And( __m128i a, __m128i b )
		{
			return _mm_and_si128( a, b );
		}

		/** Returns the result of "a & b"
		@return
			r[i] = a[i] & b;
		*/
		static inline __m128i And( __m128i a, uint32 b )
		{
			return _mm_and_si128( a, _mm_set1_epi32( b ) );
		}
		static inline __m128 And( __m128 a, uint32 b )
		{
			return _mm_and_ps( a, _mm_castsi128_ps( _mm_set1_epi32( b ) ) );
		}

		/** Test if "a AND b" will result in non-zero, returning 0xffffffff on those cases
		@remarks
			Because there is no "not-equal" instruction in integer SSE2, be need to do some
			bit flipping.
		@return
			r[i] = (a[i] & b[i]) ? 0xffffffff : 0;
		*/
		static inline __m128i TestFlags32( __m128i a, __m128i b )
		{
			// !( (a & b) == 0 ) --> ( (a & b) == 0 ) ^ -1
			return _mm_xor_si128( _mm_cmpeq_epi32( _mm_and_si128( a, b ), _mm_setzero_si128() ),
									_mm_set1_epi32( -1 ) );
		}

		/** Returns the result of "a | b"
		@return
			r[i] = a[i] | b[i];
		*/
		static inline __m128 Or( __m128 a, __m128 b )
		{
			return _mm_or_ps( a, b );
		}

		/** Returns the result of "a < b"
		@return
			r[i] = a[i] < b[i] ? 0xffffffff : 0;
		*/
		static inline __m128 CompareLess( __m128 a, __m128 b )
		{
			return _mm_cmplt_ps( a, b );
		}

		static inline ArrayReal SetAll( Real val )
		{
			return _mm_set_ps1( val );
		}

		static inline ArrayInt SetAll( uint32 val )
		{
			return _mm_set1_epi32( val );
		}

		/**	Returns the reciprocal of x
			@remarks
				If you have a very rough guarantees that you won't be feeding a zero,
				consider using @see InvNonZero4 because it's faster.

				Use SSE Newton-Raphson reciprocal estimate, accurate to 23 significant
				bits of the mantissa after an extra iteration, instead of the little
				12 bits of accuracy that _mm_rcp_ps gives us 
				In short, one Newton-Raphson Iteration:
				 f( i+1 ) = 2 * rcp( f ) - f * rcp( f ) * rcp( f )
				See Intel AP-803 (Application note), Order N� 243637-002 (if you can get it!)
				 "x0 = RCPSS(d)
				x1 = x0 * (2 - d * x0) = 2 * x0 - d * x0 * x0
				where x0 is the first approximation to the reciprocal of the divisor d, and x1 is a
				better approximation. You must use this formula before multiplying with the dividend."
			@param
				4 floating point values. If it's zero, the returned value will be infinite,
				which is the correct result, but it's slower than InvNonZero4
			@return
				1 / x (packed as 4 floats)
		*/
		static inline ArrayReal Inv4( ArrayReal val )
		{
			ArrayReal inv = _mm_rcp_ps( val );
			ArrayReal twoRcp	= _mm_add_ps( inv, inv );					//2 * rcp( f )
			ArrayReal rightSide= _mm_mul_ps( val, _mm_mul_ps( inv, inv ) );	//f * rcp( f ) * rcp( f )
			rightSide = _mm_and_ps( rightSide, _mm_cmpneq_ps( val, _mm_setzero_ps() ) ); //Nuke this NaN
			return _mm_sub_ps( twoRcp, rightSide );
		}

		/**	Returns the reciprocal of x
			@remarks
				If the input is zero, it will produce a NaN!!! (but it's faster)
				Note: Some architectures may slowdown when a NaN is produced, making this
				function slower than Inv4 for those cases
				@see Inv4

				Use SSE Newton-Raphson reciprocal estimate, accurate to 23 significant
				bits of the mantissa after an extra iteration, instead of the little
				12 bits of accuracy that _mm_rcp_ps gives us 
				In short, one Newton-Raphson Iteration:
				 f( i+1 ) = 2 * rcp( f ) - f * rcp( f ) * rcp( f )
				See Intel AP-803 (Application note), Order N� 243637-002 (if you can get it!)
				 "x0 = RCPSS(d)
				x1 = x0 * (2 - d * x0) = 2 * x0 - d * x0 * x0
				where x0 is the first approximation to the reciprocal of the divisor d, and x1 is a
				better approximation. You must use this formula before multiplying with the dividend."
			@param
				4 floating point values. If it's zero, the returned value will be NaN
			@return
				1 / x (packed as 4 floats)
		*/
		static inline ArrayReal InvNonZero4( ArrayReal val )
		{
			ArrayReal inv = _mm_rcp_ps( val );
			ArrayReal twoRcp	= _mm_add_ps( inv, inv );					//2 * rcp( f )
			ArrayReal rightSide= _mm_mul_ps( val, _mm_mul_ps( inv, inv ) );	//f * rcp( f ) * rcp( f )
			return _mm_sub_ps( twoRcp, rightSide );
		}

		/**	Returns the squared root of the reciprocal of x
			@remarks
				Use SSE Newton-Raphson reciprocal estimate, accurate to 23 significant
				bits of the mantissa after an extra iteration, instead of the little
				12 bits of accuracy that _mm_rcp_ps gives us 
				In short, one Newton-Raphson Iteration:
					 f( i+1 ) = 0.5 * rsqrt( f ) * ( 3 - f * rsqrt( f ) * rsqrt( f ) )
				See Intel AP-803 (Application note), Order N� 243637-002 (if you can get it!)
				"x0 = RSQRTSS(a)
				x1 = 0.5 * x0 * ( 3 - ( a * x0 ) * x0 )
				where x0 is the first approximation to the reciprocal square root of a, and x1 is a
				better approximation. The order of evaluation is important. You must use this formula
				before multiplying with a to get the square root."
			@param
				4 floating point values
			@return
				1 / sqrt( x ) (packed as 4 floats)
		*/
		static inline ArrayReal InvSqrt4( ArrayReal f )
		{
			ArrayReal invSqrt	= _mm_rsqrt_ps( f );

			ArrayReal halfInvSqrt= _mm_mul_ps( HALF, invSqrt );						//0.5 * rsqrt( f )
			ArrayReal rightSide  = _mm_mul_ps( invSqrt, _mm_mul_ps( f, invSqrt ) );	//f * rsqrt( f ) * rsqrt( f )
			rightSide = _mm_and_ps( rightSide, _mm_cmpneq_ps( f, _mm_setzero_ps() ) );//Nuke this NaN
			return _mm_mul_ps( halfInvSqrt, _mm_sub_ps( THREE, rightSide ) );		//halfInvSqrt*(3 - rightSide)
		}

		/**	Returns the squared root of the reciprocal of x
			@remarks
				Use SSE Newton-Raphson reciprocal estimate, accurate to 23 significant
				bits of the mantissa after an extra iteration, instead of the little
				12 bits of accuracy that _mm_rcp_ps gives us 
				In short, one Newton-Raphson Iteration:
					 f( i+1 ) = 0.5 * rsqrt( f ) * ( 3 - f * rsqrt( f ) * rsqrt( f ) )
				See Intel AP-803 (Application note), Order N� 243637-002 (if you can get it!)
				"x0 = RSQRTSS(a)
				x1 = 0.5 * x0 * ( 3 - ( a * x0 ) * x0 )
				where x0 is the first approximation to the reciprocal square root of a, and x1 is a
				better approximation. The order of evaluation is important. You must use this formula
				before multiplying with a to get the square root."

				Warning: Passing a zero will return a NaN instead of infinity
			@param
				4 floating point values
			@return
				1 / sqrt( x ) (packed as 4 floats)
		*/
		static inline ArrayReal InvSqrtNonZero4( ArrayReal f )
		{
			ArrayReal invSqrt = _mm_rsqrt_ps( f );

			ArrayReal halfInvSqrt= _mm_mul_ps( HALF, invSqrt );						//0.5 * rsqrt( f )
			ArrayReal rightSide  = _mm_mul_ps( invSqrt, _mm_mul_ps( f, invSqrt ) );	//f * rsqrt( f ) * rsqrt( f )
			return _mm_mul_ps( halfInvSqrt, _mm_sub_ps( THREE, rightSide ) );		//halfInvSqrt*(3 - rightSide)
		}

		/**	Break x into fractional and integral parts
			@param
				4 floating point values. i.e. "2.57" (x4)
			@param
				The integral part of x. i.e. 2
			@return
				The fractional part of x. i.e. 0.57
		*/
		static inline ArrayReal Modf4( ArrayReal x, ArrayReal &outIntegral );

		/**	Returns the arccos of x
			@param
				4 floating point values
			@return
				arccos( x ) (packed as 4 floats)
		*/
		static inline ArrayReal ACos4( ArrayReal x );

		/**	Returns the sine of x
			@param
				4 floating point values
			@return
				sin( x ) (packed as 4 floats)
		*/
		static ArrayReal Sin4( ArrayReal x );

		/**	Returns the cosine of x
			@param
				4 floating point values
			@return
				cos( x ) (packed as 4 floats)
		*/
		static ArrayReal Cos4( ArrayReal x );

		/**	Calculates the cosine & sine of x. Use this function if you need to calculate
			both, as it is faster than calling Cos4 & Sin4 together.
			@param
				4 floating point values
			@param
				Output value, sin( x ) (packed as 4 floats)
			@param
				Output value, cos( x ) (packed as 4 floats)
		*/
		static void SinCos4( ArrayReal x, ArrayReal &outSin, ArrayReal &outCos );
	};

//	inline ArrayReal operator - ( ArrayReal l )					{ return _mm_xor_ps( l, MathlibSSE2::SIGN_MASK ); }
//	inline ArrayReal operator + ( ArrayReal l, Real r )			{ return _mm_add_ps( l, _mm_set1_ps( r ) ); }
//	inline ArrayReal operator + ( Real l, ArrayReal r )			{ return _mm_add_ps( _mm_set1_ps( l ), r ); }
//	inline ArrayReal operator + ( ArrayReal l, ArrayReal r )	{ return _mm_add_ps( l, r ); }
//	inline ArrayReal operator - ( ArrayReal l, Real r )			{ return _mm_sub_ps( l, _mm_set1_ps( r ) ); }
//	inline ArrayReal operator - ( Real l, ArrayReal r )			{ return _mm_sub_ps( _mm_set1_ps( l ), r ); }
//	inline ArrayReal operator - ( ArrayReal l, ArrayReal r )	{ return _mm_sub_ps( l, r ); }
//	inline ArrayReal operator * ( ArrayReal l, Real r )			{ return _mm_mul_ps( l, _mm_set1_ps( r ) ); }
//	inline ArrayReal operator * ( Real l, ArrayReal r )			{ return _mm_mul_ps( _mm_set1_ps( l ), r ); }
	inline ArrayReal operator * ( ArrayReal l, ArrayReal r )	{ return _mm_mul_ps( l, r ); }
//	inline ArrayReal operator / ( ArrayReal l, Real r )			{ return _mm_div_ps( l, _mm_set1_ps( r ) ); }
//	inline ArrayReal operator / ( Real l, ArrayReal r )			{ return _mm_div_ps( _mm_set1_ps( l ), r ); }
//	inline ArrayReal operator / ( ArrayReal l, ArrayReal r )	{ return _mm_div_ps( l, r ); }
}

#include "OgreMathlibSSE2.inl"

#endif
#endif
