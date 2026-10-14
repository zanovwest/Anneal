"""Minimal CLAP host check for the built Anneal.clap (JUCE can't host CLAP, so this does it with ctypes).

Loads the binary, creates com.qulic.anneal, checks its audio ports (main + sidechain in, stereo out)
and parameter count, activates it at 48 kHz, checks the reported latency, and plays 2 s of a sine.
Usage: python ClapCheck.py <path to the CLAP binary>
  Windows/Linux: .../CLAP/Anneal.clap    macOS: .../CLAP/Anneal.clap/Contents/MacOS/Anneal
"""
import ctypes as C, math, sys

path = sys.argv[1]
lib = C.CDLL(path)
u32, i32, i64, u64, f32, f64, vp, cp, b = C.c_uint32, C.c_int32, C.c_int64, C.c_uint64, C.c_float, C.c_double, C.c_void_p, C.c_char_p, C.c_bool

class Version(C.Structure):
    _fields_ = [('major', u32), ('minor', u32), ('revision', u32)]

class Entry(C.Structure):
    _fields_ = [('clap_version', Version), ('init', C.CFUNCTYPE(b, cp)), ('deinit', C.CFUNCTYPE(None)),
                ('get_factory', C.CFUNCTYPE(vp, cp))]

class Descriptor(C.Structure):
    _fields_ = [('clap_version', Version), ('id', cp), ('name', cp), ('vendor', cp), ('url', cp), ('manual_url', cp),
                ('support_url', cp), ('version', cp), ('description', cp), ('features', C.POINTER(cp))]

class Host(C.Structure):
    pass
Host._fields_ = [('clap_version', Version), ('host_data', vp), ('name', cp), ('vendor', cp), ('url', cp), ('version', cp),
                 ('get_extension', C.CFUNCTYPE(vp, C.POINTER(Host), cp)),
                 ('request_restart', C.CFUNCTYPE(None, C.POINTER(Host))),
                 ('request_process', C.CFUNCTYPE(None, C.POINTER(Host))),
                 ('request_callback', C.CFUNCTYPE(None, C.POINTER(Host)))]

class AudioBuffer(C.Structure):
    _fields_ = [('data32', C.POINTER(C.POINTER(f32))), ('data64', C.POINTER(C.POINTER(f64))),
                ('channel_count', u32), ('latency', u32), ('constant_mask', u64)]

class InEvents(C.Structure):
    pass
InEvents._fields_ = [('ctx', vp), ('size', C.CFUNCTYPE(u32, C.POINTER(InEvents))), ('get', C.CFUNCTYPE(vp, C.POINTER(InEvents), u32))]

class OutEvents(C.Structure):
    pass
OutEvents._fields_ = [('ctx', vp), ('try_push', C.CFUNCTYPE(b, C.POINTER(OutEvents), vp))]

class Process(C.Structure):
    _fields_ = [('steady_time', i64), ('frames_count', u32), ('transport', vp),
                ('audio_inputs', C.POINTER(AudioBuffer)), ('audio_outputs', C.POINTER(AudioBuffer)),
                ('audio_inputs_count', u32), ('audio_outputs_count', u32),
                ('in_events', C.POINTER(InEvents)), ('out_events', C.POINTER(OutEvents))]

class Plugin(C.Structure):
    pass
P = C.POINTER(Plugin)
Plugin._fields_ = [('desc', C.POINTER(Descriptor)), ('plugin_data', vp),
                   ('init', C.CFUNCTYPE(b, P)), ('destroy', C.CFUNCTYPE(None, P)),
                   ('activate', C.CFUNCTYPE(b, P, f64, u32, u32)), ('deactivate', C.CFUNCTYPE(None, P)),
                   ('start_processing', C.CFUNCTYPE(b, P)), ('stop_processing', C.CFUNCTYPE(None, P)),
                   ('reset', C.CFUNCTYPE(None, P)), ('process', C.CFUNCTYPE(i32, P, C.POINTER(Process))),
                   ('get_extension', C.CFUNCTYPE(vp, P, cp)), ('on_main_thread', C.CFUNCTYPE(None, P))]

class Factory(C.Structure):
    pass
F = C.POINTER(Factory)
Factory._fields_ = [('get_plugin_count', C.CFUNCTYPE(u32, F)), ('get_plugin_descriptor', C.CFUNCTYPE(C.POINTER(Descriptor), F, u32)),
                    ('create_plugin', C.CFUNCTYPE(P, F, C.POINTER(Host), cp))]

class PortInfo(C.Structure):
    _fields_ = [('id', u32), ('name', C.c_char * 256), ('flags', u32), ('channel_count', u32), ('port_type', cp), ('in_place_pair', u32)]

class AudioPorts(C.Structure):
    _fields_ = [('count', C.CFUNCTYPE(u32, P, b)), ('get', C.CFUNCTYPE(b, P, u32, b, C.POINTER(PortInfo)))]

class LatencyExt(C.Structure):
    _fields_ = [('get', C.CFUNCTYPE(u32, P))]

class ParamsExt(C.Structure):
    _fields_ = [('count', C.CFUNCTYPE(u32, P))]   # only the first member is used

checks = []
def check(ok, name, detail=''):
    checks.append(ok)
    print(('[PASS] ' if ok else '[FAIL] ') + name + ('   ' + detail if detail else ''))

host_ext = C.CFUNCTYPE(vp, C.POINTER(Host), cp)(lambda h, i: None)
noop = C.CFUNCTYPE(None, C.POINTER(Host))(lambda h: None)
host = Host(Version(1, 2, 0), None, b'AnnealClapCheck', b'Qulic', b'', b'1.0', host_ext, noop, noop, noop)

entry = Entry.in_dll(lib, 'clap_entry')
check(entry.init(path.encode()), 'clap_entry init')
fac = C.cast(entry.get_factory(b'clap.plugin-factory'), F)
plugin = fac.contents.create_plugin(fac, C.byref(host), b'com.qulic.anneal')
check(bool(plugin), 'create com.qulic.anneal')
pl = plugin.contents
check(pl.init(plugin), 'plugin init')

ports = C.cast(pl.get_extension(plugin, b'clap.audio-ports'), C.POINTER(AudioPorts))
ins, outs = [], []
for is_in, lst in ((True, ins), (False, outs)):
    for k in range(ports.contents.count(plugin, is_in)):
        info = PortInfo()
        ports.contents.get(plugin, k, is_in, C.byref(info))
        lst.append((info.name.decode(), info.channel_count))
check(len(ins) >= 1 and ins[0][1] == 2 and len(outs) == 1 and outs[0][1] == 2, 'audio ports', f'in {ins}, out {outs}')

params = C.cast(pl.get_extension(plugin, b'clap.params'), C.POINTER(ParamsExt))
n_params = params.contents.count(plugin) if params else 0
check(n_params >= 25, 'parameters exposed', str(n_params))

fs, block = 48000.0, 512
check(pl.activate(plugin, fs, 1, 4096), 'activate 48 kHz')
lat = C.cast(pl.get_extension(plugin, b'clap.latency'), C.POINTER(LatencyExt))
latency = lat.contents.get(plugin) if lat else -1
check(latency == 1091, 'latency (Low 1024 + ceiling 67 at 48 kHz)', str(latency))
check(pl.start_processing(plugin), 'start processing')

def make_buffer(channels):
    chans = [(f32 * block)() for _ in range(channels)]
    ptrs = (C.POINTER(f32) * channels)(*[C.cast(c, C.POINTER(f32)) for c in chans])
    return chans, AudioBuffer(ptrs, None, channels, 0, 0)

in_bufs = [make_buffer(ch) for _, ch in ins]
out_chans, out_buf = make_buffer(2)
in_array = (AudioBuffer * len(in_bufs))(*[ab for _, ab in in_bufs])
size_fn = C.CFUNCTYPE(u32, C.POINTER(InEvents))(lambda l: 0)
get_fn = C.CFUNCTYPE(vp, C.POINTER(InEvents), u32)(lambda l, i: None)
push_fn = C.CFUNCTYPE(b, C.POINTER(OutEvents), vp)(lambda l, e: True)
in_events, out_events = InEvents(None, size_fn, get_fn), OutEvents(None, push_fn)

peak_out, finite, t, statuses = 0.0, True, 0, set()
for blk in range(200):   # ~2.1 s
    for i in range(block):
        x = 0.125 * math.sin(2 * math.pi * 1000.0 * (t + i) / fs)
        in_bufs[0][0][0][i] = x
        in_bufs[0][0][1][i] = x
    proc = Process(t, block, None, in_array, C.pointer(out_buf), len(in_bufs), 1, C.pointer(in_events), C.pointer(out_events))
    statuses.add(pl.process(plugin, C.byref(proc)))
    if blk > 10:
        for c in range(2):
            for i in range(block):
                v = out_chans[c][i]
                finite = finite and math.isfinite(v)
                peak_out = max(peak_out, abs(v))
    t += block
peak_db = 20 * math.log10(max(peak_out, 1e-12))
check(finite and 0 not in statuses, 'process 2 s of a 1 kHz sine', f'status {sorted(statuses)}, finite {finite}')
check(-30.0 < peak_db < 0.0, 'output level plausible (input peak -18.1 dBFS)', f'{peak_db:.1f} dBFS')

pl.stop_processing(plugin)
pl.deactivate(plugin)
pl.destroy(plugin)
entry.deinit()
fails = checks.count(False)
print(f'{len(checks)} checks, {fails} failed')
sys.exit(fails)
