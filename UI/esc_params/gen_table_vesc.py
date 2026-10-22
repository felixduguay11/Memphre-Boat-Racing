#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_table_vesc.py — regenere ui/vesc_conf_606.py pour une autre version
du firmware VESC (a lancer sur un PC, pas sur le bateau).

    git clone https://github.com/vedderb/bldc && cd bldc && git checkout release_6_06
    python3 gen_table_vesc.py bldc/confgenerator.c bldc/confgenerator.h ../ui/vesc_conf_606.py

Lit l'ordre de serialisation dans confgenerator_deserialize_*conf() et
le nom du define VESC Tool dans confgenerator_set_defaults_*conf().
"""
import re
import sys

PAT = [
    (r'conf->([\w\[\]\.]+) = buffer_get_float32_auto\(buffer, &ind\);', 'f32a', None),
    (r'conf->([\w\[\]\.]+) = buffer_get_float16\(buffer, ([\de\.\+\-]+), &ind\);', 'f16', 2),
    (r'conf->([\w\[\]\.]+) = buffer_get_float32\(buffer, ([\de\.\+\-]+), &ind\);', 'f32', 2),
    (r'conf->([\w\[\]\.]+) = buffer_get_int16\(buffer, &ind\);', 'i16', None),
    (r'conf->([\w\[\]\.]+) = buffer_get_uint16\(buffer, &ind\);', 'u16', None),
    (r'conf->([\w\[\]\.]+) = buffer_get_int32\(buffer, &ind\);', 'i32', None),
    (r'conf->([\w\[\]\.]+) = buffer_get_uint32\(buffer, &ind\);', 'u32', None),
    (r'conf->([\w\[\]\.]+) = \(int8_t\)buffer\[ind\+\+\];', 'i8', None),
    (r'conf->([\w\[\]\.]+) = buffer\[ind\+\+\];', 'u8', None),
]
# Champs dont le defaut n'est pas un define au nom VESC Tool
RENOMMER = {"HW_DEFAULT_ID": "APPCONF_CONTROLLER_ID"}


def corps(src, nom):
    m = re.search(r'^\S.*\b' + nom + r'\(.*?\{\n(.*?)^\}', src, re.S | re.M)
    if not m:
        sys.exit("fonction %s introuvable" % nom)
    return m.group(1)


def champs(src, kind):
    defauts = dict(re.findall(r'conf->([\w\[\]\.]+) = ([A-Z][A-Z0-9_]+);',
                              corps(src, "confgenerator_set_defaults_%sconf" % kind)))
    out = []
    for line in corps(src, "confgenerator_deserialize_%sconf" % kind).splitlines():
        s = line.strip()
        if not s.startswith("conf->"):
            continue
        for pat, typ, grp in PAT:
            m = re.fullmatch(pat, s)
            if m:
                d = defauts.get(m.group(1))
                if not d:
                    sys.exit("pas de define pour conf->%s" % m.group(1))
                out.append((RENOMMER.get(d, d), typ, float(m.group(grp)) if grp else None))
                break
        else:
            sys.exit("ligne non reconnue : " + s)
    return out


def main():
    src = open(sys.argv[1]).read()
    hdr = open(sys.argv[2]).read()
    sig = {k: int(re.search(k + r'\s+(\d+)', hdr).group(1))
           for k in ("MCCONF_SIGNATURE", "APPCONF_SIGNATURE")}

    def lignes(rows):
        return "\n".join("    (%r, %r, %r)," % r for r in rows)

    txt = '''# -*- coding: utf-8 -*-
"""vesc_conf_606.py — table des parametres VESC (firmware 6.06)."""

# Genere par esc_params/gen_table_vesc.py depuis confgenerator.c du firmware
# VESC. Ordre = ordre de serialisation (COMM_GET_MCCONF / COMM_GET_APPCONF).
# (nom du define VESC Tool, type, echelle)
#   f32a : float32 IEEE (buffer_append_float32_auto)
#   f16  : int16 = valeur * echelle (tronque)    f32 : int32 = valeur * echelle
#   u8/i8/u16/i16/u32/i32 : entiers big-endian

MCCONF_SIGNATURE = %d
APPCONF_SIGNATURE = %d

MCCONF_FIELDS = [
%s
]

APPCONF_FIELDS = [
%s
]
''' % (sig["MCCONF_SIGNATURE"], sig["APPCONF_SIGNATURE"], lignes(champs(src, "mc")), lignes(champs(src, "app")))
    open(sys.argv[3], "w").write(txt)
    print("ecrit", sys.argv[3])


if __name__ == "__main__":
    main()
