/* CS:GO viewmodel_fov is a horizontal FOV at a 4:3 reference aspect. */
#ifndef GL_VIEWMODEL_FOV_H
#define GL_VIEWMODEL_FOV_H
#include <math.h>
static int OC2_ViewModelFrustum( float fov, int width, int height, float znear, float bounds[2] )
{
	if( !( fov >= 54.0f && fov <= 68.0f ) || width <= 0 || height <= 0 ) return 0;
	bounds[1] = znear * tanf( fov * 3.14159265358979323846f / 360.0f ) * 0.75f;
	bounds[0] = bounds[1] * width / height;
	return 1;
}
#endif
