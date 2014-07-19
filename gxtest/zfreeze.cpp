// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <initializer_list>
#include "Test.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wiiuse/wpad.h>
#include "cgx.h"
#include "cgx_defaults.h"
#include "gxtest_util.h"
#include <ogcsys.h>


void setZfreeze(bool on)
{
    auto genmode = CGXDefault<GenMode>();
    genmode.zfreeze = on;
    CGX_LOAD_BP_REG(genmode.hex);
}

void init()
{
    CGX_LOAD_BP_REG(CGXDefault<TwoTevStageOrders>(0).hex);

    CGX_BEGIN_LOAD_XF_REGS(0x1009, 1);
    wgPipe->U32 = 1; // 1 color channel

    LitChannel chan;
    chan.hex = 0;
    chan.matsource = 1; // from vertex
    CGX_BEGIN_LOAD_XF_REGS(0x100e, 1); // color channel 1
    wgPipe->U32 = chan.hex;
    CGX_BEGIN_LOAD_XF_REGS(0x1010, 1); // alpha channel 1
    wgPipe->U32 = chan.hex;

    CGX_LOAD_BP_REG(CGXDefault<TevStageCombiner::AlphaCombiner>(0).hex);

    auto genmode = CGXDefault<GenMode>();
    genmode.numtevstages = 0; // One stage
    CGX_LOAD_BP_REG(genmode.hex);

    PE_CONTROL ctrl;
    ctrl.hex = BPMEM_ZCOMPARE<<24;
    ctrl.pixel_format = PIXELFMT_RGB8_Z24;
    ctrl.zformat = ZC_LINEAR;
    ctrl.early_ztest = 0;
    CGX_LOAD_BP_REG(ctrl.hex);

    CGX_SetViewport(20.0f, 20.0f, 50.0f, 50.0f, 0.0f, 1.0f); // stuff which really should not be filled
    auto cc = CGXDefault<TevStageCombiner::ColorCombiner>(0);
    cc.d = TEVCOLORARG_RASC;
    CGX_LOAD_BP_REG(cc.hex);
}

void clear()
{
    setZfreeze(false);
    auto zmode = CGXDefault<ZMode>();
            zmode.func = COMPARE_ALWAYS;
            zmode.testenable = 1;
            zmode.updateenable = 1;
    CGX_LOAD_BP_REG(zmode.hex);

    // First off, clear previous screen contents

    GXTest::Quad().ColorRGBA(0,0,0,0xff).Draw();

    zmode.func = COMPARE_GEQUAL;
    CGX_LOAD_BP_REG(zmode.hex);
}

void debug()
{
    GXTest::DebugDisplayEfbContents();

    while (1)
    {
        WPAD_ScanPads();
        if (WPAD_ButtonsDown(0))
            break;
    }
}

void copyZtexture(void *dest) {
    CGX_DoEfbCopyTex(20, 20, 50, 50, GX_TF_Z24X8, false, dest);
}

bool CompareZbuffers(void *a, void *b) {
    memcmp(a, b, 52 * 52 * 4) == 0;
}

void ZfreezeTest()
{
    START_TEST();

    u8 depth1[52*52*4];
    u8 depth2[52*52*4];

    init();
    clear();

// Subtest 1
// Start by confirming that the zbuffer is working as expected.

    // draw zfreeze quad
    auto zquad = GXTest::Quad().ColorRGBA(0,0,0xff,0xff);
    zquad.VertexTopLeft(-1.0f, 1.0f, 0.2f).VertexBottomLeft(-1.0f, -1.0f, 0.5f);
    zquad.VertexTopRight(1.0f, 1.0f, 0.5f).VertexBottomRight(1.0f, -1.0f, 0.8f);
    zquad.Draw();

    copyZtexture(depth1);

    // and draw red test quad
    auto testquad = GXTest::Quad().AtDepth(0.5).ColorRGBA(0xff,0,0,0xff);
    testquad.Draw();

    copyZtexture(depth2);

    GXTest::CopyToTestBuffer(20, 20, 69, 69);

    CGX_WaitForGpuToFinish();

    auto r1 = GXTest::ReadTestBuffer(15, 15, 50);
    auto r2 = GXTest::ReadTestBuffer(35, 35, 50);
    DO_TEST(r1.r == 0 && r1.b == 0xff && r2.r == 0xff && r2.b == 0 && !CompareZbuffers(depth1, depth2),
            "Depth buffer not working. Expected top left blue (%i), bottom right red (%i) and non-identical depth (%i)",
            r1.r == 0 && r1.b == 0xff, r2.r == 0xff && r2.b == 0, !CompareZbuffers(depth1, depth2));
    //debug();

// Subtest 2
// Enable Zfreeze, expect no change to the depth buffer, test quad to be drawn on top of zquad

    clear();

    zquad.Draw();
    copyZtexture(depth1);

    setZfreeze(true);
    testquad.Draw();
    copyZtexture(depth2);

    GXTest::CopyToTestBuffer(20, 20, 69, 69);

    CGX_WaitForGpuToFinish();

    r1 = GXTest::ReadTestBuffer(15, 15, 50);
    r2 = GXTest::ReadTestBuffer(35, 35, 50);
    DO_TEST(r1.r == 0xff && r1.b == 0 && r2.r == 0xff && r2.b == 0 && CompareZbuffers(depth1, depth2),
            "Zfreeze broken. Expected top left red (%i), bottom right red (%i) and identical depth (%i)",
            r1.r == 0xff && r1.b == 0, r2.r == 0xff && r2.b == 0, CompareZbuffers(depth1, depth2));
    //debug();

    // Subtest 3
    // Test with more random polygons.



    // Subtest 4
    // Check that zfreeze still works with polygons that aren't inside of the
    // zfreeze plane


// Subtest 5
// Test with polygons clipped on the near/far plane

    clear();

    zquad.Draw();

    setZfreeze(true);
    // Quad below the z=w buffer, it will get clipped and not drawn
    GXTest::Quad().AtDepth(1.1).ColorRGBA(0xff, 0, 0, 0xff).Draw();

    GXTest::CopyToTestBuffer(20, 20, 69, 69);
    CGX_WaitForGpuToFinish();

    r1 = GXTest::ReadTestBuffer(15, 15, 50);
    r2 = GXTest::ReadTestBuffer(35, 35, 50);
    DO_TEST(r1.r == 0 && r1.b == 0xff && r2.r == 0 && r2.b == 0xff,
            "Clipped polygon drawn. Expected top left blue (%i), bottom right blue (%i)",
            r1.r == 0 && r1.b == 0xff, r2.r == 0 && r2.b == 0xff);
    //debug();

// Subtest 6
// Test with a zfreeze plane that gets clipped. The clipped polygon is ignored
// and the previous polygon actually rendered becomes the zfreeze plane.

    clear();

    // draw a green quad just below the test quad.
    // If zfreeze fails on the quad below, it will become the zfreeze plane

    GXTest::Quad().AtDepth(0.55).ColorRGBA(0,0xff,0x00,0xff).Draw();

    // zquad that is outside the clip region.
    zquad.VertexTopLeft(1.1f, 1.0f, 0.8f).VertexBottomLeft(1.1f, -1.0f, 0.2f);
    zquad.VertexTopRight(1.3f, 1.0f, 0.8f).VertexBottomRight(1.3f, -1.0f, 0.2f);
    zquad.Draw();

    setZfreeze(true);

    testquad.Draw();

    GXTest::CopyToTestBuffer(20, 20, 69, 69);

    CGX_WaitForGpuToFinish();

    r1 = GXTest::ReadTestBuffer(15, 15, 50);
    r2 = GXTest::ReadTestBuffer(35, 35, 50);
    DO_TEST(r1.r == 0xff && r1.g == 0 && r2.r == 0xff && r2.g == 0,
            "Zfreeze frozen on clipped polygon. Expected top red (%i) and bottom red (%i)",
            r1.r == 0xff && r1.g == 0, r2.r == 0xff && r2.g == 0);
    //debug();

// Subtest 7
// Test with a zfreeze plane beyond the near/far plane (but unclipped)

// Subtest 8
// Check with non-flat quads as the zfreeze plane, which triangle becomes the plane?

    clear();

    auto unflat_quad = GXTest::Quad().ColorRGBA(0, 0, 0xff, 0xff);
    unflat_quad.VertexTopLeft(-1.0f, 1.0f, 0.5f).VertexBottomLeft(-1.0f, -1.0f, 1.0f);
    unflat_quad.VertexTopRight(1.0f, 1.0f, 1.0f).VertexBottomRight(1.0f, -1.0f, 0.5f);
    unflat_quad.Draw();

    setZfreeze(true);
    testquad.Draw();
    setZfreeze(false);

    GXTest::Quad().AtDepth(0.5).ColorRGBA(0, 0xff, 0, 0xff).Draw();

    GXTest::CopyToTestBuffer(20, 20, 69, 69);
    CGX_WaitForGpuToFinish();

    r1 = GXTest::ReadTestBuffer(35, 15, 50);
    r2 = GXTest::ReadTestBuffer(15, 35, 50);
    DO_TEST(r1.r == 0xff && r1.g == 0 && r2.r == 0 && r2.g == 0xff,
            "Incorrect Zfreeze plane on unflat quad. Expected top right red (%i) and bottom left green (%i)",
            r1.r == 0xff && r1.g == 0, r2.r == 0 && r2.g == 0xff);
    //debug();

    // Subtest 9
    // Enable depth tests, check that GEQUAL and EQUAL work as expected.


    // TODO test lines as zplanes. (and zfrozen)
    // TODO test polygons inside viewport but outside sissor rect.
    // TODO test zplanes that are clipped with cullmode: CULL_ALL (cullmode register = 3)
    END_TEST();
}


int main()
{
    network_init();
    WPAD_Init();

    GXTest::Init();

    ZfreezeTest();

    network_printf("Shutting down...\n");
    network_shutdown();

    return 0;
}
