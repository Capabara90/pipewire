
#define SPA_API_IMPL	SPA_EXPORT
#include <spa/utils/defs.h>
#include <spa/buffer/alloc.h>
#include <spa/buffer/buffer.h>
#include <spa/buffer/type-info.h>
#include <spa/control/control.h>
#include <spa/control/type-info.h>
#include <spa/control/ump-utils.h>
#include <spa/debug/buffer.h>
#include <spa/debug/context.h>
#include <spa/debug/dict.h>
#include <spa/debug/file.h>
#include <spa/debug/format.h>
#include <spa/debug/log.h>
#include <spa/debug/mem.h>
#include <spa/debug/node.h>
#include <spa/debug/pod.h>
#include <spa/debug/types.h>
#include <spa/filter-graph/filter-graph.h>
#include <spa/graph/graph.h>
#include <spa/interfaces/audio/aec.h>
#include <spa/monitor/device.h>
#include <spa/monitor/event.h>
#include <spa/monitor/type-info.h>
#include <spa/monitor/utils.h>
#include <spa/node/command.h>
#include <spa/node/event.h>
#include <spa/node/io.h>
#include <spa/node/keys.h>
#include <spa/node/node2.h>
#include <spa/node/node.h>
#include <spa/node/type-info.h>
#include <spa/node/utils.h>
#include <spa/param/audio/aac.h>
#include <spa/param/audio/aac-types.h>
#include <spa/param/audio/aac-utils.h>
#include <spa/param/audio/alac.h>
#include <spa/param/audio/alac-utils.h>
#include <spa/param/audio/amr.h>
#include <spa/param/audio/amr-types.h>
#include <spa/param/audio/amr-utils.h>
#include <spa/param/audio/ape.h>
#include <spa/param/audio/ape-utils.h>
#include <spa/param/audio/compressed.h>
#include <spa/param/audio/dsd.h>
#include <spa/param/audio/dsd-utils.h>
#include <spa/param/audio/dsp.h>
#include <spa/param/audio/dsp-utils.h>
#include <spa/param/audio/flac.h>
#include <spa/param/audio/flac-utils.h>
#include <spa/param/audio/format.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/iec958.h>
#include <spa/param/audio/iec958-types.h>
#include <spa/param/audio/iec958-utils.h>
#include <spa/param/audio/layout.h>
#include <spa/param/audio/mp3.h>
#include <spa/param/audio/mp3-types.h>
#include <spa/param/audio/mp3-utils.h>
#include <spa/param/audio/opus.h>
#include <spa/param/audio/ra.h>
#include <spa/param/audio/ra-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/param/audio/raw-json.h>
#include <spa/param/audio/raw-types.h>
#include <spa/param/audio/raw-utils.h>
#include <spa/param/audio/type-info.h>
#include <spa/param/audio/vorbis.h>
#include <spa/param/audio/vorbis-utils.h>
#include <spa/param/audio/wma.h>
#include <spa/param/audio/wma-types.h>
#include <spa/param/audio/wma-utils.h>
#include <spa/param/bluetooth/audio.h>
#include <spa/param/bluetooth/type-info.h>
#include <spa/param/buffers.h>
#include <spa/param/buffers-types.h>
#include <spa/param/format.h>
#include <spa/param/format-types.h>
#include <spa/param/format-utils.h>
#include <spa/param/latency.h>
#include <spa/param/latency-types.h>
#include <spa/param/latency-utils.h>
#include <spa/param/param.h>
#include <spa/param/param-types.h>
#include <spa/param/port-config.h>
#include <spa/param/port-config-types.h>
#include <spa/param/profile.h>
#include <spa/param/profiler.h>
#include <spa/param/profiler-types.h>
#include <spa/param/profile-types.h>
#include <spa/param/props.h>
#include <spa/param/props-types.h>
#include <spa/param/route.h>
#include <spa/param/route-types.h>
#include <spa/param/tag.h>
#include <spa/param/tag-types.h>
#include <spa/param/tag-utils.h>
#include <spa/param/type-info.h>
#include <spa/param/video/chroma.h>
#include <spa/param/video/color.h>
#include <spa/param/video/dsp.h>
#include <spa/param/video/dsp-utils.h>
#include <spa/param/video/encoded.h>
#include <spa/param/video/format.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/h264.h>
#include <spa/param/video/h264-utils.h>
#include <spa/param/video/mjpg.h>
#include <spa/param/video/mjpg-utils.h>
#include <spa/param/video/multiview.h>
#include <spa/param/video/raw.h>
#include <spa/param/video/raw-types.h>
#include <spa/param/video/raw-utils.h>
#include <spa/param/video/type-info.h>
#include <spa/pod/builder.h>
#include <spa/pod/command.h>
#include <spa/pod/compare.h>
#include <spa/pod/dynamic.h>
#include <spa/pod/event.h>
#include <spa/pod/filter.h>
#include <spa/pod/iter.h>
#include <spa/pod/parser.h>
#include <spa/pod/pod.h>
#include <spa/pod/vararg.h>
#include <spa/support/cpu.h>
#include <spa/support/dbus.h>
#include <spa/support/i18n.h>
#include <spa/support/log.h>
#include <spa/support/log-impl.h>
#include <spa/support/loop.h>
#include <spa/support/loop-invoke.h>
#include <spa/support/plugin.h>
#include <spa/support/plugin-loader.h>
#include <spa/support/system.h>
#include <spa/support/thread.h>
#include <spa/utils/ansi.h>
#include <spa/utils/atomic.h>
#include <spa/utils/cleanup.h>
#include <spa/utils/defs.h>
#include <spa/utils/dict.h>
#include <spa/utils/dll.h>
#include <spa/utils/endian.h>
#include <spa/utils/enum-types.h>
#include <spa/utils/hook.h>
#include <spa/utils/json-core.h>
#include <spa/utils/json.h>
#include <spa/utils/json-pod.h>
#include <spa/utils/json-visitor.h>
#include <spa/utils/keys.h>
#include <spa/utils/list.h>
#include <spa/utils/names.h>
#include <spa/utils/ratelimit.h>
#include <spa/utils/result.h>
#include <spa/utils/ringbuffer.h>
#include <spa/utils/string.h>
#include <spa/utils/type.h>
#include <spa/utils/type-info.h>






