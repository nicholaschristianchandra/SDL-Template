#pragma once

#include <Framework/Framework.hpp>

namespace Math
{
	constexpr float PI = 3.14159265358979f;

	struct Vec3
	{
		float x = 0, y = 0, z = 0;
	};

	/* Column-major, like HLSL's default: m[column * 4 + row] */
	struct Mat4
	{
		float m[16] = {};
	};

	inline Vec3 Add(Vec3 a, Vec3 b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
	inline Vec3 Sub(Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
	inline Vec3 Scale(Vec3 a, float s) { return Vec3{a.x * s, a.y * s, a.z * s}; }
	inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline Vec3 Cross(Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
	inline Vec3 Lerp(Vec3 a, Vec3 b, float t) { return Add(a, Scale(Sub(b, a), t)); }

	inline Vec3 Normalize(Vec3 a)
	{
		const float length = SDL_sqrtf(Dot(a, a));
		return length > 0 ? Scale(a, 1.0f / length) : a;
	}

	inline float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

	inline float SmoothStep(float edge0, float edge1, float x)
	{
		const float t = Clamp01((x - edge0) / (edge1 - edge0));
		return t * t * (3 - 2 * t);
	}

	inline Mat4 Multiply(const Mat4 &a, const Mat4 &b)
	{
		Mat4 result;

		for (int column = 0; column < 4; column++)
		{
			for (int row = 0; row < 4; row++)
			{
				float sum = 0;

				for (int k = 0; k < 4; k++)
				{
					sum += a.m[k * 4 + row] * b.m[column * 4 + k];
				}

				result.m[column * 4 + row] = sum;
			}
		}

		return result;
	}

	/* Right-handed, depth from 0 (near) to 1 (far), as SDL_GPU expects */
	inline Mat4 Perspective(float fovY, float aspect, float nearPlane, float farPlane)
	{
		const float f = 1.0f / SDL_tanf(fovY * 0.5f);
		Mat4 result;
		result.m[0] = f / aspect;
		result.m[5] = f;
		result.m[10] = farPlane / (nearPlane - farPlane);
		result.m[11] = -1;
		result.m[14] = nearPlane * farPlane / (nearPlane - farPlane);
		return result;
	}

	/* A box halfSize wide each way, for the shadow map */
	inline Mat4 Orthographic(float halfSize, float nearPlane, float farPlane)
	{
		Mat4 result;
		result.m[0] = 1.0f / halfSize;
		result.m[5] = 1.0f / halfSize;
		result.m[10] = 1.0f / (nearPlane - farPlane);
		result.m[14] = nearPlane / (nearPlane - farPlane);
		result.m[15] = 1;
		return result;
	}

	inline Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up)
	{
		const Vec3 z = Normalize(Sub(eye, target));
		const Vec3 x = Normalize(Cross(up, z));
		const Vec3 y = Cross(z, x);
		Mat4 result;
		result.m[0] = x.x; result.m[4] = x.y; result.m[8] = x.z; result.m[12] = -Dot(x, eye);
		result.m[1] = y.x; result.m[5] = y.y; result.m[9] = y.z; result.m[13] = -Dot(y, eye);
		result.m[2] = z.x; result.m[6] = z.y; result.m[10] = z.z; result.m[14] = -Dot(z, eye);
		result.m[15] = 1;
		return result;
	}
}
