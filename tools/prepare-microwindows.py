"""Patch generated Microwindows sources for the DDOS screen backend."""

import sys
from pathlib import Path


root = Path(sys.argv[1])


def patch(file, old, new):
    path = root / file
    text = path.read_text()
    if new in text:
        return
    if text.count(old) != 1:
        raise SystemExit(f"Unsupported Microwindows source at {file}: {old!r}")
    path.write_text(text.replace(old, new), newline="\n")


patch("drivers/scr_fb.c", "SCREENDEVICE\tscrdev = {",
      '#include "ddos_present.c"\n\nSCREENDEVICE\tscrdev = {')
patch("drivers/scr_fb.c", "NULL,\t\t\t\t/* Update*/\n\tNULL\t\t\t\t/* PreSelect*/",
      "ddos_damage,\t\t\t/* Update*/\n\tddos_present\t\t\t/* PreSelect*/")
patch("drivers/scr_fb.c", "MAP_SHARED,fb,0);", "MAP_SHARED,fb,psd->size);")
patch("drivers/scr_fb.c", "MAP_SHARED,fb,psd->size);\n#endif",
      "MAP_SHARED,fb,psd->size);\n"
      "\tif (psd->addr == (unsigned char *)-1) {\n"
      "\t\tpsd->Update = NULL;\n\t\tpsd->PreSelect = NULL;\n"
      "\t\tpsd->addr = mmap(NULL, psd->size, PROT_READ|PROT_WRITE,MAP_SHARED,fb,0);\n\t}\n#endif")
patch("nanox/srvmain.c", "void\nGsSelect(GR_TIMEOUT timeout)\n{\n\tfd_set\trfds;",
      "extern void ddos_frame_activity(int fd);\n\nvoid\nGsSelect(GR_TIMEOUT timeout)\n{\n\tfd_set\trfds;")
patch("nanox/srvmain.c", "\t\t\tif(FD_ISSET(curclient->id, &rfds))\n\t\t\t\tGsHandleClient(curclient->id);",
      "\t\t\tif(FD_ISSET(curclient->id, &rfds)) {\n"
      "\t\t\t\tint fd = curclient->id;\n\t\t\t\tGsHandleClient(fd);\n\t\t\t\tddos_frame_activity(fd);\n\t\t\t}")

patch("engine/font_freetype2.c", '".ttf|.otf|.pfr"', '".ttf|.otf|.woff|.pfr"')
patch("engine/devfont.c", "    if (access(fullpath, R_OK) != 0)\n        return NULL;",
      '    FILE *fontfile = fopen(fullpath, "rb");\n'
      "    if (!fontfile)\n        return NULL;\n    fclose(fontfile);")

patch("nanox/nxproto.h", "#define GrTotalNumCalls         126",
      """#define GrNumDdosFrame           126
typedef struct {
    BYTE8 reqType;
    BYTE8 hilength;
    UINT16 length;
    UINT32 begin;
} nxDdosFrameReq;

#define GrTotalNumCalls         127""")
patch("include/nano-X.h", "void\t\tGrFlush(void);",
      "void\t\tGrFlush(void);\nvoid\t\tGrDdosFrame(GR_BOOL begin);")
patch("nanox/client.c", "void \nGrFlush(void)\n{",
      """void
GrDdosFrame(GR_BOOL begin)
{
    LOCK(&nxGlobalLock);
    nxDdosFrameReq *req = AllocReq(DdosFrame);
    req->begin = begin;
    if (!begin)
        nxFlushReq(0L, 1);
    UNLOCK(&nxGlobalLock);
}

void \nGrFlush(void)
{""")
patch("nanox/srvnet.c", "/* 125 */ {GrDrawImagePartToFitWrapper, \"GrDrawImagePartToFit\"},",
      "/* 125 */ {GrDrawImagePartToFitWrapper, \"GrDrawImagePartToFit\"},\n"
      "\t/* 126 */ {GrDdosFrameWrapper, \"GrDdosFrame\"},")
patch("nanox/srvnet.c", "void\nGsDropClient(int fd)",
      "extern void ddos_frame_state(int fd, int begin);\n\nvoid\nGsDropClient(int fd)")
patch("nanox/srvnet.c", "if((client = GsFindClient(fd))) { /* If it exists */",
      "if((client = GsFindClient(fd))) { /* If it exists */\n\t\tddos_frame_state(fd, 0);")
patch("nanox/srvnet.c", "static void\nGrCreateFontFromBufferWrapper(void *r)",
      """extern void ddos_frame_state(int fd, int begin);

static void
GrDdosFrameWrapper(void *r)
{
    nxDdosFrameReq *req = r;
    ddos_frame_state(current_fd, req->begin != 0);
}

static void
GrCreateFontFromBufferWrapper(void *r)""")
