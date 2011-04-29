import re
import os

# The following python subroutine and the subsequent scons command
# shows how to do general template filling. This system should perhaps
# be put in a separate directory, or even be made part of scons.
def patch_src(env, target, source):
    out = open(str(target[0]), "wb")
    inp = open(str(source[0]), "r")

    in_area = 0
    for line in inp.readlines():
        if in_area:
            line = re.sub('G_TYPE_OBJECT',
                          'GTK_TYPE_ADJUSTMENT',
                          line)
            if re.search(r'\);', line):
                in_area=0
        else:
            if re.search(r"object_signals\[SET_SCROLL_ADJUSTMENTS_SIGNAL\]",line):
                in_area=1

        out.write(line)
        
    out.close()
    inp.close()

env = Environment(CPPFLAGS=['-g','-Wall'])

env['SBOX'] = 'SBOX_UNAME_MACHINE' in os.environ
packages = ['gtk+-2.0']
cppdefines = []
if env['SBOX']:
    env['ENV'] = os.environ
    cppdefines += ['USE_HILDON=1']
    packages += ['hildon-1']

env.ParseConfig("pkg-config --cflags --libs " + " ".join(packages))
env.Append(CPPDEFINES=cppdefines)
env.Program("gmeasure",
            ["dovtk-lasso.c",
             "gmeasure.c",
             "gtk-image-viewer-fixed.c",
             "giv-calibrate-dialog.c"],
            LIBS=['m']+env['LIBS'])

for f in ["gtk-image-viewer.gob",
          "giv-calibrate-dialog.gob"
          ]:
    env.Command([ re.sub(r"\.gob", ".h", f),
                  re.sub(r"\.gob", ".c", f) ],
                f,
                "gob2  $SOURCES")

env.Command("gtk-image-viewer-fixed.c",
            ["gtk-image-viewer.c"],
            patch_src)
