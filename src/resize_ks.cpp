#include <Windows.h>
#include <cmath>
#include <numeric>
#include <thread>
#include "filter2.hpp"
#include "version.hpp"

static bool func_proc_video(FILTER_PROC_VIDEO *video);

#define PLUGIN_NAME L"Lanczos3 / 画素平均法リサイズ"
static FILTER_ITEM_TRACK track_mag(L"拡大率", 100.0, 0.0, 10000.0, 0.01);
static FILTER_ITEM_TRACK track_width(L"X", 100.0, 0.0, 10000.0, 0.01);
static FILTER_ITEM_TRACK track_height(L"Y", 100.0, 0.0, 10000.0, 0.01);
static FILTER_ITEM_CHECK check_ave(L"平均法", false);
static FILTER_ITEM_CHECK check_dot(L"ドット数でサイズ指定", false);
static FILTER_ITEM_TRACK track_nth(L"スレッド数", 1.0, -1000.0, 1000.0, 1.0);

EXTERN_C FILTER_PLUGIN_TABLE *
GetFilterPluginTable()
{
	static void* settings[] = {
		&track_mag, &track_width, &track_height, &check_ave, &check_dot, &track_nth, nullptr
	};
	static FILTER_PLUGIN_TABLE fpt = {
		FILTER_PLUGIN_TABLE::FLAG_VIDEO,
		PLUGIN_NAME,
		L"変形",
		PLUGIN_NAME L" " VERSION L" by KAZOON",
		settings,
		func_proc_video,
		nullptr, // func_proc_audio
	};
	return &fpt;
}

class Rational {
private:
	std::intmax_t numerator, denominator;
public:
	Rational(const std::intmax_t num, const std::intmax_t &den)
	{
		auto c = std::gcd(std::abs(num), std::abs(den));
		if ( den < 0 ) {
			numerator = -num/c;
			denominator = -den/c;
		} else {
			numerator = num/c;
			denominator = den/c;
		}
	}
	Rational(const std::intmax_t i) : numerator(i), denominator(1) {}
	Rational() : numerator(0), denominator(1) {}
	std::intmax_t
	get_numerator()
	const {
		return numerator;
	}
	std::intmax_t
	get_denominator()
	const {
		return denominator;
	}
	Rational
	operator +(const Rational &other)
	const {
		const std::intmax_t c = std::gcd(denominator, other.denominator);
		const std::intmax_t s_d = denominator/c, o_d = other.denominator/c;
		return Rational(numerator*o_d+other.numerator*s_d, denominator*o_d);
	}
	Rational
	operator +(const std::intmax_t &other)
	const {
		return Rational(numerator+other*denominator, denominator);
	}
	Rational
	operator -(const Rational &other)
	const {
		const std::intmax_t c = std::gcd(denominator, other.denominator);
		const std::intmax_t s_d = denominator/c, o_d = other.denominator/c;
		return Rational(numerator*o_d-other.numerator*s_d, denominator*o_d);
	}
	Rational
	operator -(const std::intmax_t &other)
	const {
		return Rational(numerator-other*denominator, denominator);
	}
	Rational
	operator *(const Rational &other)
	const {
		const std::intmax_t ca = std::gcd(std::abs(numerator), other.denominator);
		const std::intmax_t cb = std::gcd(denominator, std::abs(other.numerator));
		return Rational((numerator/ca) * (other.numerator/cb), (denominator/cb) * (other.denominator/ca));
	}
	Rational
	operator *(const std::intmax_t &other)
	const {
		const std::intmax_t c = std::gcd(std::abs(other), denominator);
		return Rational(numerator*(other/c), denominator/c);
	}
	Rational
	operator /(const Rational &other)
	const {
		const std::intmax_t ca = std::gcd(std::abs(numerator), std::abs(other.numerator));
		const std::intmax_t cb = std::gcd(denominator, other.denominator);
		return Rational((numerator/ca) * (other.denominator/cb), (denominator/cb) * (other.numerator/ca));
	}
	Rational
	operator /(const std::intmax_t &other)
	const {
		const std::intmax_t c = std::gcd(std::abs(numerator), std::abs(other));
		return Rational(numerator/c, denominator*(other/c));
	}
	Rational
	reciprocal()
	const {
		return Rational(denominator, numerator);
	}
	std::intmax_t
	floor()
	const {
		const std::intmax_t r = numerator % denominator;
		if ( r < 0 ) {
			return ( (numerator-r)/denominator - 1 );
		} else {
			return ( (numerator-r)/denominator );
		}
	}
	std::intmax_t
	floor_eps()
	const {
		const std::intmax_t r = numerator % denominator;
		if ( r <= 0 ) {
			return ( (numerator-r)/denominator - 1 );
		} else {
			return ( (numerator-r)/denominator );
		}
	}
	std::intmax_t
	ceil()
	const {
		const std::intmax_t r = numerator % denominator;
		if ( r <= 0 ) {
			return ( (numerator-r)/denominator );
		} else {
			return ( (numerator-r)/denominator + 1 );
		}
	}
	std::intmax_t
	ceil_eps()
	const {
		const std::intmax_t r = numerator % denominator;
		if ( r < 0 ) {
			return ( (numerator-r)/denominator );
		} else {
			return ( (numerator-r)/denominator + 1 );
		}
	}
	float
	to_float()
	const {
		return( static_cast<float>(numerator) / static_cast<float>(denominator) );
	}
};

template <class T>
static void
parallel_do(void (T::*f)(int, const int &), T *p, const int &n)
{
	if ( 1 < n ) {
		auto threads = std::make_unique<std::thread[]>(n);
		for (int i=0; i<n; i++) {
			threads[i] = std::thread(f, p, i, n);
		}
		for (int i=0; i<n; i++) {
			threads[i].join();
		}
	} else {
		(p->*f)(0, n);
	}
}

constexpr static const float PI = 3.141592653589793f;

static unsigned char
uc_cast(const float &x)
{
	if ( x < 0.0f || std::isnan(x) ) {
		return static_cast<unsigned char>(0);
	} else if ( 255.0f < x ) {
		return static_cast<unsigned char>(255);
	} else {
		return static_cast<unsigned char>(std::round(x));
	}
}
static unsigned char
uc_cast(std::intmax_t num, std::intmax_t den)
{
	std::intmax_t c = std::gcd(std::abs(num), std::abs(den));
	if ( den < 0 ) {
		num = -num/c;
		den = -den/c;
	} else {
		num = num/c;
		den = den/c;
	}
	if ( num <= 0 ) {
		return static_cast<unsigned char>(0);
	} else if ( 255*den <= num ) {
		return static_cast<unsigned char>(255);
	} else {
		std::intmax_t r = num % den;
		if ( r*2 < den ) {
			return static_cast<unsigned char>((num-r)/den);
		} else {
			return static_cast<unsigned char>((num-r)/den+1);
		}
	}
}
static int
n_th_correction(const double &d_th)
{
	int n_th=static_cast<int>(d_th);
	if ( n_th <= 0 ) {
		n_th += std::thread::hardware_concurrency();
		if ( n_th <= 0 ) {
			n_th = 1;
		}
	}
	return n_th;
}

class ResizeL3 {
private:
	class XY {
	private:
		static float
		sinc(const float &x)
		{
			if ( x == 0.0f ) {
				return 1.0f;
			} else {
				return std::sin(x)/x;
			}
		}
		static float
		lanczos3(const float &x)
		{
			return sinc(PI*x)*sinc((PI/3.0f)*x);
		}
	public:
		struct RANGE {
			int start, end, skipped;
			Rational center;
		};
		int src_size, dest_size, var;
		bool extend;
		Rational reversed_scale, correction, weight_scale;
		std::unique_ptr<std::unique_ptr<float[]>[]> weights;
		XY(int ss, int ds) : src_size(ss), dest_size(ds) {}
		void
		calc_range(const int &dest, RANGE *range)
		const {
			range->center = reversed_scale*dest + correction;
			if ( extend ) {
				range->start = static_cast<int>( range->center.ceil_eps() ) - 3;
				range->end = static_cast<int>( range->center.floor_eps() ) + 3;
			} else {
				range->start = static_cast<int>( ( range->center - reversed_scale*3 ).ceil_eps() );
				range->end = static_cast<int>( ( range->center + reversed_scale*3 ).floor_eps() );
			}
			range->skipped = 0;
			if ( range->start < 0 ) {
				range->skipped = -(range->start);
				range->start = 0;
			}
			if ( src_size - 1 < range->end ) {
				range->end = src_size - 1;
			}
		}
		void
		calc_params()
		{
			reversed_scale = Rational(src_size, dest_size);
			extend = ( reversed_scale.get_numerator() <= reversed_scale.get_denominator() );
			correction = (reversed_scale-1)/2;
			weight_scale = extend ? Rational(1) : reversed_scale.reciprocal();
			var = dest_size / std::gcd(dest_size, src_size);
			weights = std::make_unique<std::unique_ptr<float[]>[]>(var);
		}
		void
		set_weights(const int &start, const int &end)
		{
			for (auto i=start; i<end; i++) {
				const Rational c = reversed_scale*i + correction;
				int s, e;
				if ( extend ) {
					s = static_cast<int>( c.ceil_eps() ) - 3;
					e = static_cast<int>( c.floor_eps() ) + 3;
				} else {
					s = static_cast<int>( ( c - reversed_scale*3 ).ceil_eps() );
					e = static_cast<int>( ( c + reversed_scale*3 ).floor_eps() );
				}
				weights[i] = std::make_unique<float[]>(e-s+1);
				for ( auto sxy = s; sxy <= e; sxy++ ) {
					weights[i][sxy-s] = lanczos3( ((c-sxy)*weight_scale).to_float() );
				}
			}
		}
	};
	void
	interpolate(const int &dx, const int dy)
	{
		XY::RANGE xrange, yrange;
		x.calc_range(dx, &xrange);
		y.calc_range(dy, &yrange);
		float b=0.0f, g=0.0f, r=0.0f, a=0.0f, w=0.0f;
		const auto *wxs = x.weights[ dx % (x.var) ].get();
		const auto *wys = y.weights[ dy % (y.var) ].get();
		for ( auto sy=(yrange.start); sy<=(yrange.end); sy++ ) {
			const auto wy = wys[sy-(yrange.start)+(yrange.skipped)];
			for ( auto sx=(xrange.start); sx<=(xrange.end); sx++ ) {
				const auto wxy = wy*wxs[sx-(xrange.start)+(xrange.skipped)];
				const auto s = src[sy*(x.src_size)+sx];
				const auto wxya = wxy*s.a;
				r += s.r*wxya;
				g += s.g*wxya;
				b += s.b*wxya;
				a += wxya;
				w += wxy;
			}
		}
		auto d_px = &dest[dy*(x.dest_size)+dx];
		d_px->r = uc_cast(r/a);
		d_px->g = uc_cast(g/a);
		d_px->b = uc_cast(b/a);
		d_px->a = uc_cast(a/w);
	}
public:
	const PIXEL_RGBA *src;
	PIXEL_RGBA *dest;
	XY x, y;
	ResizeL3(const PIXEL_RGBA *_src, int sw, int sh, PIXEL_RGBA *_dest, int dw, int dh)
		: src(_src), dest(_dest), x(sw, dw), y(sh, dh)
	{
		x.calc_params();
		y.calc_params();
	}
	void
	invoke_set_weights(int i, const int &n_th)
	{
		x.set_weights( (i*(x.var))/n_th, ((i+1)*(x.var))/n_th );
		y.set_weights( (i*(y.var))/n_th, ((i+1)*(y.var))/n_th );
	}
	void
	invoke_interpolate(int i, const int &n_th)
	{
		const auto y_start=(i*(y.dest_size))/n_th;
		const auto y_end=((i+1)*(y.dest_size))/n_th;
		for (auto dy=y_start; dy<y_end; dy++) {
			for (auto dx=0; dx<(x.dest_size); dx++) {
				interpolate(dx, dy);
			}
		}
	}
};

class ResizeAa {
private:
	class XY {
	public:
		int src_size, dest_size, sc, dc;
		struct RANGE {
			int start, end;
		};
		XY(int ss, int ds) : src_size(ss), dest_size(ds) {}
		void
		calc_range(const int &dest, RANGE *range)
		const {
			range->start = dest*dc;
			range->end = (dest+1)*dc;
		}
		void
		calc_params()
		{
			const int c = std::gcd(dest_size, src_size);
			sc = dest_size/c;
			dc = src_size/c;
		}
	};
	void
	interpolate(const int &dx, const int &dy)
	{
		XY::RANGE xrange, yrange;
		x.calc_range(dx, &xrange);
		y.calc_range(dy, &yrange);
		std::intmax_t b=0, g=0, r=0, a=0;
		for ( auto sy=(yrange.start); sy<(yrange.end); sy++ ) {
			const auto xs = (sy/y.sc)*(x.src_size);
			for ( auto sx=(xrange.start); sx<(xrange.end); sx++ ) {
				const auto s = src[xs+(sx/x.sc)];
				const std::intmax_t wa=static_cast<std::intmax_t>(s.a);
				r += s.r*wa;
				g += s.g*wa;
				b += s.b*wa;
				a += wa;
			}
		}
		auto d_px = &dest[dy*(x.dest_size)+dx];
		d_px->r = uc_cast(r, a);
		d_px->g = uc_cast(g, a);
		d_px->b = uc_cast(b, a);
		d_px->a = uc_cast(a, w);
	}
public:
	const PIXEL_RGBA *src;
	PIXEL_RGBA *dest;
	XY x, y;
	std::intmax_t w;
	ResizeAa(const PIXEL_RGBA *_src, int sw, int sh, PIXEL_RGBA *_dest, int dw, int dh)
		: src(_src), dest(_dest), x(sw, dw), y(sh, dh)
	{
		x.calc_params();
		y.calc_params();
	}
	void
	invoke_interpolate(int i, const int &n_th)
	{
		const auto y_start = ( i*(y.dest_size) )/n_th;
		const auto y_end = ( (i+1)*(y.dest_size) )/n_th;
		for (auto dy=y_start; dy<y_end; dy++) {
			for (auto dx=0; dx<(x.dest_size); dx++) {
				interpolate(dx, dy);
			}
		}
	}
};

static bool
func_proc_video(FILTER_PROC_VIDEO *video)
{
	const int sw=video->object->width, sh=video->object->height;
	int dw, dh;
	if (check_dot.value) {
		dw = static_cast<int>(std::round(track_width.value));
		dh = static_cast<int>(std::round(track_height.value));
	} else {
		dw = static_cast<int>(std::round(sw*(track_mag.value)*(track_width.value)*1e-4));
		dh = static_cast<int>(std::round(sh*(track_mag.value)*(track_height.value)*1e-4));
	}
	if ( ( 0 < sw ) && ( 0 < sh ) && ( 0 < dw ) && ( 0 < dh ) ) {
		auto src = std::make_unique<PIXEL_RGBA[]>(sw*sh);
		auto dest = std::make_unique<PIXEL_RGBA[]>(dw*dh);
		video->get_image_data(src.get());
		auto n_th = n_th_correction(track_nth.value);
		if (check_ave.value) {
			auto p = std::make_unique<ResizeAa>(src.get(), sw, sh, dest.get(), dw, dh);
			p->w = static_cast<std::intmax_t>((p->x.dc)*(p->y.dc));
			parallel_do(ResizeAa::invoke_interpolate, p.get(), n_th);
		} else {
			auto p = std::make_unique<ResizeL3>(src.get(), sw, sh, dest.get(), dw, dh);
			parallel_do(ResizeL3::invoke_set_weights, p.get(), n_th);
			parallel_do(ResizeL3::invoke_interpolate, p.get(), n_th);
		}
		video->set_image_data(dest.get(), dw, dh);
		return true;
	} else {
		return false;
	}
}
