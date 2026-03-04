#include <Windows.h>
#include <cmath>
#include <thread>
#include "filter2.hpp"
#include "version.hpp"

static bool func_proc_video(FILTER_PROC_VIDEO *video);

#define PLUGIN_NAME L"Lanczos3 / 画素平均法リサイズ"

EXTERN_C FILTER_PLUGIN_TABLE *
GetFilterPluginTable()
{
	static void* settings[] = { nullptr };
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

static bool
func_proc_video(FILTER_PROC_VIDEO *video)
{
	return true;
}
