#ifndef SMAA_RT_METRICS
    #define SMAA_RT_METRICS float4( 1.0 / 1600.0, 1.0 / 900.0, 1600, 900 )
#endif
#define SMAA_PRESET_ULTRA 1
#define SMAA_HLSL_4_1 1

float toSRGB( float x )
{
	if (x <= 0.0031308)
		return 12.92 * x;
	else
		return 1.055 * pow( x,(1.0 / 2.4) ) - 0.055;
}

float fromSRGB( float x )
{
	if( x <= 0.040449907 )
		return x / 12.92;
	else
		return pow( (x + 0.055) / 1.055, 2.4 );
}

float4 fromSRGB( float4 x )
{
    return float4( fromSRGB( x.x ), fromSRGB( x.y ), fromSRGB( x.z ), x.w );
}
