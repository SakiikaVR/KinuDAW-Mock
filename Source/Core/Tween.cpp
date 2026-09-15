#include "../../Include/RmlUi/Core/Tween.h"
#include "../../Include/RmlUi/Core/Math.h"
#include <cstdio>
#include <utility>

namespace Rml {

namespace TweenFunctions {

	/**
	    Tweening functions below.
	    Partly based on http://libclaw.sourceforge.net/tweeners.html
	 */

	static inline float square(float t)
	{
		return t * t;
	}

	static float back(float t)
	{
		return t * t * (2.70158f * t - 1.70158f);
	}

	static float bounce(float t)
	{
		if (t > 1.f - 1.f / 2.75f)
			return 1.f - 7.5625f * square(1.f - t);
		else if (t > 1.f - 2.f / 2.75f)
			return 1.0f - (7.5625f * square(1.f - t - 1.5f / 2.75f) + 0.75f);
		else if (t > 1.f - 2.5f / 2.75f)
			return 1.0f - (7.5625f * square(1.f - t - 2.25f / 2.75f) + 0.9375f);
		return 1.0f - (7.5625f * square(1.f - t - 2.625f / 2.75f) + 0.984375f);
	}

	static float circular(float t)
	{
		return 1.f - Math::SquareRoot(1.f - t * t);
	}

	static float cubic(float t)
	{
		return t * t * t;
	}

	static float elastic(float t)
	{
		if (t == 0)
			return t;
		if (t == 1)
			return t;
		return -Math::Exp(7.24f * (t - 1.f)) * Math::Sin((t - 1.1f) * 2.f * Math::RMLUI_PI / 0.4f);
	}

	static float exponential(float t)
	{
		if (t == 0)
			return t;
		if (t == 1)
			return t;
		return Math::Exp(7.24f * (t - 1.f));
	}

	static float linear(float t)
	{
		return t;
	}

	static float quadratic(float t)
	{
		return t * t;
	}

	static float quartic(float t)
	{
		return t * t * t * t;
	}

	static float quintic(float t)
	{
		return t * t * t * t * t;
	}

	static float sine(float t)
	{
		return 1.f - Math::Cos(t * Math::RMLUI_PI * 0.5f);
	}

} // namespace TweenFunctions

Tween::Tween(Type type, Direction direction)
{
	if (direction & In)
		type_in = type;
	if (direction & Out)
		type_out = type;
}
Tween::Tween(Type type_in, Type type_out) : type_in(type_in), type_out(type_out) {}
Tween::Tween(CallbackFnc callback, Direction direction) : callback(callback)
{
	if (direction & In)
		type_in = Callback;
	if (direction & Out)
		type_out = Callback;
}
Tween Tween::CubicBezier(float x1, float y1, float x2, float y2)
{
	Tween result;
	result.is_cubic_bezier = true;
	result.cubic_bezier[0] = x1;
	result.cubic_bezier[1] = y1;
	result.cubic_bezier[2] = x2;
	result.cubic_bezier[3] = y2;
	return result;
}
float Tween::operator()(float t) const
{
	if (is_cubic_bezier)
	{
		if (t <= 0.f)
			return 0.f;
		if (t >= 1.f)
			return 1.f;
		const float x1 = cubic_bezier[0], y1 = cubic_bezier[1], x2 = cubic_bezier[2], y2 = cubic_bezier[3];
		auto Sample = [](float u, float p1, float p2) {
			const float v = 1.f - u;
			return 3.f * v * v * u * p1 + 3.f * v * u * u * p2 + u * u * u;
		};
		auto Slope = [](float u, float p1, float p2) {
			const float v = 1.f - u;
			return 3.f * v * v * p1 + 6.f * v * u * (p2 - p1) + 3.f * u * u * (1.f - p2);
		};
		float u = Math::Clamp(t, 0.f, 1.f);
		for (int i = 0; i < 8; ++i)
		{
			const float slope = Slope(u, x1, x2);
			if (Math::Absolute(slope) < 1e-6f)
				break;
			u = Math::Clamp(u - (Sample(u, x1, x2) - t) / slope, 0.f, 1.f);
		}
		float lo = 0.f, hi = 1.f;
		for (int i = 0; i < 10; ++i)
		{
			if (Sample(u, x1, x2) < t)
				lo = u;
			else
				hi = u;
			u = 0.5f * (lo + hi);
		}
		return Sample(u, y1, y2);
	}
	if (type_in != None && type_out == None)
	{
		return in(t);
	}
	if (type_in == None && type_out != None)
	{
		return out(t);
	}
	if (type_in != None && type_out != None)
	{
		return in_out(t);
	}
	return t;
}

void Tween::reverse()
{
	if (is_cubic_bezier)
	{
		const float x1 = cubic_bezier[0], y1 = cubic_bezier[1];
		cubic_bezier[0] = 1.f - cubic_bezier[2];
		cubic_bezier[1] = 1.f - cubic_bezier[3];
		cubic_bezier[2] = 1.f - x1;
		cubic_bezier[3] = 1.f - y1;
		return;
	}
	std::swap(type_in, type_out);
}

bool Tween::operator==(const Tween& other) const
{
	return type_in == other.type_in && type_out == other.type_out && callback == other.callback && is_cubic_bezier == other.is_cubic_bezier &&
		(!is_cubic_bezier ||
			(cubic_bezier[0] == other.cubic_bezier[0] && cubic_bezier[1] == other.cubic_bezier[1] && cubic_bezier[2] == other.cubic_bezier[2] &&
				cubic_bezier[3] == other.cubic_bezier[3]));
}

bool Tween::operator!=(const Tween& other) const
{
	return !(*this == other);
}

String Tween::to_string() const
{
	if (is_cubic_bezier)
	{
		char value[96];
		std::snprintf(value, sizeof(value), "cubic-bezier(%g, %g, %g, %g)", cubic_bezier[0], cubic_bezier[1], cubic_bezier[2], cubic_bezier[3]);
		return value;
	}
	static const Array<String, size_t(Count)> type_str = {
		{"none", "back", "bounce", "circular", "cubic", "elastic", "exponential", "linear", "quadratic", "quartic", "quintic", "sine", "callback"}};

	if (size_t(type_in) < type_str.size() && size_t(type_out) < type_str.size())
	{
		if (type_in == None && type_out == None)
		{
			return "none";
		}
		else if (type_in == type_out)
		{
			return type_str[size_t(type_in)] + String("-in-out");
		}
		else if (type_in == None)
		{
			return type_str[size_t(type_out)] + String("-out");
		}
		else if (type_out == None)
		{
			return type_str[size_t(type_in)] + String("-in");
		}
		else if (type_in != type_out)
		{
			return type_str[size_t(type_in)] + String("-in-") + type_str[size_t(type_out)] + String("-out");
		}
	}
	return "unknown";
}

float Tween::tween(Type type, float t) const
{
	using namespace TweenFunctions;

	switch (type)
	{
	case Back: return back(t);
	case Bounce: return bounce(t);
	case Circular: return circular(t);
	case Cubic: return cubic(t);
	case Elastic: return elastic(t);
	case Exponential: return exponential(t);
	case Linear: return linear(t);
	case Quadratic: return quadratic(t);
	case Quartic: return quartic(t);
	case Quintic: return quintic(t);
	case Sine: return sine(t);
	case Callback:
		if (callback)
			return (*callback)(t);
		break;
	default: break;
	}
	return t;
}

float Tween::in(float t) const
{
	return tween(type_in, t);
}

float Tween::out(float t) const
{
	return 1.0f - tween(type_out, 1.0f - t);
}

float Tween::in_out(float t) const
{
	if (t < 0.5f)
		return tween(type_in, 2.0f * t) * 0.5f;
	else
		return 0.5f + out(2.0f * t - 1.0f) * 0.5f;
}

} // namespace Rml
