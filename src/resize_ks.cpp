#include <Windows.h>
#include <cmath>
#include <numeric>
#include <numbers>
#include "util.hpp"
#include "filter2.hpp"
#include "version.hpp"

static bool func_proc_video(FILTER_PROC_VIDEO *video);

#define PLUGIN_NAME L"Lanczos3 / 画素平均法リサイズ"
static FILTER_ITEM_TRACK track_mag(L"拡大率", 100.0, 0.0, 10000.0, 0.01);
static FILTER_ITEM_TRACK track_width(L"X", 100.0, 0.0, 10000.0, 0.01);
static FILTER_ITEM_TRACK track_height(L"Y", 100.0, 0.0, 10000.0, 0.01);
static FILTER_ITEM_CHECK check_ave(L"平均法", false);
static FILTER_ITEM_CHECK check_dot(L"ピクセル数でサイズ指定", false);

EXTERN_C FILTER_PLUGIN_TABLE *
GetFilterPluginTable()
{
	static void* settings[] = {
		&track_mag, &track_width, &track_height, &check_ave, &check_dot, nullptr
	};
	static FILTER_PLUGIN_TABLE fpt = {
		FILTER_PLUGIN_TABLE::FLAG_VIDEO,
		PLUGIN_NAME,
		L"変形",
		PLUGIN_NAME L" ver " VERSION L" by KAZOON",
		settings,
		func_proc_video,
		nullptr, // func_proc_audio
	};
	return &fpt;
}

static std::unique_ptr<ThreadPool> TP;

EXTERN_C bool
InitializePlugin(DWORD version)
{
	TP = std::make_unique<ThreadPool>();
	return true;
}

EXTERN_C void
UninitializePlugin()
{
	TP = nullptr;
}

class ResizeAa {
private:
	class XY {
	public:
		int src_size, dest_size, sc, dc;
		struct RANGE {
			int start, end;
		};
		XY(int ss, int ds) : src_size(ss), dest_size(ds)
		{
			const int c = std::gcd(dest_size, src_size);
			sc = dest_size/c;
			dc = src_size/c;
		}
		void
		calc_range(const int xy, RANGE *range)
		const {
			range->start = xy*dc;
			range->end = (xy+1)*dc;
		}
	};
	static unsigned char
	uc_cast(std::int64_t num, std::int64_t den)
	{
		constexpr static const unsigned char u0=0u, u255=255u;
		if ( num <= 0ll ) {
			return u0;
		} else if ( 255ll*den <= num ) {
			return u255;
		} else {
			auto r = num % den;
			if ( r*2ll < den ) {
				return static_cast<unsigned char>((num-r)/den);
			} else if ( r*2ll == den ) {
				r = (num-r)/den;
				if ( (r&1ll) == 0ll ) {
					return static_cast<unsigned char>(r);
				} else {
					return static_cast<unsigned char>(r+1ll);
				}
			} else {
				return static_cast<unsigned char>((num-r)/den+1ll);
			}
		}
	}
	const PIXEL_RGBA *src;
	PIXEL_RGBA *dest;
	XY x, y;
	std::int64_t w;
	void
	interpolate(int dx, int dy)
	{
		XY::RANGE xrange, yrange;
		x.calc_range(dx, &xrange);
		y.calc_range(dy, &yrange);
		std::int64_t r=0ll, g=0ll, b=0ll, a=0ll;
		for ( auto sy=(yrange.start); sy<(yrange.end); sy++ ) {
			const auto xs = (sy/y.sc)*(x.src_size);
			for ( auto sx=(xrange.start); sx<(xrange.end); sx++ ) {
				const auto s_px = &src[xs+(sx/x.sc)];
				const auto wa = static_cast<std::int64_t>(s_px->a);
				r += s_px->r*wa;
				g += s_px->g*wa;
				b += s_px->b*wa;
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
	ResizeAa(const PIXEL_RGBA *_src, int sw, int sh, PIXEL_RGBA *_dest, int dw, int dh)
		: src(_src), dest(_dest), x(sw, dw), y(sh, dh)
	{
		w = x.dc*y.dc;
	}
	int
	dest_height()
	{
		return y.dest_size;
	}
	void
	invoke_interpolate(int dy)
	{
		for (auto dx=0; dx<(x.dest_size); dx++) {
			interpolate(dx, dy);
		}
	}
};

class ResizeL3 {
private:
	class XY {
	private:
		struct RANGE {
			int start, end, skipped;
			Rational center;
		};
		static float
		sinc(float x)
		{
			if ( x == 0.0f ) {
				return 1.0f;
			} else {
				return std::sin(x)/x;
			}
		}
		static float
		lanczos3(float x)
		{
			constexpr static const float pi = std::numbers::pi_v<float>;
			constexpr static const float pi_third = pi/3.0f;
			return sinc(pi*x)*sinc(pi_third*x);
		}
	public:
		int src_size, dest_size, var;
		bool extend;
		Rational reversed_scale, correction, weight_scale;
		std::unique_ptr<std::unique_ptr<float[]>[]> weights;
		std::unique_ptr<RANGE[]> ranges;
		XY(int ss, int ds) : src_size(ss), dest_size(ds), reversed_scale(src_size, dest_size)
		{
			extend = ( reversed_scale.get_numerator() <= reversed_scale.get_denominator() );
			correction = (reversed_scale-1ll)/2ll;
			weight_scale = extend ? Rational(1ll) : reversed_scale.reciprocal();
			var = dest_size / std::gcd(dest_size, src_size);
			weights = std::make_unique<std::unique_ptr<float[]>[]>(static_cast<std::size_t>(var));
			ranges = std::make_unique<RANGE[]>(static_cast<std::size_t>(dest_size));
		}
		void
		calc_range(int xy)
		{
			auto range = &ranges[static_cast<std::size_t>(xy)];
			range->center = reversed_scale*xy + correction;
			if ( extend ) {
				range->start = static_cast<int>( range->center.floorp1() ) - 3;
				range->end = static_cast<int>( range->center.ceilm1() ) + 3;
			} else {
				range->start = static_cast<int>( ( range->center - reversed_scale*3ll ).floorp1() );
				range->end = static_cast<int>( ( range->center + reversed_scale*3ll ).ceilm1() );
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
		set_weights(int i)
		{
			const Rational c = reversed_scale*i + correction; // reversed_scale*(i+1/2)-1/2
			std::intmax_t s, e;
			if ( extend ) {
				s = c.floorp1() - 3ll;
				e = c.ceilm1() + 3ll;
			} else {
				s = ( c - reversed_scale*3ll ).floorp1();
				e = ( c + reversed_scale*3ll ).ceilm1();
			}
			auto j = static_cast<std::size_t>(i);
			weights[j] = std::make_unique<float[]>(static_cast<std::size_t>(e-s+1ll));
			for ( auto sxy = s; sxy <= e; sxy++ ) {
				weights[j][static_cast<std::size_t>(sxy-s)] = lanczos3( ((c-sxy)*weight_scale).to_float() );
			}
		}
	};
	class FloatRGBAW {
	private:
		static unsigned char
		uc_cast(float x)
		{
			constexpr static const unsigned char u0=0u, u255=255u;
			if ( x < 0.0f || std::isnan(x) ) {
				return u0;
			} else if ( 255.0f < x ) {
				return u255;
			} else {
				return static_cast<unsigned char>(std::nearbyint(x));
			}
		}
	public:
		float r, g, b, a, w;
		FloatRGBAW() : r(0.0f), g(0.0f), b(0.0f), a(0.0f), w(0.0f) {}
		void
		fma(const PIXEL_RGBA *s_px, float wxy)
		{
			const auto wxya = wxy*static_cast<float>(s_px->a);
			r = std::fma(static_cast<float>(s_px->r), wxya, r);
			g = std::fma(static_cast<float>(s_px->g), wxya, g);
			b = std::fma(static_cast<float>(s_px->b), wxya, b);
			a += wxya;
			w += wxy;
		}
		void
		put_pixel(PIXEL_RGBA *d_px)
		const {
			d_px->r = uc_cast(r/a);
			d_px->g = uc_cast(g/a);
			d_px->b = uc_cast(b/a);
			d_px->a = uc_cast(a/w);
		}
	};
	const PIXEL_RGBA *src;
	PIXEL_RGBA *dest;
	XY x, y;
	void
	interpolate(int dx, int dy)
	{
		const auto xrange = &(x.ranges[static_cast<std::size_t>(dx)]);
		const auto yrange = &(y.ranges[static_cast<std::size_t>(dy)]);
		FloatRGBAW rgbaw;
		const auto wxs = x.weights[ static_cast<std::size_t>( dx % (x.var) ) ].get();
		const auto wys = y.weights[ static_cast<std::size_t>( dy % (y.var) ) ].get();
		for ( auto sy=(yrange->start); sy<=(yrange->end); sy++ ) {
			const auto wy = wys[sy-(yrange->start)+(yrange->skipped)];
			for ( auto sx=(xrange->start); sx<=(xrange->end); sx++ ) {
				const auto wxy = wy*wxs[sx-(xrange->start)+(xrange->skipped)];
				rgbaw.fma(&src[sy*(x.src_size)+sx], wxy);
			}
		}
		rgbaw.put_pixel(&dest[dy*(x.dest_size)+dx]);
	}
public:
	ResizeL3(const PIXEL_RGBA *_src, int sw, int sh, PIXEL_RGBA *_dest, int dw, int dh)
		: src(_src), dest(_dest), x(sw, dw), y(sh, dh) {}
	int
	var_size()
	{
		return x.var + y.var;
	}
	int
	dest_sum()
	{
		return x.dest_size + y.dest_size;
	}
	int
	dest_height()
	{
		return y.dest_size;
	}
	void
	invoke_set_weights(int i)
	{
		if ( i < x.var ) {
			x.set_weights(i);
		} else {
			y.set_weights(i-x.var);
		}
	}
	void
	invoke_calc_range(int i)
	{
		if ( i < x.dest_size ) {
			x.calc_range(i);
		} else {
			y.calc_range(i-x.dest_size);
		}
	}
	void
	invoke_interpolate(int dy)
	{
		for (auto dx=0; dx<(x.dest_size); dx++) {
			interpolate(dx, dy);
		}
	}
};

static bool
func_proc_video(FILTER_PROC_VIDEO *video)
{
	const int sw=video->object->width, sh=video->object->height;
	int dw, dh;
	if (check_dot.value) {
		dw = std::lrint(track_width.value);
		dh = std::lrint(track_height.value);
	} else {
		dw = std::lrint(sw*(track_mag.value)*(track_width.value)*1e-4);
		dh = std::lrint(sh*(track_mag.value)*(track_height.value)*1e-4);
	}
	if ( ( 0 < sw ) && ( 0 < sh ) && ( 0 < dw ) && ( 0 < dh ) ) {
		auto src = std::make_unique<PIXEL_RGBA[]>(static_cast<std::size_t>(sw*sh));
		auto dest = std::make_unique<PIXEL_RGBA[]>(static_cast<std::size_t>(dw*dh));
		video->get_image_data(src.get());
		try {
			if (check_ave.value) {
				ResizeAa it(src.get(), sw, sh, dest.get(), dw, dh);
				TP->parallel_do_batched([&it](int i){ it.invoke_interpolate(i); }, it.dest_height());
			} else {
				ResizeL3 it(src.get(), sw, sh, dest.get(), dw, dh);
				TP->parallel_do([&it](int i){ it.invoke_set_weights(i); }, it.var_size());
				TP->parallel_do_batched([&it](int i){ it.invoke_calc_range(i); }, it.dest_sum());
				TP->parallel_do([&it](int i){ it.invoke_interpolate(i); }, it.dest_height());
			}
		} catch ( std::exception &e ) {
			int size = MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, nullptr, 0);
			std::wstring what(static_cast<std::size_t>(size-1), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, what.data(), size);
			std::wstring wstr = L"以下のエラーが発生しました．バグの可能性が高いため， 詳しい状況を報告いただけると助かります．\nエラー内容: ";
			wstr += what;
			wstr += L"\n報告先: https://github.com/cycloawaodorin/resize_ks/issues";
			MessageBoxW(GetActiveWindow(), wstr.c_str(), nullptr, MB_OK);
			return false;
		}
		src = nullptr;
		video->set_image_data(dest.get(), dw, dh);
		return true;
	} else {
		return false;
	}
}
