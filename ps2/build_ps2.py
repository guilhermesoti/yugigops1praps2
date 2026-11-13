#!/usr/bin/env python3
"""Build Yu-Gi-Oh! Forbidden Memories for the PlayStation 2 (Emotion Engine).

The whole decompiled game (src/game, the overlays the PC port links natively)
and the port's native layer (src/pc) are compiled with the ps2dev toolchain
for the R5900, over a PS2 platform (src/ps2). The steps follow
tools/pc/build_game32.py, the PC driver, with the PS2's memory model:

  * guest RAM is a TLB window at 0xC0000000 (src/ps2/force.h); the build
    mirrors src/ into the build folder with every retail RAM literal
    (0x80010000..0x801FFFFF) written as MEMORIES_RETAIL(...);
  * every indirect call (`jalr $25`) goes through Memories_Icall
    (src/ps2/thunk.S), which sends a target in guest memory to its native
    function or to the MIPS interpreter, as the PC's branch thunks do;
  * the variables the game leaves undefined are pinned at their window
    addresses, functions with no source get stubs that name themselves.

Usage: python3 ps2/build_ps2.py [--build tmp/ps2] [-j N]
Environment: PS2DEV (default /usr/local/ps2dev)."""
import argparse, concurrent.futures, csv, glob, hashlib, os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "pc"))
from build_config import (GATED_MODULES, MODULE_CONFIG, MODULES, SOURCE_ADDRESSED_MODULES,  # noqa: E402
                          TARGETS, game_sources, native_sources)
import model_gate  # noqa: E402
import model_gate_port  # noqa: E402

PS2DEV = os.environ.get("PS2DEV", "/usr/local/ps2dev")
PS2SDK = os.environ.get("PS2SDK", os.path.join(PS2DEV, "ps2sdk"))
GSKIT = os.environ.get("GSKIT", os.path.join(PS2DEV, "gsKit"))
PREFIX = os.path.join(PS2DEV, "ee", "bin", "mips64r5900el-ps2-elf-")
CC, NM, OBJCOPY, OBJDUMP, READELF, STRIP = (PREFIX + t for t in ("gcc", "nm", "objcopy", "objdump", "readelf", "strip"))
ADDRESSES = "config/pc/guest_addresses.txt"
WINDOW = 0xC0000000
SCRATCHPAD_RETAIL, SCRATCHPAD_SIZE = 0x1F800000, 0x400
SCRATCHPAD_VIEW = 0xC0300000

INCLUDES = ["-I" + PS2SDK + "/ee/include", "-I" + PS2SDK + "/common/include", "-I" + PS2SDK + "/ports/include",
            "-I" + GSKIT + "/include"]
COMMON = ["-D_EE", "-G0", "-mno-gpopt", "-fno-strict-aliasing", "-fwrapv", "-fno-optimize-sibling-calls",
          "-DMEMORIES_PC", "-DMEMORIES_PS2", "-D_LANGUAGE_C", "-DLANGUAGE_C", "-ffunction-sections", "-g0"]
# -O0 for game units, as on the PC: the original busy-waits poll globals
# the VBlank handler changes, which the optimizer would hoist.
GAME_CFLAGS = ["-std=gnu11", "-fpermissive", "-w", "-O0", "-fcommon", *COMMON,
               "-include", "src/ps2/force.h", "-Isrc", "-include", "src/pc/compat/pgxp_game.h"]
NATIVE_CFLAGS = ["-std=gnu11", "-fpermissive", "-w", "-O2", *COMMON,
                 "-include", "src/ps2/force.h", "-Isrc/ps2/compat", "-include", "src/ps2/compat/ps2_native.h",
                 "-Isrc", *INCLUDES, "-I" + PS2SDK + "/ports/include/freetype2"]

# Units of src/pc the PS2 does not build: the memory engines it replaces
# (src/ps2/guest_ps2.c), host windows and audio, the crash monitor, network
# and code mods (machine code of other CPUs).
EXCLUDED = {
    "src/pc/guest/image.c", "src/pc/guest/branch_thunks.c", "src/pc/guest/resolve.c", "src/pc/guest/state.c",
    "src/pc/guest/low_memory.c", "src/pc/guest/image_loader.c",
    "src/pc/platform/sdl.c", "src/pc/platform/x11.c", "src/pc/platform/win32.c", "src/pc/platform/android.c",
    "src/pc/platform/android_loader.c", "src/pc/platform/android_report.c", "src/pc/platform/darwin_image.c",
    "src/pc/platform/macos_fonts.c", "src/pc/platform/audio_alsa.c", "src/pc/platform/gamepad_evdev.c",
    "src/pc/platform/controls_linux.c", "src/pc/platform/update_net.c", "src/pc/platform/touch_pad.c",
    "src/pc/platform/touch_pad_art.c", "src/pc/platform/touch_pad_simple_art.c",
    "src/pc/render/gl_picture.c", "src/pc/render/present_pass.c", "src/pc/render/overlay_gl.c",
    "src/pc/render/gl_desktop_none.c", "src/pc/render/psyz_gpu.c",
    "src/pc/debug/crash.c", "src/pc/debug/crash_test.c", "src/pc/debug/monitor.c", "src/pc/debug/control_net.c",
    "src/pc/mods/object_loader.c", "src/pc/mods/hooks.c", "src/pc/mods/hd_pack.c", "src/pc/mods/import.c",
}
PS2_SOURCES = sorted(glob.glob(os.path.join(ROOT, "src/ps2/*.c")) + glob.glob(os.path.join(ROOT, "src/ps2/*.S")))

RETAIL_LITERAL = re.compile(r"\b0x80(0[1-9A-Fa-f]|1[0-9A-Fa-f])([0-9A-Fa-f]{4})[uU]?\b")
RETAIL_WRAPPED = re.compile(r"\bUINT32_C\(\s*(0x80(?:0[1-9A-Fa-f]|1[0-9A-Fa-f])[0-9A-Fa-f]{4})[uU]?\s*\)")
RAM_END = re.compile(r"\b0x80200000[uU]?\b")


def run(command, cwd=None):
    result = subprocess.run(command, capture_output=True, text=True, cwd=cwd)
    if result.returncode:
        sys.exit(f"{' '.join(command[:8])} ...\n{result.stdout}{result.stderr}")
    return result.stdout


def mirror_sources(build):
    """src/ into <build>/src with the retail RAM literals rewritten
    (force.h, MEMORIES_RETAIL). Files whose bytes would not change are left
    alone, so their objects stay current."""
    out_root = os.path.join(build, "src")
    for directory, _, files in os.walk(os.path.join(ROOT, "src")):
        rel = os.path.relpath(directory, ROOT)
        for name in files:
            if not name.endswith((".c", ".h", ".inc", ".S", ".s")):
                continue
            source = os.path.join(directory, name)
            target = os.path.join(build, rel, name)
            with open(source, "rb") as handle:
                data = handle.read()
            if name.endswith((".c", ".h", ".inc")) and not rel.startswith("src/ps2"):
                text = data.decode("utf-8", "surrogateescape")
                text = RETAIL_WRAPPED.sub(lambda m: f"({m.group(1)}u)", text)
                text = RAM_END.sub("0xC0200000u", text)
                text = RETAIL_LITERAL.sub(lambda m: f"MEMORIES_RETAIL(0x80{m.group(1)}{m.group(2)}u)", text)
                data = text.encode("utf-8", "surrogateescape")
            try:
                with open(target, "rb") as handle:
                    if handle.read() == data:
                        continue
            except OSError:
                os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "wb") as handle:
                handle.write(data)
    return out_root


JALR = re.compile(r"^(\s*)jalr\s+\$(25|t9)\s*$")
JR25 = re.compile(r"^(\s*)jr\s+\$(25|t9)\s*$")


def thunk_assembly(text):
    """Every indirect call through $25 becomes `jal Memories_Icall` (and a
    jump through it `j`): the thunk goes on to $25, or to the native
    function or interpreter for a guest address (src/ps2/thunk.S)."""
    lines = []
    for line in text.splitlines():
        found = JALR.match(line)
        if found:
            lines.append(f"{found.group(1)}jal\tMemories_Icall")
            continue
        found = JR25.match(line)
        if found:
            lines.append(f"{found.group(1)}j\tMemories_Icall")
            continue
        lines.append(line)
    return "\n".join(lines) + "\n"


UNIT_FLAGS = {"src/pc/guest/main.c": ["-Dmain=Memories_PcMain"],
              "src/pc/render/soft_gpu.c": ["-O3"],
              "src/pc/sdk/libpress.c": ["-O3"],
              "src/pc/audio/spu.c": ["-O3"],
              "src/ps2/stubs_ps2.c": ["-fno-builtin"],
              "src/ps2/irx.S": ["-Wa,-I" + PS2SDK + "/iop/irx"]}


def compile_unit(job):
    source, obj, flags, build_src = job
    flags = list(flags) + UNIT_FLAGS.get(source, [])
    path = os.path.join(build_src, os.path.relpath(source, "src")) if not source.startswith("/") else source
    newest = os.path.getmtime(path)
    if os.path.exists(obj) and os.path.getmtime(obj) >= newest and os.path.getmtime(obj) >= HEADERS_TIME:
        return
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    if path.endswith(".S"):
        run([CC, *flags, "-c", path, "-o", obj], cwd=os.path.dirname(build_src))
        return
    asm = obj[:-2] + ".s"
    run([CC, *flags, "-S", path, "-o", asm], cwd=os.path.dirname(build_src))
    with open(asm) as handle:
        text = handle.read()
    with open(asm, "w") as handle:
        handle.write(thunk_assembly(text))
    run([CC, *flags, "-c", asm, "-o", obj], cwd=os.path.dirname(build_src))
    if flags is not None and "-O0" in flags:
        # The game's calls to the C library it brought along become the
        # port's (config/pc/host_symbol_renames.txt), as on the PC.
        run([OBJCOPY, "--redefine-syms=" + os.path.join(ROOT, "config/pc/host_symbol_renames.txt"), obj])


def symbols(objects):
    defined, tentative, undefined = set(), set(), set()
    for chunk in range(0, len(objects), 200):
        for line in run([NM, "-g", *objects[chunk:chunk + 200]]).splitlines():
            parts = line.split()
            if len(parts) >= 2 and parts[-2] in "UCTDBRVWS" and not line.endswith(":"):
                {"U": undefined, "C": tentative}.get(parts[-2], defined).add(parts[-1])
    return defined, tentative, undefined


def guest_addresses():
    tables, text, section = {}, (0, 0), None
    with open(ADDRESSES) as handle:
        for line in handle:
            parts = line.split()
            if not parts or parts[0].startswith("#"):
                continue
            if parts[0] == "text":
                text = (int(parts[1], 16), int(parts[2], 16))
            elif parts[0].startswith("["):
                section = tables.setdefault(parts[0][1:-1], {})
            else:
                section[parts[0]] = int(parts[1], 16)
    for name, found in model_gate.address_sections().items():
        tables[name] = dict(found)
    return tables["SLUS_014.11"], {name: tables.get(name, {}) for name, _, _, _ in MODULES}, text


def host_address(address):
    """A retail address as the PS2 build reaches it (force.h)."""
    if SCRATCHPAD_RETAIL <= address < SCRATCHPAD_RETAIL + SCRATCHPAD_SIZE:
        return address - SCRATCHPAD_RETAIL + SCRATCHPAD_VIEW
    if (address & 0xE0000000) in (0x80000000, 0xA0000000) and (address & 0x1FFFFFFF) < 0x200000:
        return WINDOW | (address & 0x1FFFFF)
    return address


HOST_LIBC = {"printf", "sprintf", "strcmp", "strcpy", "bzero", "qsort", "memcpy", "memset",
             "memmove", "strlen", "strcat", "strncmp", "strncpy", "memcmp"}


def direct_branches(objects, names):
    """Names the objects call by name (R_MIPS_26: jal/j), of `names`."""
    called, used = set(), set()
    for chunk in range(0, len(objects), 100):
        for line in run([OBJDUMP, "-r", *objects[chunk:chunk + 100]]).splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[1].startswith("R_MIPS"):
                symbol = parts[2].split("+")[0]
                if symbol in names:
                    (called if parts[1] == "R_MIPS_26" else used).add(symbol)
    both = called & used
    if both:
        print("note: called by name and used as a value (a stub keeps the value native): " + ", ".join(sorted(both)))
    return called


def main():
    global HEADERS_TIME
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default="tmp/ps2")
    parser.add_argument("-j", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--output", default=None)
    options = parser.parse_args()
    os.chdir(ROOT)
    build = os.path.abspath(options.build)
    os.makedirs(build + "/obj", exist_ok=True)
    build_src = mirror_sources(build)
    HEADERS_TIME = max(os.path.getmtime(p) for p in [__file__] + glob.glob(build + "/src/**/*.h", recursive=True)
                       + glob.glob(build + "/src/**/*.inc", recursive=True))
    obj = lambda source: f"{build}/obj/{source.replace('/', '_')}.o"

    inventory = game_sources()
    resident = inventory["resident"]
    module_sources = {name: inventory[name] for name, _, _, _ in MODULES}
    game = resident + [s for name, _, _, _ in MODULES for s in module_sources[name]]
    native = [s for s in native_sources(TARGETS["linux"], None) if s.endswith(".c") and s not in EXCLUDED]
    native = [s for s in native if not s.startswith("src/pc/guest/translated") and "state_translated" not in s]
    native += [os.path.relpath(p, ROOT) for p in PS2_SOURCES]

    jobs = [(s, obj(s), GAME_CFLAGS, build_src) for s in game]
    jobs += [(s, obj(s), NATIVE_CFLAGS, build_src) for s in native]
    errors = []
    def safe(job):
        try:
            compile_unit(job)
        except SystemExit as error:
            errors.append(str(error))
    with concurrent.futures.ThreadPoolExecutor(options.j) as pool:
        list(pool.map(safe, jobs))
    if errors:
        for error in errors[:20]:
            print(error[:3000])
        sys.exit(f"{len(errors)} units failed")
    print(f"compiled {len(game)} game units and {len(native)} native units")

    resident_elf, module_elf, text = guest_addresses()
    module_symbols = {name: symbols([obj(s) for s in module_sources[name]]) for name, _, _, _ in MODULES}
    resident_defined = symbols([obj(s) for s in resident])[0]
    renamed, sections = {}, {}
    for name, _, _, bank in MODULES:
        if not bank:
            continue
        defined, common, wanted_here = module_symbols[name]
        others = [other for other, _, _, _ in MODULES if other != name]
        clash = {symbol for symbol in defined | common
                 if symbol in resident_defined or any(symbol in module_symbols[o][0] | module_symbols[o][1] for o in others)}
        clash |= {symbol for symbol in (wanted_here | common) - defined
                  if symbol in module_elf[name] and symbol not in resident_defined and any(
                      places.get(symbol, module_elf[name][symbol]) != module_elf[name][symbol]
                      for places in [resident_elf] + [module_elf[o] for o in others])}
        if name in GATED_MODULES:
            clash |= defined | common
        renamed[name] = {symbol: f"{name}__{symbol}" for symbol in clash if not symbol.startswith(f"{name}__")}
        for source in module_sources[name]:
            marker = obj(source) + ".renamed"
            if os.path.exists(marker) and os.path.getmtime(marker) >= os.path.getmtime(obj(source)):
                continue
            command = [OBJCOPY]
            for old, new in sorted(renamed[name].items()):
                command.append(f"--redefine-sym={old}={new}")
            for section in (".data", ".sdata"):
                command.append(f"--rename-section={section}=ovl_{name}_data")
            for section in (".bss", ".sbss"):
                command.append(f"--rename-section={section}=ovl_{name}_bss")
            run(command + [obj(source)])
            open(marker, "w").close()
        headers_text = run([OBJDUMP, "-h", *[obj(s) for s in module_sources[name]]])
        sections[name] = [kind for kind in ("data", "bss") if f"ovl_{name}_{kind}" in headers_text]

    game_defined, tentative, undefined = symbols([obj(s) for s in game])
    native_defined, native_tentative, native_undefined = symbols([obj(s) for s in native])
    with open("config/slus_01411/functions.csv") as handle:
        rows = list(csv.DictReader(handle))
    functions = {row["name"]: row["status"] for row in rows}
    overlay_rows = []
    for name, _, identifier, bank in MODULES:
        if name in SOURCE_ADDRESSED_MODULES:
            continue
        with open(f"config/slus_01411/overlays/{MODULE_CONFIG.get(name, name)}_functions.csv") as handle:
            for row in csv.DictReader(handle):
                row["name"] = renamed.get(name, {}).get(row["name"], row["name"])
                row["bank"], row["identifier"] = bank, identifier if bank else 0
                if name not in GATED_MODULES:
                    overlay_rows.append(row)
    by_address = {int(row["address"], 16): row["name"] for row in rows}
    addresses = dict(resident_elf)
    for name, _, _, _ in MODULES:
        for symbol, address in module_elf[name].items():
            addresses.setdefault(renamed.get(name, {}).get(symbol, symbol), address)
    for symbol, address in model_gate_port.read_names().items():
        addresses.setdefault(symbol, address)

    # A native definition replaces the game's: weaken the game's.
    overridden = set(game_defined & native_defined)
    if overridden:
        weaken = {}
        objects = [obj(s) for s in game]
        for chunk in range(0, len(objects), 200):
            for line in run([NM, "-A", "-g", "--defined-only", *objects[chunk:chunk + 200]]).splitlines():
                found = re.match(r"(.*?):\s*[0-9A-Fa-f]+\s", line)
                if found and line.split()[-1] in overridden:
                    weaken.setdefault(found.group(1), []).append(line.split()[-1])
        for path, hits in weaken.items():
            run([OBJCOPY, *[f"--weaken-symbol={name}" for name in hits], path])

    KEPT_FOR_MODS = {"D_801A8008"}
    wanted = (undefined | tentative | KEPT_FOR_MODS) - game_defined - native_defined - HOST_LIBC - {"_gp"}
    pinned, stubs, unknown, aliases = {}, [], [], {}
    for name in sorted(wanted):
        address = addresses.get(name)
        current = by_address.get(address if address is not None else
                                 int(name[5:], 16) if name.startswith("func_8") and len(name) == 13 else -1)
        if current and current != name and current in game_defined | native_defined:
            aliases[name] = current
            continue
        if name in functions or (address is not None and text[0] <= address < text[1]):
            stubs.append(name)
        elif address is not None:
            pinned[name] = host_address(address)
        else:
            unknown.append(name)
    for name in native_undefined - game_defined - native_defined:
        if name.startswith("D_8") and name in addresses:
            pinned[name] = host_address(addresses[name])
    stubs += [name for name in unknown if name in undefined]
    branches = {name: pinned.pop(name) for name in sorted(direct_branches([obj(s) for s in game + native], set(pinned)))}

    gen = f"{build}/gen"
    os.makedirs(gen, exist_ok=True)
    with open(f"{gen}/guest_symbols.ld", "w") as handle:
        handle.writelines(f"{name} = 0x{address:08X};\n" for name, address in pinned.items())
        handle.writelines(f"{name} = {target};\n" for name, target in aliases.items())
    with open(f"{gen}/guest_branches.S", "w") as handle:
        handle.write("/* Written by ps2/build_ps2.py: module functions called by name, through the thunk. */\n")
        handle.write(".set noreorder\n.set noat\n.text\n")
        for name, address in sorted(branches.items()):
            handle.write(f".globl {name}\n.type {name}, @function\n{name}:\n"
                         f"    lui $25, 0x{address >> 16:04X}\n    j Memories_Icall\n    ori $25, $25, 0x{address & 0xFFFF:04X}\n")
    digest = hashlib.sha256(" ".join(GAME_CFLAGS).encode())
    with open(f"{gen}/stubs.c", "w") as handle:
        handle.write('#include "pc/guest/image.h"\n')
        handle.write(f"const unsigned Memories_GameFingerprint = 0x{digest.hexdigest()[:8]}u;\n")
        handle.write(f"const unsigned Memories_RetailTextStart = 0x{text[0]:08X}u, Memories_RetailTextEnd = 0x{text[1]:08X}u;\n")
        handle.writelines(f'void {name}(void) {{ Memories_Unimplemented("{name}"); }}\n' for name in sorted(stubs))
        linked = game_defined | native_defined | set(stubs) | set(aliases)
        mapped = [(int(row["address"], 16), row["name"], row.get("bank", 0), row.get("identifier", 0))
                  for row in rows + overlay_rows if row["name"] in linked]
        mapped += [(0x8013A004, "Memories_ModelPrimaryControlA", 0, 0),
                   (0x8013B004, "Memories_ModelVariantControlA", 0, 0),
                   (0x801462B0, "Memories_DuelEffectControl", 0, 0),
                   (0x801807B0, "Memories_CreditsInit", 0, 0),
                   (0x80180A24, "Memories_CreditsUpdate", 0, 0),
                   (0x80181C4C, "Memories_CreditsLines", 0, 0),
                   (0x8017A004, "Memories_ModelPrimaryControlB", 0, 0),
                   (0x8017B004, "Memories_ModelVariantControlB", 0, 0)]
        mapped = sorted((host_address(a), n, host_address(int(b)) if b else 0, i) for a, n, b, i in mapped)
        handle.writelines(f"extern void {name}(void);\n" for name in sorted({m[1] for m in mapped} - set(stubs)))
        handle.write("const MemoriesGuestFunction Memories_FunctionMap[] = {\n")
        handle.writelines(f"    {{0x{address:08X}u, {name}, 0x{bank:08X}u, 0x{int(identifier):X}u}},\n"
                          for address, name, bank, identifier in mapped)
        handle.write(f"}};\nconst unsigned Memories_FunctionMapCount = {len(mapped)};\n")
        shared = [(name, identifier, bank) for name, _, identifier, bank in MODULES if bank and name not in GATED_MODULES]
        for name, _, _ in shared:
            for kind in sections[name]:
                handle.write(f"extern char __start_ovl_{name}_{kind}[], __stop_ovl_{name}_{kind}[];\n")
        handle.write("const MemoriesModule Memories_Modules[] = {\n")
        for name, identifier, bank in shared:
            ranges = [f"__start_ovl_{name}_{kind}, __stop_ovl_{name}_{kind}" if kind in sections[name] else "0, 0"
                      for kind in ("data", "bss")]
            handle.write(f'    {{"{name}", 0x{host_address(bank):08X}u, 0x{identifier:X}u, {", ".join(ranges)}}},\n')
        handle.write(f"}};\nconst unsigned Memories_ModuleCount = {len(shared)};\n")
    # Mods bind by name through this table (src/pc/mods/exports.h); code mods
    # are not loaded on the PS2, but data mods still look names up.
    names = game_defined | native_defined | tentative | set(pinned) | set(branches) | set(stubs)
    identifier = lambda n: (n[:1].isalpha() or n[:1] == "_") and all(c.isalnum() or c == "_" for c in n)
    table = {n: n for n in names if identifier(n) and n not in HOST_LIBC and n != "main"
             and not n.startswith(("Mods_", "Json_", "ObjectLoader_", "__"))}
    table.update((n, t) for n, t in aliases.items() if t in table)
    with open(f"{gen}/mod_exports.c", "w") as handle:
        handle.write('#include "pc/mods/exports.h"\n')
        handle.writelines(f"extern char {n}[];\n" for n in sorted(set(table.values())))
        handle.write("const MemoriesModExport Memories_ModExports[] = {\n")
        handle.writelines(f'    {{"{n}", {table[n]}}},\n' for n in sorted(table))
        handle.write(f"}};\nconst unsigned Memories_ModExportCount = {len(table)};\n")
    generated = []
    for name in ("stubs.c", "mod_exports.c", "guest_branches.S"):
        out = f"{gen}/{name}.o"
        flags = NATIVE_CFLAGS + (["-fno-builtin"] if name == "mod_exports.c" else [])
        run([CC, *flags, "-I" + build_src, "-c", f"{gen}/{name}", "-o", out])
        generated.append(out)

    output = options.output or f"{build}/YFM_PS2.ELF"
    libraries = ["-L" + PS2SDK + "/ee/lib", "-L" + PS2SDK + "/ports/lib", "-L" + GSKIT + "/lib",
                 "-lgskit", "-ldmakit", "-lpatches", "-laudsrv", "-lpad", "-lfileXio", "-lfreetype", "-lpng", "-lz",
                 "-ldebug", "-leedebug", "-lgraph", "-ldma", "-lm"]
    objects = [obj(s) for s in game + native] + generated
    with open(f"{build}/objects.txt", "w") as handle:
        handle.writelines(f"{o}\n" for o in objects)
    run([CC, "-T" + os.path.join(ROOT, "ps2/linkfile"), "-O2", "-o", output, f"@{build}/objects.txt",
         f"{gen}/guest_symbols.ld", "-Wl,-zmax-page-size=128", "-Wl,--gc-sections", f"-Wl,-Map,{build}/YFM_PS2.map",
         *libraries])
    print(f"{output}: {len(stubs)} stubs, {len(pinned)} pinned, {len(mapped)} mapped functions")


HEADERS_TIME = 0
if __name__ == "__main__":
    main()
