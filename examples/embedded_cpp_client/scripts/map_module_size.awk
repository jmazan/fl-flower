#!/usr/bin/awk -f
# Per-module memory usage from a GNU ld -Map file.
#
#   awk -f tools/map_module_size.awk build/flwr_client.map 2>totals.txt | sort -t$'\t' -k4,4nr
#
# stdout : rows of  module \t image_bytes \t ram_bytes \t total_bytes
# stderr : column header + grand totals
#
# Definitions (classification by output-section name):
#   image : bytes stored in the program image  -> "flash" on an MCU target,
#           file-backed + load segment on Linux (comparable to `size` text+data)
#   ram   : bytes occupied at runtime          -> .data + .bss + writable tables
#           (comparable to `size` data+bss)
# Non-alloc sections (.debug*, .comment, .stab*, ...) are ignored.
# Only the "Linker script and memory map" region is parsed, so the archive
# table and the "Discarded input sections" are ignored.
#
# Two quirks of GNU ld maps handled here:
#   * orphan output sections may be printed at column 0 WITHOUT a leading dot
#     (e.g. `protodesc_cold  0x...  0x...`); their memory class is inherited
#     from the preceding section.
#   * linker-synthesized sections (.plt, .dynsym, .rela.*, .eh_frame_hdr, ...)
#     are attributed to the first object listed (misleadingly crt1.o), so they
#     are regrouped under the pseudo-module "(linker-synthesized)".

BEGIN { in_map = 0; cls = "none"; prev = "img" }

/^Linker script and memory map$/ { in_map = 1; next }
!in_map                          { next }

# --- column-0 lines: LOAD/OUTPUT/GROUP bookkeeping or an output-section header
substr($0, 1, 1) != " " {
    if ($0 == "") next
    if ($1 == "LOAD" || $1 ~ /^OUTPUT/ || $1 == "START" || $1 == "END") next
    sec = $1
    cls = sec_class($1)
    if (cls != "none") prev = cls
    next
}

# --- indented lines inside the memory map ------------------------------------
{
    if ($1 == "*fill*") {                       # alignment padding
        if ($3 ~ /^0x/) add(strtonum($3), "(fill/alignment)")
        next
    }
    # input-section record: [<secname>] <addr> <size> <file>
    if (NF >= 3 && $(NF - 1) ~ /^0x/ && isobj($NF))
        add(strtonum($(NF - 1)), modname($NF))
}

# memory class of an output section: img | ram | both | none
function sec_class(name) {
    if (name ~ /^\/DISCARD/) return "none"
    if (name ~ /^(\.debug|\.comment|\.stab|\.zdebug|\.gnu_debug|\.symtab|\.strtab|\.shstrtab|\.annobin|\.gnu\.build\.attributes)/) return "none"
    if (name ~ /^\.(bss|sbss|tbss|noinit)/ || name == "COMMON") return "ram"
    if (name ~ /^\.(data|sdata|tdata|got|jcr|ctors|dtors|preinit_array|init_array|fini_array|tm_clone|dynamic$)/) return "both"
    if (name ~ /^\./) return "img"              # code, rodata, eh_frame, plt, ...
    return prev                                  # dot-less orphan: inherit neighbour
}

function isobj(f) {
    return (f ~ /\.(o|obj)\)?$/ || f ~ /\.a\(/ || f ~ /\.so(\.[0-9]+)*$/)
}

# archive(member.o) -> archive basename; CMakeFiles/<target>.dir/x.o -> target;
# linker-synthesized sections -> "(linker-synthesized)"; else basename
function modname(f,  n, a, i, c) {
    if (f ~ /\(/) {
        n = substr(f, 1, index(f, "(") - 1)
        sub(/^.*\//, "", n)
        return n
    }
    split(f, a, "/")
    for (i in a)
        if (a[i] ~ /\.dir$/) { c = a[i]; sub(/\.dir$/, "", c); return c }
    n = f
    sub(/^.*\//, "", n)
    return n
}

# sections ld builds itself from the whole link, not from one object
function synthetic(s) {
    return (s ~ /^(\.plt|\.dynsym|\.dynstr|\.hash|\.gnu\.hash|\.gnu\.version|\.rela\.|\.relr\.|\.dynamic$|\.interp$|\.eh_frame_hdr$|\.got\.plt$|\.note\.gnu\.build-id$)/)
}

function add(sz, mod) {
    if (sec == "" || sz <= 0 || cls == "none") return
    if (synthetic(sec)) mod = "(linker-synthesized)"
    if (cls == "ram")         ram[mod] += sz
    else if (cls == "both") { img[mod] += sz; ram[mod] += sz }
    else                      img[mod] += sz
}

END {
    ti = 0; tr = 0
    for (m in img) ti += img[m]
    for (m in ram) tr += ram[m]
    printf "module\timage_bytes\tram_bytes\ttotal_bytes\n" > "/dev/stderr"
    printf "TOTAL\t%d\t%d\t%d\n", ti, tr, ti + tr > "/dev/stderr"
    for (m in img)
        printf "%s\t%d\t%d\t%d\n", m, img[m], ram[m] + 0, img[m] + ram[m]
    for (m in ram)
        if (!(m in img)) printf "%s\t0\t%d\t%d\n", m, ram[m], ram[m]
}