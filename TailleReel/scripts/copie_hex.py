# =====================================================
#  copie_hex.py — script PlatformIO (post-build)
#
#  Après chaque compilation, copie firmware.hex dans
#  firmware/firmware.hex. Ce fichier est COMMITTÉ : le
#  Raspberry Pi le récupère par git pull et le flashe avec
#  teensy_loader_cli (le Pi ne peut pas compiler : la
#  toolchain tsandmann n'existe pas pour Linux ARM).
# =====================================================
Import("env")

import os
import shutil


def copier_hex(source, target, env):
    src = str(target[0])
    dossier = os.path.join(env.subst("$PROJECT_DIR"), "firmware")
    os.makedirs(dossier, exist_ok=True)
    dst = os.path.join(dossier, "firmware.hex")
    shutil.copyfile(src, dst)
    print("firmware.hex copie dans firmware/ -> a committer pour le Pi")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", copier_hex)
