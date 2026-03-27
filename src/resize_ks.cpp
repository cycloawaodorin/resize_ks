#include <Windows.h>
#include <cmath>
#include <numeric>
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
	TP.reset(nullptr);
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
		calc_range(const int &_dest, RANGE *range)
		const {
			range->center = reversed_scale*_dest + correction;
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
			weights = std::make_unique<std::unique_ptr<float[]>[]>(static_cast<std::size_t>(var));
		}
		void
		set_weights(const int i)
		{
			const Rational c = reversed_scale*i + correction; // reversed_scale*(i+1/2)-1/2
			std::intmax_t s, e;
			if ( extend ) {
				s = c.ceil_eps() - 3;
				e = c.floor_eps() + 3;
			} else {
				s = ( c - reversed_scale*3 ).ceil_eps();
				e = ( c + reversed_scale*3 ).floor_eps();
			}
			auto j = static_cast<std::size_t>(i);
			weights[j] = std::make_unique<float[]>(static_cast<std::size_t>(e-s+1));
			for ( auto sxy = s; sxy <= e; sxy++ ) {
				weights[j][static_cast<std::size_t>(sxy-s)] = lanczos3( ((c-sxy)*weight_scale).to_float() );
			}
		}
	};
	void
	interpolate(const int dx, const int dy, XY::RANGE *yrange)
	{
		auto xrange = &xranges[static_cast<std::size_t>(dx)];
		float b=0.0f, g=0.0f, r=0.0f, a=0.0f, w=0.0f;
		const auto *wxs = x.weights[ static_cast<std::size_t>( dx % (x.var) ) ].get();
		const auto *wys = y.weights[ static_cast<std::size_t>( dy % (y.var) ) ].get();
		for ( auto sy=(yrange->start); sy<=(yrange->end); sy++ ) {
			const auto wy = wys[sy-(yrange->start)+(yrange->skipped)];
			for ( auto sx=(xrange->start); sx<=(xrange->end); sx++ ) {
				const auto wxy = wy*wxs[sx-(xrange->start)+(xrange->skipped)];
				const auto s_px = &src[sy*(x.src_size)+sx];
				const auto wxya = wxy*s_px->a;
				r += s_px->r*wxya;
				g += s_px->g*wxya;
				b += s_px->b*wxya;
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
	std::unique_ptr<XY::RANGE[]> xranges;
	ResizeL3(const PIXEL_RGBA *_src, int sw, int sh, PIXEL_RGBA *_dest, int dw, int dh)
		: src(_src), dest(_dest), x(sw, dw), y(sh, dh)
	{
		x.calc_params();
		y.calc_params();
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
	invoke_calc_xranges(int i)
	{
		x.calc_range(i, &xranges[static_cast<std::size_t>(i)]);
	}
	void
	invoke_interpolate(int dy)
	{
		XY::RANGE yrange;
		y.calc_range(dy, &yrange);
		for (auto dx=0; dx<(x.dest_size); dx++) {
			interpolate(dx, dy, &yrange);
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
		XY(int ss, int ds) : src_size(ss), dest_size(ds)
		{
			const int c = std::gcd(dest_size, src_size);
			sc = dest_size/c;
			dc = src_size/c;
		}
		void
		calc_range(const int _dest, RANGE *range)
		const {
			range->start = _dest*dc;
			range->end = (_dest+1)*dc;
		}
	};
	void
	interpolate(const int dx, const int dy)
	{
		XY::RANGE xrange, yrange;
		x.calc_range(dx, &xrange);
		y.calc_range(dy, &yrange);
		std::uint64_t b=0, g=0, r=0, a=0;
		for ( auto sy=(yrange.start); sy<(yrange.end); sy++ ) {
			const auto xs = (sy/y.sc)*(x.src_size);
			for ( auto sx=(xrange.start); sx<(xrange.end); sx++ ) {
				const auto s_px = &src[xs+(sx/x.sc)];
				const std::uint64_t wa = s_px->a;
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
	const PIXEL_RGBA *src;
	PIXEL_RGBA *dest;
	XY x, y;
	std::uint64_t w;
	ResizeAa(const PIXEL_RGBA *_src, int sw, int sh, PIXEL_RGBA *_dest, int dw, int dh)
		: src(_src), dest(_dest), x(sw, dw), y(sh, dh) {}
	void
	invoke_interpolate(int i, const int n_th)
	{
		const int y_start = ( i*(y.dest_size) )/n_th;
		const int y_end = ( (i+1)*(y.dest_size) )/n_th;
		for (int dy=y_start; dy<y_end; dy++) {
			for (int dx=0; dx<(x.dest_size); dx++) {
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
		auto src = std::make_unique<PIXEL_RGBA[]>(static_cast<std::size_t>(sw*sh));
		auto dest = std::make_unique<PIXEL_RGBA[]>(static_cast<std::size_t>(dw*dh));
		video->get_image_data(src.get());
		if (check_ave.value) {
			ResizeAa it(src.get(), sw, sh, dest.get(), dw, dh);
			it.w = static_cast<std::uint64_t>( (it.x.dc)*(it.y.dc) );
			int n = static_cast<int>(TP->get_size());
			TP->parallel_do([&it, n](int i){it.invoke_interpolate(i, n);}, n);
		} else {
			ResizeL3 it(src.get(), sw, sh, dest.get(), dw, dh);
			TP->parallel_do([&it](int i){it.invoke_set_weights(i);}, it.x.var + it.y.var);
			TP->parallel_do([&it](int i){it.invoke_calc_xranges(i);}, it.x.dest_size);
			TP->parallel_do([&it](int i){it.invoke_interpolate(i);}, it.y.dest_size);
		}
		video->set_image_data(dest.get(), dw, dh);
		return true;
	} else {
		return false;
	}
}
