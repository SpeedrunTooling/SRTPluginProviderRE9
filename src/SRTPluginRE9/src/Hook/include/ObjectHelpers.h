#ifndef SRTPLUGINRE9_OBJECTHELPERS_H
#define SRTPLUGINRE9_OBJECTHELPERS_H

#include <cmath>

namespace SRTPluginRE9::ObjectHelpers
{
	template <std::floating_point TFloatingType>
	struct Point3D
	{
		TFloatingType x, y, z;

		Point3D operator+(const Point3D &rhs) const
		{
			return {x + rhs.x, y + rhs.y, z + rhs.z};
		}

		Point3D operator-(const Point3D &rhs) const
		{
			return {x - rhs.x, y - rhs.y, z - rhs.z};
		}

		TFloatingType EuclideanDistance(const Point3D &lhs, const Point3D &rhs) const
		{
			auto [dx, dy, dz] = lhs - rhs;
			return std::hypot(dx, dy, dz);
		}
	};

	template <>
	struct Point3D<float_t>
	{
	};

	template <>
	struct Point3D<double_t>
	{
	};
}

#endif
